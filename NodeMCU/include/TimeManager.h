#pragma once
#include <Arduino.h>

namespace TimeManager {
    void initialize();
    unsigned long long getCurrentUnixTime();
}
