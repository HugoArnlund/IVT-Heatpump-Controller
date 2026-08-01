#include "OTAUpdateManager.h"

#include <ESP8266httpUpdate.h>

#include <cstdlib>
#include <cstring>

#include <ESPLogger.h>

#include "FirmwareVersion.h"
#include "secrets.h"

static constexpr char TAG_OTA[] = "OTA";
static constexpr char OTA_HOST[] = "skraken.melo.se";
static constexpr uint16_t OTA_PORT = 443;
static constexpr char OTA_LATEST_PATH[] = "/fw/latest.txt";
static constexpr char OTA_FIRMWARE_PREFIX[] = "/fw/";
static constexpr size_t OTA_URL_BUFFER_SIZE = 96;

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

    ESPhttpUpdate.rebootOnUpdate(false);

    ESPLogger.info(TAG_OTA, "Downloading firmware from %s", firmwareUrl);
    auto updateResult = ESPhttpUpdate.update(client, firmwareUrl);

    if (updateResult == HTTP_UPDATE_OK) {
        ESPLogger.info(TAG_OTA, "Firmware update stream completed");
        return true;
    }

    if (updateResult == HTTP_UPDATE_NO_UPDATES) {
        ESPLogger.warn(TAG_OTA, "Update server reported no update");
        return false;
    }

    ESPLogger.error(TAG_OTA, "Update failed with code %d", ESPhttpUpdate.getLastError());
    return false;
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