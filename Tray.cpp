#include "Battery.h"
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <algorithm>
#include <process.h>
#include <cstdio>
#include <cwchar>

namespace Gdiplus
{
using std::min;
using std::max;
}
#pragma warning(push, 3)
#include <gdiplus.h>
#pragma warning(pop)

using namespace Gdiplus;

namespace
{
constexpr UINT TrayMessage = WM_APP + 1;
constexpr UINT BatteryReady = WM_APP + 2;
constexpr UINT Refresh = 1, Details = 2, Exit = 3;
constexpr int CardWidth = 324, CardHeight = 214;

const Color Canvas(24, 34, 38), Mint(157, 240, 205), Amber(255, 199, 116);
const Color Ink(241, 247, 245), Muted(174, 193, 192), Line(58, 76, 79);
BatteryReading current, pending;
HANDLE worker = nullptr;
HICON icon = nullptr;
UINT taskbarCreated = 0;
bool registered = false;
SYSTEMTIME checked{};

Color Accent()
{
    if (current.device.empty())
    {
        return Muted;
    }
    return current.Low() ? Amber : Mint;
}

void Rounded(Graphics& g, const Color& color, REAL x, REAL y, REAL w, REAL h, REAL r)
{
    GraphicsPath path;
    REAL d = r * 2;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();

    SolidBrush brush(color);
    g.FillPath(&brush, &path);
}

REAL Text(Graphics& g, const std::wstring& text, const wchar_t* face, REAL size,
          int style, Color color, REAL x, REAL y, REAL width, REAL height)
{
    Font font(face, size, style, UnitPixel);
    SolidBrush brush(color);
    StringFormat format(StringFormat::GenericTypographic());
    format.SetFormatFlags((format.GetFormatFlags() & ~StringFormatFlagsLineLimit) | StringFormatFlagsNoWrap);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    format.SetLineAlignment(StringAlignmentCenter);
    RectF box(x, y, width, height), bounds;
    g.DrawString(text.c_str(), -1, &font, box, &format, &brush);
    g.MeasureString(text.c_str(), -1, &font, box, &format, &bounds);
    return bounds.Width;
}

HICON MakeIcon(int size)
{
    //1. 在透明画布上绘制高对比圆角数字，三位电量也留出边距。
    Bitmap bitmap(size, size, PixelFormat32bppARGB);
    Graphics g(&bitmap);
    g.ScaleTransform(size / 32.f, size / 32.f);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    Rounded(g, Accent(), 0, 1, 32, 30, 6);

    std::wstring value = current.value == L"极低" ? L"危" : current.value;
    FontFamily family(current.device.empty() || current.percent >= 0 ? L"Segoe UI" : L"Microsoft YaHei UI");
    GraphicsPath glyphs;
    glyphs.AddString(value.c_str(), -1, &family, FontStyleBold, 25.f, PointF(0, 0), nullptr);
    RectF bounds;
    glyphs.GetBounds(&bounds);
    if (bounds.Width > 0 && bounds.Height > 0)
    {
        REAL scale = std::min(29.f / bounds.Width, 22.f / bounds.Height);
        Matrix fit(scale, 0, 0, scale, (32 - bounds.Width * scale) / 2 - bounds.X * scale,
                   (32 - bounds.Height * scale) / 2 - bounds.Y * scale);
        glyphs.Transform(&fit);
        SolidBrush brush(Canvas);
        g.FillPath(&brush, &glyphs);
    }

    //2. 系统托盘复制图标，调用方负责释放生成的句柄。
    HICON result = nullptr;
    bitmap.GetHICON(&result);
    return result;
}

void DrawMouse(Graphics& g, Color accent)
{
    // 用俯视轮廓表示鼠标，状态色只点亮滚轮。
    GraphicsState saved = g.Save();
    g.TranslateTransform(259.f, 99.f);
    g.RotateTransform(16.f);
    SolidBrush shadow(Color(15, 24, 27));
    g.FillEllipse(&shadow, -30, -38, 66, 87);

    GraphicsPath body;
    body.AddBezier(-28, -9, -27, -35, -17, -47, 0, -47);
    body.AddBezier(0, -47, 18, -47, 29, -32, 30, -6);
    body.AddBezier(30, -6, 32, 23, 24, 44, 3, 46);
    body.AddBezier(3, 46, -19, 47, -33, 29, -28, -9);
    body.CloseFigure();
    LinearGradientBrush shell(Point(0, -47), Point(0, 46), Color(61, 82, 85), Color(37, 50, 55));
    Pen edge(Color(102, 130, 130), 1.3f);
    g.FillPath(&shell, &body);
    g.DrawPath(&edge, &body);

    Pen seam(Color(25, 39, 42), 1.6f);
    g.DrawLine(&seam, 0, -46, 0, -7);
    g.DrawBezier(&seam, -27, -7, -13, 0, 13, 0, 29, -8);
    Rounded(g, Color(19, 31, 34), -5, -31, 10, 21, 5);
    Rounded(g, accent, -2, -28, 4, 13, 2);
    g.Restore(saved);
}

void DrawCard(HWND window, HDC dc)
{
    //1. 按窗口实际像素缩放，在内存中画完整张卡片。
    RECT area{};
    GetClientRect(window, &area);
    if (!area.right || !area.bottom)
    {
        return;
    }

    Bitmap bitmap(area.right, area.bottom, PixelFormat32bppPARGB);
    Graphics g(&bitmap);
    g.Clear(Canvas);
    g.ScaleTransform(REAL(area.right) / CardWidth, REAL(area.bottom) / CardHeight);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    Color accent = Accent();
    Rounded(g, accent, 20, 20, 14, 6, 3);
    Text(g, L"鼠标电量", L"Microsoft YaHei UI", 11, FontStyleRegular, Muted, 40, 12, 150, 22);
    Text(g, current.device.empty() ? L"罗技无线鼠标" : current.device, L"Microsoft YaHei UI", 19,
         FontStyleBold, Ink, 19, 34, 284, 30);
    DrawMouse(g, accent);

    //2. 百分比和档位分别排版；读数缺失时保留设备提示。
    if (current.percent >= 0)
    {
        REAL x = 18;
        if (current.estimated)
        {
            x += Text(g, L"约", L"Microsoft YaHei UI", 16, FontStyleRegular, Muted, x, 93, 24, 34) + 4;
        }
        x += Text(g, current.value, L"Segoe UI", 60, FontStyleBold, Ink, x, 53, 165, 88);
        Text(g, L"%", L"Segoe UI", 26, FontStyleRegular, accent, x + 3, 86, 44, 40);
    }
    else
    {
        std::wstring value = current.device.empty() ? L"—" : current.value;
        Text(g, value, L"Microsoft YaHei UI", 42, FontStyleBold, Ink, 19, 66, 190, 66);
    }
    Text(g, current.detail, L"Microsoft YaHei UI", 12, FontStyleRegular, Muted, 21, 130, 274, 23);

    //3. 精确读数填充刻度，档位与未知状态只显示文字。
    for (int i = 0; i < 20; ++i)
    {
        REAL x = 20.f + i * 14.35f;
        Rounded(g, Line, x, 159, 11, 6, 2);
        int fraction = std::clamp(current.percent - i * 5, 0, 5);
        if (fraction > 0)
        {
            REAL w = 11.f * fraction / 5;
            Rounded(g, accent, x, 159, w, 6, std::min(2.f, w / 2));
        }
    }

    Pen divider(Line);
    g.DrawLine(&divider, 20, 180, 304, 180);
    Text(g, L"每 30 秒自动刷新", L"Microsoft YaHei UI", 11, FontStyleRegular, Muted, 20, 185, 168, 23);
    if (checked.wYear)
    {
        wchar_t time[20]{};
        swprintf_s(time, L"%02u:%02u:%02u", checked.wHour, checked.wMinute, checked.wSecond);
        Text(g, time, L"Consolas", 12, FontStyleRegular, Muted, 245, 185, 63, 23);
    }

    Graphics output(dc);
    output.DrawImage(&bitmap, 0, 0, area.right, area.bottom);
}

NOTIFYICONDATAW TrayData(HWND window)
{
    NOTIFYICONDATAW data{ sizeof(data) };
    data.hWnd = window;
    data.uID = 1;
    return data;
}

void UpdateTray(HWND window)
{
    NOTIFYICONDATAW data = TrayData(window);
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = TrayMessage;
    data.hIcon = MakeIcon(std::max(16, GetSystemMetrics(SM_CXSMICON)));
    if (!data.hIcon)
    {
        return;
    }

    std::wstring tip = current.Summary();
    wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
    if (!registered || !Shell_NotifyIconW(NIM_MODIFY, &data))
    {
        registered = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
        if (registered)
        {
            data.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &data);
        }
    }
    if (icon)
    {
        DestroyIcon(icon);
    }
    icon = data.hIcon;
    SetWindowTextW(window, (L"鼠标电量 · " + tip).c_str());
}

