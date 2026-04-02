#include "Logger.h"
#include "SensorManager.h"
#include <AM2302-Sensor.h>
#include "Config.h"

namespace SensorManager {

namespace {
    AM2302::AM2302_Sensor temperatureSensor(Config::SENSOR_PIN);
}

void initialize() {
    if (!temperatureSensor.begin()) {
        Logger::log(Logger::LOG_ERROR, "Failed to initialize temperature sensor", "SensorManager");
        return;
    }
    delay(Config::SENSOR_INIT_DELAY); // Initial stabilization delay
    Logger::log(Logger::LOG_INFO, "Temperature sensor initialized", "SensorManager");
}

bool read(float& temperature, float& humidity) {
    auto status = temperatureSensor.read();
    if (status != AM2302::AM2302_READ_OK) {
        Logger::log(Logger::LOG_WARNING, "Error reading sensor data", "SensorManager");
        return false;
    }
    temperature = temperatureSensor.get_Temperature();
    humidity = temperatureSensor.get_Humidity();
    char buf[64];
    snprintf(buf, sizeof(buf), "Sensor read: temp=%.2f, hum=%.2f", temperature, humidity);
    Logger::log(Logger::LOG_DEBUG, buf, "SensorManager");
    return true;
}

} // namespace SensorManager
