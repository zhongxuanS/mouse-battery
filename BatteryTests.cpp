// 测试直接调用报告解码函数，发布程序无需提供模拟设备入口。
#include "Battery.cpp"
#include "Tray.cpp"
#include <cassert>
#include <cstdio>

static void CheckCardDigits(int percent, int scale)
{
    //1. 使用实际卡片绘制代码，在内存位图上按指定比例渲染。
    int width = MulDiv(CardWidth, scale, 100);
    int height = MulDiv(CardHeight, scale, 100);
    HWND window = CreateWindowW(L"STATIC", L"", WS_POPUP, 0, 0, width, height, nullptr, nullptr, nullptr, nullptr);
    assert(window);
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    HGDIOBJ previous = SelectObject(dc, bitmap);

    current.device = L"G304 X LIGHTSPEED";
    current.percent = percent;
    current.value = std::to_wstring(percent);
    DrawCard(window, dc);

    //2. 只检查数字的区域，忽略百分号和标题。
    int litPixels = 0;
    for (int y = 77; y < 121; y += 2)
    {
        for (int x = 22; x < 75; x += 2)
        {
            COLORREF pixel = GetPixel(dc, MulDiv(x, scale, 100), MulDiv(y, scale, 100));
            if (pixel != CLR_INVALID && GetRValue(pixel) > 200)
            {
                ++litPixels;
            }
        }
    }

    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    DestroyWindow(window);
    assert(litPixels > 30);
}

int main()
{
    //1. 检查电量百分比、档位、充电状态和非法报告。
    BatteryReading result;
    Report data{};
    data[4] = 95;
    assert(Decode(0x1004, true, data, result) && result.percent == 95 && !result.Low());

    data[4] = 0;
    data[6] = 1;
    assert(Decode(0x1004, true, data, result) && result.percent == 0 && result.Low() && result.charging);

    result = BatteryReading{};
    data[5] = 2;
    assert(Decode(0x1004, false, data, result) && result.value == L"低" && result.Low());

    data[4] = 101;
    assert(!Decode(0x1004, true, data, result));

    result = BatteryReading{};
    data[4] = 35;
    data[6] = 0;
    assert(Decode(0x1000, false, data, result) && result.percent == 35);

    //2. 电压估算显示“约”，未知状态保留设备提示。
    data[4] = 14;
    data[5] = 227;
    data[6] = 128;
    assert(Decode(0x1001, false, data, result) && result.percent == 50 && result.charging);
    result.device = L"Mouse";
    assert(result.Summary() == L"Mouse · 约 50% · 充电中");

    data = {};
    assert(!Decode(0x1f20, false, data, result));
    result = BatteryReading{};
    assert(result.Summary() == result.detail && !result.Low());
    GdiplusStartupInput input;
    ULONG_PTR token = 0;
    assert(GdiplusStartup(&token, &input, nullptr) == Ok);
    CheckCardDigits(95, 100);
    CheckCardDigits(95, 250);
    CheckCardDigits(100, 100);
    CheckCardDigits(100, 250);
    GdiplusShutdown(token);
    puts("9 battery checks and 4 card rendering checks passed.");
}