void ShowCard(HWND window, POINT anchor)
{
    //1. 找到托盘图标所在显示器的可用范围。
    MONITORINFO info{ sizeof(info) };
    GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &info);
    RECT work = info.rcWork;
    HDC dc = GetDC(window);
    int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(window, dc);
    int w = MulDiv(CardWidth, dpi, 96), h = MulDiv(CardHeight, dpi, 96);
    int gap = MulDiv(10, dpi, 96);
    int x = std::max(int(work.left), std::min(int(anchor.x - w / 2), int(work.right) - w));
    int y = anchor.y - h - gap;
    if (y < work.top)
    {
        y = anchor.y + gap;
    }
    y = std::max(int(work.top), std::min(y, int(work.bottom) - h));

    //2. 卡片跟随任务栏位置，失去焦点时收起。
    int radius = MulDiv(20, dpi, 96);
    HRGN region = CreateRoundRectRgn(0, 0, w + 1, h + 1, radius, radius);
    if (region && !SetWindowRgn(window, region, FALSE))
    {
        DeleteObject(region);
    }
    SetWindowPos(window, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
    SetForegroundWindow(window);
}

unsigned __stdcall ReadWorker(void* context)
{
    pending = ReadMouseBattery();
    PostMessageW(static_cast<HWND>(context), BatteryReady, 0, 0);
    return 0;
}

