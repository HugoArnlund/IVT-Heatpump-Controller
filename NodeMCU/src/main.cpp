#include <Arduino.h>
#include <ArduinoJson.h>

#include "WiFiManager.h"
#include "OTAUpdateManager.h"
#include "FirebaseManager.h"
#include "TimeManager.h"
#include "SensorManager.h"
#include "Logger.h"
#include "secrets.h"


// =============== Initialization Functions ===============
void setup() {
    Serial.begin(115200);
    while (!Serial) {}

    Logger::initialize();
    Logger::log(Logger::LOG_INFO, "Logger initialized", "System");

    WiFiManager::initialize();
    OTAUpdateManager::performOTAUpdate();
    TimeManager::initialize();
    SensorManager::initialize();
    FirebaseManager::initialize();

    Logger::log(Logger::LOG_CRITICAL, "System initialization complete", "System");
}

void handleSensorReadings() {
    static unsigned long lastReadingTime = 0;
    unsigned long currentTime = millis();

    if (currentTime - lastReadingTime >= Config::SENSOR_READ_INTERVAL || lastReadingTime == 0) {
        lastReadingTime = currentTime;

        float temperature, humidity;
        if (!SensorManager::read(temperature, humidity)) {
            Logger::log(Logger::LOG_ERROR, "Failed to read sensor data", "SensorManager");
            return;
        }
        unsigned long long timestamp = TimeManager::getCurrentUnixTime();
        FirebaseManager::sendSensorData(timestamp, temperature, humidity);
        char msg[64];
        snprintf(msg, sizeof(msg), "Sent sensor data - Temp: %.1f°C, Hum: %.1f%%", temperature, humidity);
        Logger::log(Logger::LOG_INFO, msg, "SensorManager");
    }
}

void loop() {
    FirebaseManager::loop();
    if (FirebaseManager::ready()) {
        handleSensorReadings();
    }
    delay(100);
}