param(
    [string]$BuildDir = 'E:\开发文档\mouse-battery\原生托盘界面\临时文件',
    [string]$OutputPath = (Join-Path $PSScriptRoot 'MouseBattery.exe'),
    [switch]$Test
)
$ErrorActionPreference = 'Stop'

# 1. 找到已安装的 x64 C++ 工具链。
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$install = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$install)
{
    throw '未找到 Visual C++ x64 编译工具'
}

# 2. 对象文件留在构建目录，发布时只需复制 EXE。
$devCmd = Join-Path $install 'Common7\Tools\VsDevCmd.bat'
$sources = '"{0}\Battery.cpp" "{0}\Tray.cpp"' -f $PSScriptRoot
$libraries = 'user32.lib gdi32.lib shell32.lib gdiplus.lib hid.lib cfgmgr32.lib'
$subsystem = 'windows'
$resourceCommand = 'rc /nologo /i "{0}" /fo MouseBattery.res "{0}\MouseBattery.rc" && ' -f $PSScriptRoot
if ($Test)
{
    $sources = '"{0}\BatteryTests.cpp"' -f $PSScriptRoot
    $OutputPath = Join-Path $BuildDir 'BatteryTests.exe'
    $subsystem = 'console'
    $resourceCommand = ''
}
else
{
    $libraries += ' MouseBattery.res'
}

$target = [IO.Path]::GetFullPath($OutputPath)
$null = New-Item -Path $BuildDir -ItemType Directory -Force
Push-Location -LiteralPath $BuildDir
try
{
    $command = 'call "{0}" -no_logo -arch=x64 >nul && {5}cl /nologo /std:c++17 /utf-8 /MT /O1 /W4 /EHsc /Gy /DNOMINMAX /DUNICODE /D_UNICODE {1} /Fe:"{2}" /link /INCREMENTAL:NO /OPT:REF /OPT:ICF /SUBSYSTEM:{3} {4}' -f $devCmd, $sources, $target, $subsystem, $libraries, $resourceCommand
    & cmd.exe /d /c $command
    if ($LASTEXITCODE -ne 0)
    {
        throw "编译失败 ($LASTEXITCODE)"
    }
    if ($Test)
    {
        & $target
        if ($LASTEXITCODE -ne 0)
        {
            throw "测试失败 ($LASTEXITCODE)"
        }
    }
}
finally
{
    Pop-Location
}
