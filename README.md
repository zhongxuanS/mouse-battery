# 罗技鼠标电量

简体中文 · [English](README.en.md)

在 Windows 任务栏的通知区域查看罗技无线鼠标的剩余电量。程序每 30 秒读取一次，支持 Windows x64，免安装，也无需运行 Logitech G HUB。

## 开始使用

1. 接好鼠标的 USB 接收器，下载并双击运行 [MouseBattery.exe](https://github.com/zhongxuanS/mouse-battery/raw/refs/heads/main/MouseBattery.exe)。
2. 在任务栏时钟旁找到电量图标。如果看不到，展开隐藏的托盘图标。
3. 单击图标可查看鼠标型号、电量和上次检查时间。右键单击可选择“立即刷新”或“退出”。

罗技 G304 X LIGHTSPEED 已通过 USB 接收器实测。其他型号能否读取，以及能读到哪种电量信息，取决于鼠标提供的接口。支持百分比的鼠标直接显示数字，只上报档位的鼠标显示“高”“中”“低”或“极低”。如果根据电压估算百分比，详情卡片会在数字前加“约”。

如果托盘显示 `?`，先移动鼠标将它唤醒，再右键选择“立即刷新”。

## 在命令行读取一次

在 EXE 所在目录打开 PowerShell，运行以下命令即可输出一行电量信息并退出。

```powershell
.\MouseBattery.exe --read | Out-Host
```

读取成功时，`$LASTEXITCODE` 为 `0`。未读到电量时，程序输出原因并返回 `1`。

## 从源码构建

安装 Visual Studio 或 Visual Studio Build Tools，选择“使用 C++ 的桌面开发”，包含 MSVC x64 工具链和 Windows SDK。在仓库根目录打开 PowerShell，运行以下命令。生成的 `MouseBattery.exe` 位于仓库根目录。

```powershell
$buildDir = Join-Path $env:TEMP 'mouse-battery-build'
.\build.ps1 -BuildDir $buildDir
```

使用同一个 PowerShell 窗口构建并运行电量解码和卡片绘制测试。

```powershell
.\build.ps1 -BuildDir $buildDir -Test
```
