#include "Logger.h"
#include "WiFiManager.h"
#include <ESP8266WiFi.h>
#include "Config.h"
#include "Secrets.h"

namespace WiFiManager {

void initialize() {
    Logger::log(Logger::LOG_INFO, "Connecting to WiFi", "WiFiManager");
    WiFi.begin(Secrets::WIFI_SSID, Secrets::WIFI_PASSWORD);
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < Config::WIFI_CONNECT_TIMEOUT) {
        delay(Config::WIFI_RETRY_DELAY);
    }
    if (WiFi.status() != WL_CONNECTED) {
        Logger::log(Logger::LOG_ERROR, "Failed to connect to WiFi, restarting...", "WiFiManager");
        ESP.restart();
    }
    Logger::log(Logger::LOG_INFO, "WiFi connected", "WiFiManager");
    String ipMsg = String("IP Address: ") + WiFi.localIP().toString();
    Logger::log(Logger::LOG_INFO, ipMsg, "WiFiManager");
}

bool isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

String getLocalIP() {
    return WiFi.localIP().toString();
}

} // namespace WiFiManager
