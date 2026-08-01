#include "OTAUpdateManager.h"

#include <Updater.h>

#include <cstdlib>
#include <cstring>
#include <strings.h>

#include <ESPLogger.h>

#include "FirmwareVersion.h"
#include "secrets.h"

static constexpr char TAG_OTA[] = "OTA";
static constexpr char OTA_HOST[] = "skraken.melo.se";
static constexpr uint16_t OTA_PORT = 443;
static constexpr char OTA_LATEST_PATH[] = "/fw/latest.txt";
static constexpr char OTA_FIRMWARE_PREFIX[] = "/fw/";
static constexpr size_t OTA_URL_BUFFER_SIZE = 96;
static constexpr size_t OTA_DOWNLOAD_BUFFER_SIZE = 1024;

void OTAUpdateManager::logHeap(const char* stage) const
{
    ESPLogger.info(TAG_OTA, "%s heap=%u frag=%u", stage, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getHeapFragmentation());
}

bool OTAUpdateManager::readLine(WiFiClientSecure& client, char* buffer, size_t bufferSize)
{
    size_t length = 0;
    unsigned long lastByteTime = millis();

    if (bufferSize == 0) {
        return false;
    }

    while (client.connected() || client.available()) {
        while (!client.available()) {
            if (!client.connected()) {
                break;
            }

            if (millis() - lastByteTime > Config::SSL_TIMEOUT) {
                buffer[0] = '\0';
                return false;
            }

            delay(1);
        }

        if (!client.available()) {
            break;
        }

        char ch = static_cast<char>(client.read());
        lastByteTime = millis();

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            break;
        }

        if (length + 1 < bufferSize) {
            buffer[length++] = ch;
        }
    }

    buffer[length] = '\0';
    return length > 0;
}

bool OTAUpdateManager::readBodyToken(WiFiClientSecure& client, char* buffer, size_t bufferSize)
{
    size_t length = 0;
    unsigned long lastByteTime = millis();

    if (bufferSize == 0) {
        return false;
    }

    while (client.connected() || client.available()) {
        while (!client.available()) {
            if (!client.connected()) {
                break;
            }

            if (millis() - lastByteTime > Config::SSL_TIMEOUT) {
                buffer[0] = '\0';
                return false;
            }

            delay(1);
        }

        if (!client.available()) {
            break;
        }

        char ch = static_cast<char>(client.read());
        lastByteTime = millis();

        if (ch == '\r' || ch == '\n') {
            if (length > 0) {
                break;
            }
            continue;
        }

        if (length + 1 < bufferSize) {
            buffer[length++] = ch;
        }
    }

    buffer[length] = '\0';
    return length > 0;
}

bool OTAUpdateManager::fetchLatestVersion(uint64_t& latestVersion)
{
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(Config::SSL_TIMEOUT);
    client.setBufferSizes(1024, 512);

    if (!client.connect(OTA_HOST, OTA_PORT)) {
        ESPLogger.error(TAG_OTA, "Failed to connect to version server");
        return false;
    }

    client.print(F("GET "));
    client.print(OTA_LATEST_PATH);
    client.print(F(" HTTP/1.1\r\nHost: "));
    client.print(OTA_HOST);
    client.print(F("\r\nUser-Agent: IVT-Heatpump-Controller\r\nConnection: close\r\n\r\n"));

    char line[64];
    if (!readLine(client, line, sizeof(line))) {
        ESPLogger.error(TAG_OTA, "Failed to read HTTP status line");
        return false;
    }

    if (strncmp(line, "HTTP/", 5) != 0 || strstr(line, " 200 ") == nullptr) {
        ESPLogger.error(TAG_OTA, "Unexpected HTTP response: %s", line);
        return false;
    }

    while (readLine(client, line, sizeof(line))) {
        if (line[0] == '\0') {
            break;
        }
    }

    char versionBuffer[16];
    if (!readBodyToken(client, versionBuffer, sizeof(versionBuffer))) {
        ESPLogger.error(TAG_OTA, "Version body missing or invalid");
        return false;
    }

    char* endPtr = nullptr;
    unsigned long long parsedVersion = strtoull(versionBuffer, &endPtr, 10);
    if (endPtr == versionBuffer || *endPtr != '\0' || parsedVersion == 0) {
        ESPLogger.error(TAG_OTA, "Invalid version response: %s", versionBuffer);
        return false;
    }

    latestVersion = static_cast<uint64_t>(parsedVersion);
    return true;
}