void StartReading(HWND window)
{
    if (!worker)
    {
        worker = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, ReadWorker, window, 0, nullptr));
    }
}

void ShowMenu(HWND window, POINT anchor)
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, Details, L"查看电量");
    AppendMenuW(menu, MF_STRING, Refresh, L"立即刷新");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, Exit, L"退出");
    SetForegroundWindow(window);
    UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, anchor.x, anchor.y, 0, window, nullptr);
    DestroyMenu(menu);
    switch (choice)
    {
    case Details: ShowCard(window, anchor); break;
    case Refresh: StartReading(window); break;
    case Exit: DestroyWindow(window); break;
    }
}

LRESULT CALLBACK WindowProc(HWND window, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == taskbarCreated && taskbarCreated)
    {
        registered = false;
        UpdateTray(window);
        return 0;
    }

    switch (msg)
    {
    case WM_CREATE:
        UpdateTray(window);
        SetTimer(window, 1, 30000, nullptr);
        StartReading(window);
        return 0;

    case WM_TIMER:
        StartReading(window);
        return 0;

    case BatteryReady:
        WaitForSingleObject(worker, INFINITE);
        CloseHandle(worker);
        worker = nullptr;
        current = std::move(pending);
        GetLocalTime(&checked);
        UpdateTray(window);
        InvalidateRect(window, nullptr, FALSE);
        return 0;

    case TrayMessage:
    {
        POINT anchor{ GET_X_LPARAM(w), GET_Y_LPARAM(w) };
        UINT action = LOWORD(l);
        if (action == NIN_SELECT || action == NIN_KEYSELECT)
        {
            ShowCard(window, anchor);
        }
        else if (action == WM_CONTEXTMENU)
        {
            ShowMenu(window, anchor);
        }
        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        DrawCard(window, dc);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_ESCAPE)
        {
            ShowWindow(window, SW_HIDE);
        }
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE)
        {
            ShowWindow(window, SW_HIDE);
        }
        break;

    case WM_DESTROY:
    {
        KillTimer(window, 1);
        auto data = TrayData(window);
        Shell_NotifyIconW(NIM_DELETE, &data);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(window, msg, w, l);
}

int RunTray(HINSTANCE instance)
{
    SetProcessDPIAware();
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = L"LogitechMouseBatteryCard";
    if (!RegisterClassW(&wc) || !CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"鼠标电量",
        WS_POPUP, 0, 0, CardWidth, CardHeight, nullptr, nullptr, instance, nullptr))
    {
        return 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (worker)
    {
        WaitForSingleObject(worker, INFINITE);
        CloseHandle(worker);
    }
    if (icon)
    {
        DestroyIcon(icon);
    }
    return 0;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR args, int)
{
    //1. 命令行与托盘共用同一条设备读取路径。
    if (wcscmp(args, L"--read") == 0)
    {
        BatteryReading reading = ReadMouseBattery();
        std::wstring text = reading.Summary() + L"\r\n";
        HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0, written = 0;
        if (GetConsoleMode(output, &mode))
        {
            WriteConsoleW(output, text.data(), DWORD(text.size()), &written, nullptr);
        }
        else
        {
            int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
            std::string bytes(size, '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), bytes.data(), size, nullptr, nullptr);
            WriteFile(output, bytes.data(), DWORD(size), &written, nullptr);
        }
        return reading.device.empty() ? 1 : 0;
    }

    //2. 每个登录会话只创建一个托盘程序。
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\LogitechMouseBatteryTray");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (mutex)
        {
            CloseHandle(mutex);
        }
        return 0;
    }

    GdiplusStartupInput input;
    ULONG_PTR token = 0;
    int result = 1;
    if (GdiplusStartup(&token, &input, nullptr) == Ok)
    {
        result = RunTray(instance);
        GdiplusShutdown(token);
    }
    CloseHandle(mutex);
    return result;
}
