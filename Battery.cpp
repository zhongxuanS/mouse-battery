#include "Battery.h"
#include <windows.h>
#include <cfgmgr32.h>
extern "C"
{
#include <hidsdi.h>
}
#include <algorithm>
#include <array>
#include <vector>

namespace
{
using Report = std::array<BYTE, 20>;

struct Handle
{
    HANDLE value;
    explicit Handle(HANDLE h) : value(h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle()
    {
        if (value && value != INVALID_HANDLE_VALUE)
        {
            CloseHandle(value);
        }
    }
};

class HidPort
{
public:
    HANDLE file;
    DWORD size;
    BYTE softwareId;

    bool Transfer(Report& data, bool write, DWORD timeout)
    {
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.value)
        {
            return false;
        }

        OVERLAPPED operation{};
        operation.hEvent = event.value;
        DWORD count = 0;
        BOOL done = write ? WriteFile(file, data.data(), size, &count, &operation)
                          : ReadFile(file, data.data(), size, &count, &operation);
        if (!done)
        {
            if (GetLastError() != ERROR_IO_PENDING)
            {
                return false;
            }

            // 超时后先等驱动确认取消，再释放报告缓冲区。
            if (WaitForSingleObject(event.value, timeout) != WAIT_OBJECT_0)
            {
                CancelIoEx(file, &operation);
            }

            done = GetOverlappedResult(file, &operation, &count, TRUE);
        }

        return done && count == size;
    }

    bool Query(BYTE device, BYTE feature, BYTE function, Report& reply, BYTE first = 0, BYTE second = 0)
    {
        //1. 为每次请求分配标识，跳过上一条请求的迟到答复。
        softwareId = softwareId % 15 + 1;
        Report request{ BYTE(size == 20 ? 0x11 : 0x10), device, feature,
                        BYTE(function << 4 | softwareId), first, second };
        if (!Transfer(request, true, 600))
        {
            return false;
        }

        //2. 只接收目标设备对本次请求的回复。
        ULONGLONG end = GetTickCount64() + 600;
        for (ULONGLONG now = GetTickCount64(); now < end; now = GetTickCount64())
        {
            if (!Transfer(reply, false, DWORD(end - now)))
            {
                break;
            }
            if (reply[0] != request[0] || reply[1] != device)
            {
                continue;
            }

            if (reply[2] == feature && reply[3] == request[3])
            {
                return true;
            }
            if ((reply[2] == 0xff || reply[2] == 0x8f) && reply[3] == feature && reply[4] == request[3])
            {
                break;
            }
        }

        return false;
    }

