# Logitech Mouse Battery

[简体中文](README.md) · English

See your Logitech wireless mouse’s battery level in the Windows notification area. This portable x64 app checks the battery every 30 seconds. It runs without an installer or Logitech G HUB. The app’s menus and status messages are in Simplified Chinese.

## Get started

1. Plug in your mouse’s USB receiver, then download and run [MouseBattery.exe](https://github.com/zhongxuanS/mouse-battery/raw/refs/heads/main/MouseBattery.exe).
2. Look for the battery indicator next to the taskbar clock. If it’s hidden, open the tray overflow.
3. Click the indicator to see the mouse model, battery level, and time of the last check. Right-click for **立即刷新** (Refresh now) or **退出** (Exit).

The Logitech G304 X LIGHTSPEED has been tested with its USB receiver. Support for other models depends on the battery interface the mouse exposes. The app shows a percentage when the mouse reports one. For mice that report only a level, it shows 高 (high), 中 (medium), 低 (low), or 极低 (critical). A percentage prefixed with 约 (about) on the details card is an estimate based on battery voltage.

If the tray shows `?`, move the mouse to wake it, then right-click the indicator and select **立即刷新**.

## Read the battery from PowerShell

Open PowerShell in the folder containing the executable. The following command prints one reading and exits.

```powershell
.\MouseBattery.exe --read | Out-Host
```

Check `$LASTEXITCODE` for the result. A successful reading returns `0`. If the battery is unavailable, the app prints a status message and returns `1`.

## Build from source

Install Visual Studio or Visual Studio Build Tools with the **Desktop development with C++** workload, including the MSVC x64 tools and Windows SDK. Run the following in PowerShell from the repository root. The build writes `MouseBattery.exe` to that directory.

```powershell
$buildDir = Join-Path $env:TEMP 'mouse-battery-build'
.\build.ps1 -BuildDir $buildDir
```

In the same PowerShell session, build and run the battery decoding and card rendering tests.

```powershell
.\build.ps1 -BuildDir $buildDir -Test
```
