#include "unistd.h"

// =============== Configuration Section ===============
namespace Config {
    // WiFi Configuration
    const char* WIFI_SSID = "";
    const char* WIFI_PASSWORD = "";

    // Firebase Configuration
    const char* WEB_API_KEY = "";
    const char* DATABASE_URL = "";
    const char* USER_EMAIL = "";
    const char* USER_PASSWORD = "";
    const char* DISCORD_WEBHOOK_URL = "";

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

    // TLS buffer sizes tuned to reduce heap pressure on ESP8266.
    // Firebase needs more headroom than Discord webhook posts.
    inline const unsigned long SSL_TIMEOUT = 20000;
    inline const size_t DISCORD_SSL_RX_BUFFER_SIZE = 1024;
    inline const size_t DISCORD_SSL_TX_BUFFER_SIZE = 512;
    inline const size_t FIREBASE_SSL_RX_BUFFER_SIZE = 2048;
    inline const size_t FIREBASE_SSL_TX_BUFFER_SIZE = 512;
}