    BYTE Feature(BYTE device, unsigned id)
    {
        Report reply{};
        return Query(device, 0, 0, reply, BYTE(id >> 8), BYTE(id)) ? reply[4] : 0;
    }
};

int ReportLength(const wchar_t* path)
{
    Handle info(CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    PHIDP_PREPARSED_DATA data = nullptr;
    if (info.value == INVALID_HANDLE_VALUE || !HidD_GetPreparsedData(info.value, &data))
    {
        return 0;
    }

    HIDP_CAPS caps{};
    NTSTATUS status = HidP_GetCaps(data, &caps);
    HidD_FreePreparsedData(data);
    if (status != HIDP_STATUS_SUCCESS || caps.UsagePage != 0xff00 ||
        caps.InputReportByteLength != caps.OutputReportByteLength)
    {
        return 0;
    }

    if ((caps.Usage == 1 && caps.InputReportByteLength == 7) ||
        (caps.Usage == 2 && caps.InputReportByteLength == 20))
    {
        return caps.InputReportByteLength;
    }

    return 0;
}

std::wstring ReadName(HidPort& port, BYTE device, BYTE index)
{
    Report reply{};
    if (!port.Query(device, index, 0, reply) || !reply[4])
    {
        return L"罗技鼠标";
    }

    int length = reply[4];
    std::string name;
    while (int(name.size()) < length)
    {
        if (!port.Query(device, index, 1, reply, BYTE(name.size())))
        {
            return L"罗技鼠标";
        }

        int count = std::min(int(port.size) - 4, length - int(name.size()));
        name.append(reinterpret_cast<const char*>(&reply[4]), count);
    }

    wchar_t result[256]{};
    MultiByteToWideChar(CP_UTF8, 0, name.data(), length, result, 255);
    return result[0] ? result : L"罗技鼠标";
}

int EstimatePercent(int mv)
{
    // 相邻采样点之间按罗技锂电池的放电曲线估算。
    static const int volts[] = { 3500,3579,3646,3671,3717,3751,3778,3811,3859,3922,3989,4067,4186 };
    static const int levels[] = { 0,2,5,10,20,30,40,50,60,70,80,90,100 };
    if (mv <= volts[0])
    {
        return 0;
    }
    for (int i = 1; i < int(_countof(volts)); ++i)
    {
        if (mv <= volts[i])
        {
            return levels[i - 1] + MulDiv(mv - volts[i - 1], levels[i] - levels[i - 1], volts[i] - volts[i - 1]);
        }
    }
    return 100;
}

bool Decode(unsigned feature, bool percentage, const Report& r, BatteryReading& reading)
{
    //1. 鼠标只上报档位时，保留原始档位。
    if (feature == 0x1004 && !percentage)
    {
        switch (r[5])
        {
        case 1: reading.value = L"极低"; break;
        case 2: reading.value = L"低"; break;
        case 4: reading.value = L"中"; break;
        case 8: reading.value = L"高"; break;
        default: return false;
        }

        reading.charging = r[6] == 1 || r[6] == 2;
        return true;
    }

    //2. 优先使用设备百分比，仅在电压接口上估算。
    if (feature == 0x1001 || feature == 0x1f20)
    {
        int mv = (r[4] << 8) | r[5];
        if (mv < 2800 || mv > 4500 || (feature == 0x1f20 && !(r[6] & 1)))
        {
            return false;
        }

        reading.percent = EstimatePercent(mv);
        reading.estimated = true;
        reading.charging = feature == 0x1001 ? (r[6] & 0x87) == 0x80 : r[6] == 3;
    }
    else
    {
        if (r[4] > 100 || (feature == 0x1000 && !r[4]))
        {
            return false;
        }

        reading.percent = r[4];
        reading.charging = r[6] == 1 || r[6] == 2;
    }

    reading.value = std::to_wstring(reading.percent);
    return true;
}

bool ReadSlot(HidPort& port, BYTE device, BatteryReading& reading)
{
    //1. 先确认配对设备是鼠标。
    BYTE nameIndex = port.Feature(device, 5);
    if (!nameIndex && device == 1)
    {
        Sleep(150);
        nameIndex = port.Feature(device, 5);
    }

    Report reply{};
    if (!nameIndex || !port.Query(device, nameIndex, 2, reply) || reply[4] != 3)
    {
        return false;
    }

    //2. 按设备支持的电量功能读取，成功后补全型号。
    for (unsigned feature : { 0x1004, 0x1000, 0x1001, 0x1f20 })
    {
        BYTE index = port.Feature(device, feature);
        if (!index)
        {
            continue;
        }

        bool unified = feature == 0x1004;
        bool percentage = unified && port.Query(device, index, 0, reply) && (reply[5] & 2);
        if (port.Query(device, index, unified ? 1 : 0, reply) && Decode(feature, percentage, reply, reading))
        {
            reading.device = ReadName(port, device, nameIndex);
            reading.detail = reading.charging ? L"充电中" : (reading.Low() ? L"电量偏低" : L"剩余电量");
            return true;
        }
    }

    return false;
}
}

bool BatteryReading::Low() const
{
    return (percent >= 0 && percent <= 20) || value == L"低" || value == L"极低";
}

std::wstring BatteryReading::Summary() const
{
    if (device.empty())
    {
        return detail;
    }

    std::wstring amount = percent >= 0 ? value + L"%" : L"电量" + value;
    return device + L" · " + (estimated ? L"约 " : L"") + amount + (charging ? L" · 充电中" : L"");
}

BatteryReading ReadMouseBattery()
{
    BatteryReading result;
    result.detail = L"暂未发现鼠标，请检查接收器";

    //1. 枚举已连接的罗技 USB HID 接口。
    GUID hid{};
    HidD_GetHidGuid(&hid);
    ULONG size = 0;
    if (CM_Get_Device_Interface_List_SizeW(&size, &hid, nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS)
    {
        result.detail = L"Windows 无法枚举 HID 设备";
        return result;
    }

    std::vector<wchar_t> paths(size);
    if (!size || CM_Get_Device_Interface_ListW(&hid, nullptr, paths.data(), size, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS)
    {
        result.detail = L"设备列表已变更，等待刷新";
        return result;
    }

    //2. 长报告优先，依次检查接收器槽位和 USB 直连地址。
    std::vector<std::pair<int, const wchar_t*>> ports;
    const wchar_t prefix[] = LR"(\\?\hid#vid_046d&)";
    for (const wchar_t* path = paths.data(); *path; path += wcslen(path) + 1)
    {
        if (_wcsnicmp(path, prefix, _countof(prefix) - 1) == 0)
        {
            int length = ReportLength(path);
            if (length)
            {
                ports.emplace_back(length, path);
            }
        }
    }
    std::stable_sort(ports.begin(), ports.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

    for (const auto& candidate : ports)
    {
        Handle file(CreateFileW(candidate.second, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
        if (file.value == INVALID_HANDLE_VALUE)
        {
            result.detail = L"无法打开罗技设备，等待刷新";
            continue;
        }

        result.detail = L"电量暂不可用，移动鼠标后可刷新";
        HidPort port{ file.value, DWORD(candidate.first), BYTE((GetTickCount64() ^ GetCurrentProcessId()) % 15) };
        constexpr BYTE slots[] = { 1, 2, 255, 3, 4, 5, 6 };
        for (BYTE device : slots)
        {
            if (ReadSlot(port, device, result))
            {
                return result;
            }
        }
    }

    return result;
}
