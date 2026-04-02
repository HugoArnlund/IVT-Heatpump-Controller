// =============== Non-sensitive Configuration Section ===============
#pragma once

namespace Config {
    // NTP Configuration
    inline const char* NTP_SERVER = "pool.ntp.org";
    inline const long TIMEZONE_OFFSET = 0; // UTC
    inline const unsigned long NTP_UPDATE_INTERVAL = 30 * 60000; // 30 minutes

    // Sensor Configuration
    inline const unsigned long SENSOR_READ_INTERVAL = 30 * 60 * 1000UL; // 30 minutes
    inline const uint8_t SENSOR_PIN = 4;
    inline const unsigned long SENSOR_INIT_DELAY = 3000; // 3 seconds

    // Network Settings
    inline const unsigned long WIFI_CONNECT_TIMEOUT = 30000; // 30 seconds
    inline const unsigned long WIFI_RETRY_DELAY = 300;

    // Firebase Settings
    inline const unsigned long SSL_TIMEOUT = 20000;
    inline const size_t SSL_RX_BUFFER_SIZE = 4096;
    inline const size_t SSL_TX_BUFFER_SIZE = 1024;
}
