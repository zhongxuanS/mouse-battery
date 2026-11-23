#pragma once

#include <string>

struct BatteryReading
{
    std::wstring device;
    std::wstring value = L"?";
    std::wstring detail = L"正在读取鼠标电量";
    int percent = -1;
    bool estimated = false;
    bool charging = false;

    bool Low() const;
    std::wstring Summary() const;
};

BatteryReading ReadMouseBattery();
