#pragma once
#include <Arduino.h>

namespace OTAUpdateManager {
    void initialize();
    void performOTAUpdate();
    int fetchServerFirmwareVersion();
}
