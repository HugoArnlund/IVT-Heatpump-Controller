#include "Logger.h"
#include "OTAUpdateManager.h"
#include <ESP8266httpUpdate.h>
#include <ESP8266HTTPClient.h>
#include "Config.h"

namespace OTAUpdateManager {

#include "FirmwareVersion.h"

int fetchServerFirmwareVersion() {
    HTTPClient http;
    WiFiClient client;
    int serverVersion = -1;
    if (http.begin(client, "http://skarken.melo.se/fw/version.txt")) {
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            serverVersion = payload.toInt();
            char buf[64];
            snprintf(buf, sizeof(buf), "Server firmware version: %d", serverVersion);
            Logger::log(Logger::LOG_INFO, buf, "OTAUpdateManager");
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "Failed to fetch version.txt, HTTP code: %d", httpCode);
            Logger::log(Logger::LOG_WARNING, buf, "OTAUpdateManager");
        }
        http.end();
    } else {
        Logger::log(Logger::LOG_ERROR, "Unable to connect to version.txt", "OTAUpdateManager");
    }
    return serverVersion;
}

void performOTAUpdate() {
    char buf[64];
    snprintf(buf, sizeof(buf), "Current firmware version: %d", FIRMWARE_VERSION);
    Logger::log(Logger::LOG_INFO, buf, "OTAUpdateManager");
    int serverVersion = fetchServerFirmwareVersion();
    if (serverVersion <= 0) {
        Logger::log(Logger::LOG_ERROR, "Invalid server version. Aborting update.", "OTAUpdateManager");
        return;
    }
    if (serverVersion > FIRMWARE_VERSION) {
        Logger::log(Logger::LOG_INFO, "Newer firmware available. Starting OTA update...", "OTAUpdateManager");
        WiFiClient client;
        t_httpUpdate_return ret = ESPhttpUpdate.update(client, "http://skarken.melo.se/fw/latest.bin");
        switch (ret) {
            case HTTP_UPDATE_FAILED: {
                char errbuf[128];
                snprintf(errbuf, sizeof(errbuf), "Update failed. Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
                Logger::log(Logger::LOG_ERROR, errbuf, "OTAUpdateManager");
                break;
            }
            case HTTP_UPDATE_NO_UPDATES:
                Logger::log(Logger::LOG_INFO, "No updates available.", "OTAUpdateManager");
                break;
            case HTTP_UPDATE_OK:
                Logger::log(Logger::LOG_CRITICAL, "Update successful. Rebooting...", "OTAUpdateManager");
                delay(1000);
                ESP.restart();
                break;
        }
    } else {
        Logger::log(Logger::LOG_INFO, "Firmware is up to date. No update needed.", "OTAUpdateManager");
    }
}

void initialize() {
    // Reserved for future use (e.g., scheduled OTA checks)
}

} // namespace OTAUpdateManager
