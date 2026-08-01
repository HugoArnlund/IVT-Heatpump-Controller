#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>

class OTAUpdateManager {
public:
    void check();

private:
    bool fetchLatestVersion(uint64_t& latestVersion);
    bool performUpdate(uint64_t latestVersion);
    bool readLine(WiFiClientSecure& client, char* buffer, size_t bufferSize);
    bool readBodyToken(WiFiClientSecure& client, char* buffer, size_t bufferSize);
    void logHeap(const char* stage) const;
};

void checkOTAUpdate();