#pragma once
#include <Arduino.h>

namespace SensorManager {
    void initialize();
    bool read(float& temperature, float& humidity);
}
