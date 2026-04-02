#pragma once
#include <Arduino.h>

namespace WiFiManager {
    void initialize();
    bool isConnected();
    String getLocalIP();
}