bool OTAUpdateManager::performUpdate(uint64_t latestVersion)
{
    char firmwareUrl[OTA_URL_BUFFER_SIZE];
    snprintf(firmwareUrl, sizeof(firmwareUrl), "https://%s%s%llu", OTA_HOST, OTA_FIRMWARE_PREFIX, static_cast<unsigned long long>(latestVersion));

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(Config::SSL_TIMEOUT);
    client.setBufferSizes(1024, 512);

    ESPLogger.info(TAG_OTA, "Downloading firmware from %s", firmwareUrl);

    if (!client.connect(OTA_HOST, OTA_PORT)) {
        ESPLogger.error(TAG_OTA, "Failed to connect to firmware server");
        return false;
    }

    client.print(F("GET "));
    client.print(OTA_FIRMWARE_PREFIX);
    client.print(static_cast<unsigned long long>(latestVersion));
    client.print(F(" HTTP/1.0\r\nHost: "));
    client.print(OTA_HOST);
    client.print(F("\r\nUser-Agent: IVT-Heatpump-Controller\r\nRange: bytes=0-\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n"));

    char line[128];
    if (!readLine(client, line, sizeof(line))) {
        ESPLogger.error(TAG_OTA, "Failed to read firmware response status line");
        return false;
    }

    if (strncmp(line, "HTTP/", 5) != 0 || (strstr(line, " 200 ") == nullptr && strstr(line, " 206 ") == nullptr)) {
        ESPLogger.error(TAG_OTA, "Unexpected firmware response: %s", line);
        return false;
    }

    uint64_t firmwareSize = 0;
    bool haveFirmwareSize = false;

    while (readLine(client, line, sizeof(line))) {
        if (line[0] == '\0') {
            break;
        }

        if (strncasecmp(line, "Content-Length:", 15) == 0) {
            char* value = line + 15;
            while (*value == ' ' || *value == '\t') {
                ++value;
            }

            char* endPtr = nullptr;
            unsigned long long parsedSize = strtoull(value, &endPtr, 10);
            if (endPtr != value && parsedSize > 0) {
                firmwareSize = static_cast<uint64_t>(parsedSize);
                haveFirmwareSize = true;
            }
        }

        if (strncasecmp(line, "Content-Range:", 14) == 0) {
            char* slash = strchr(line, '/');
            if (slash != nullptr && slash[1] != '\0') {
                char* endPtr = nullptr;
                unsigned long long parsedSize = strtoull(slash + 1, &endPtr, 10);
                if (endPtr != slash + 1 && parsedSize > 0) {
                    firmwareSize = static_cast<uint64_t>(parsedSize);
                    haveFirmwareSize = true;
                }
            }
        }
    }

    if (!haveFirmwareSize) {
        ESPLogger.error(TAG_OTA, "Firmware response did not report a usable size");
        return false;
    }

    if (firmwareSize > ESP.getFreeSketchSpace()) {
        ESPLogger.error(TAG_OTA, "Firmware is too large for free sketch space: %llu > %u", static_cast<unsigned long long>(firmwareSize), (unsigned)ESP.getFreeSketchSpace());
        return false;
    }

    if (!Update.begin(static_cast<size_t>(firmwareSize), U_FLASH)) {
        ESPLogger.error(TAG_OTA, "Update.begin failed: %s", Update.getErrorString().c_str());
        return false;
    }

    uint8_t buffer[OTA_DOWNLOAD_BUFFER_SIZE];
    uint64_t written = 0;
    unsigned long lastByteTime = millis();

    while (written < firmwareSize) {
        size_t available = client.available();
        if (available == 0) {
            if (!client.connected()) {
                break;
            }

            if (millis() - lastByteTime > Config::SSL_TIMEOUT) {
                ESPLogger.error(TAG_OTA, "Timed out while reading firmware body");
                Update.end();
                return false;
            }

            delay(1);
            continue;
        }

        size_t toRead = sizeof(buffer);
        size_t remaining = static_cast<size_t>(firmwareSize - written);
        if (toRead > remaining) {
            toRead = remaining;
        }
        if (toRead > available) {
            toRead = available;
        }

        int bytesRead = client.readBytes(reinterpret_cast<char*>(buffer), toRead);
        if (bytesRead <= 0) {
            ESPLogger.error(TAG_OTA, "Failed while reading firmware body");
            Update.end();
            return false;
        }

        lastByteTime = millis();

        size_t bytesWritten = Update.write(buffer, static_cast<size_t>(bytesRead));
        if (bytesWritten != static_cast<size_t>(bytesRead)) {
            ESPLogger.error(TAG_OTA, "Failed while writing firmware body: %u/%u", (unsigned)bytesWritten, (unsigned)bytesRead);
            Update.end();
            return false;
        }

        written += bytesWritten;
    }

    if (written != firmwareSize) {
        ESPLogger.error(TAG_OTA, "Firmware body ended early: %llu/%llu", static_cast<unsigned long long>(written), static_cast<unsigned long long>(firmwareSize));
        Update.end();
        return false;
    }

    if (!Update.end()) {
        ESPLogger.error(TAG_OTA, "Update.end failed: %s", Update.getErrorString().c_str());
        return false;
    }

    ESPLogger.info(TAG_OTA, "Firmware update stream completed");
    return true;
}

void OTAUpdateManager::check()
{
    uint64_t currentVersion = getFirmwareVersion();
    ESPLogger.info(TAG_OTA, "Current firmware: %llu", static_cast<unsigned long long>(currentVersion));
    logHeap("before_check");

    if (WiFi.status() != WL_CONNECTED) {
        ESPLogger.warn(TAG_OTA, "WiFi unavailable, skipping OTA check");
        return;
    }

    uint64_t latestVersion = 0;
    if (!fetchLatestVersion(latestVersion)) {
        logHeap("after_version_check_failed");
        return;
    }

    ESPLogger.info(TAG_OTA, "Latest firmware: %llu", static_cast<unsigned long long>(latestVersion));
    logHeap("after_version_check");

    if (latestVersion <= currentVersion) {
        ESPLogger.info(TAG_OTA, "No update needed");
        return;
    }

    ESPLogger.info(TAG_OTA, "Update available");
    if (!performUpdate(latestVersion)) {
        logHeap("after_update_failed");
        return;
    }

    logHeap("after_update_success");
    ESPLogger.info(TAG_OTA, "Update successful, restarting");
    ESPLogger.loop();
    delay(250);
    ESP.restart();
}

void checkOTAUpdate()
{
    OTAUpdateManager updater;
    updater.check();
}