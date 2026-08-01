#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <ESP.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <ESPLogger.h>
#include <math.h>
#include <ctype.h>
#include "FirmwareVersion.h"
#include "IRController.h"
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <AM2302-Sensor.h>

#include "secrets.h"

// This firmware acts as a lightweight controller for the heat pump.

// =============== Global Objects ===============
// Hardware and networking objects used throughout the application.
IRController irController;
AM2302::AM2302_Sensor temperatureSensor(Config::SENSOR_PIN);
SerialSink serialSink;

static constexpr char TAG_MAIN[] = "Main";
static constexpr char TAG_WIFI[] = "WiFi";
static constexpr char TAG_SENSOR[] = "Sensor";
static constexpr char TAG_FIREBASE[] = "Firebase";
static constexpr char TAG_HEAP[] = "Heap";
static constexpr char TAG_FIREBASE_SEND[] = "FirebaseSend";

static constexpr char TASK_UID_ACK[] = "acknowledgmentTask";
static constexpr char TASK_UID_PUSH_TEMP[] = "pushTempTask";
static constexpr char TASK_UID_PUSH_HUM[] = "pushHumTask";
static constexpr size_t FIREBASE_STREAM_SSL_RX_BUFFER_SIZE = 1024;
static constexpr size_t FIREBASE_SEND_SSL_RX_BUFFER_SIZE = 512;

static constexpr size_t MIN_FREE_HEAP_BYTES = 6 * 1024;
static constexpr unsigned long MAX_UPTIME_BEFORE_RESTART_MS = 48UL * 60UL * 60UL * 1000UL;
static constexpr uint8_t MIN_SUPPORTED_TEMPERATURE = 16;
static constexpr uint8_t MAX_SUPPORTED_TEMPERATURE = 30;
static constexpr uint8_t MAX_FAN_SPEED = 3;
static constexpr unsigned long WIFI_STATUS_LOG_INTERVAL_MS = 30000UL;
static constexpr unsigned long LOOP_TRACE_INTERVAL_MS = 60000UL;
static constexpr unsigned long HEAP_INFO_LOG_INTERVAL_MS = 30UL * 60UL * 1000UL;
static constexpr unsigned long HEARTBEAT_INTERVAL_MS = 30000UL;

// Time synchronization keeps sensor timestamps and logs aligned with the network clock.
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, Config::NTP_SERVER, Config::TIMEZONE_OFFSET, Config::NTP_UPDATE_INTERVAL);

// Firebase authentication is required before the device can read or write database values.
UserAuth userAuth(Config::WEB_API_KEY, Config::USER_EMAIL, Config::USER_PASSWORD);

// FirebaseApp owns the async task lifecycle for the realtime database clients.
FirebaseApp firebaseApp;

// Keep the stream client separate from outbound writes so stream events are
// not delayed behind acknowledgments or sensor uploads.
WiFiClientSecure sslClientStream;
using AsyncClient = AsyncClientClass;
AsyncClient asyncClientStream(sslClientStream);
RealtimeDatabase databaseStream;

WiFiClientSecure sslClientSend;
AsyncClient asyncClientSend(sslClientSend);
RealtimeDatabase databaseSend;

// =============== Function Declarations ===============
void initializeWiFi();
void initializeFirebase();
void initializeTimeClient();
void initializeSensor();
void processFirebaseData(AsyncResult& result);
void handleSensorReadings();
unsigned long long getCurrentUnixTime();
String escapeJsonString(const char* value);
bool publishCommandAcknowledgement(int commandId, const char* status, const char* message, const char* error = nullptr, int power = 0, int tenMode = 0, int fan = 0, float temp = 0.0f);
void logHeapUsage(const char* tag, LogLevel level = LogLevel::DEBUG);
void logFirebaseWriteResult(AsyncResult& result);
void publishHeartbeat();
void checkSystemHealth();

// =============== Initialization and Runtime Lifecycle ===============
static unsigned long lastWifiStatusLogTime = 0;
static unsigned long lastFirebaseErrorLogTime = 0;
static unsigned long lastLoopTraceTime = 0;
static unsigned long lastHeartbeatSendTime = 0;

// The setup function is the boot sequence for the controller.
void setup() {
    Serial.begin(115200);
    while (!Serial) {} // Wait for serial port to connect (for debugging)

    ESPLogger.trace(TAG_MAIN, "Boot sequence started, reset reason: %s", ESP.getResetReason().c_str());

    ESPLogger.clearSinks();
    ESPLogger.addSink(&serialSink, LogLevel::TRACE);

    ESPLogger.info(TAG_MAIN, "Logger initialized");
    ESPLogger.info(TAG_MAIN, "Firmware version: %llu", static_cast<unsigned long long>(getFirmwareVersion()));

    initializeWiFi();
    initializeTimeClient();
    initializeSensor();
    initializeFirebase();

    ESPLogger.warn(TAG_MAIN, "System initialization complete");
    logHeapUsage("setup:complete", LogLevel::DEBUG);
}

// Connect to the configured Wi-Fi network before any other services are used.
void initializeWiFi() {
    ESPLogger.info(TAG_WIFI, "Connecting to WiFi");
    WiFi.begin(Config::WIFI_SSID, Config::WIFI_PASSWORD);

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && 
          millis() - startTime < Config::WIFI_CONNECT_TIMEOUT) {
        const unsigned long now = millis();
        if (now - lastWifiStatusLogTime >= WIFI_STATUS_LOG_INTERVAL_MS || lastWifiStatusLogTime == 0) {
            lastWifiStatusLogTime = now;
            ESPLogger.trace(TAG_WIFI, "Waiting for WiFi connection (%lu ms elapsed)", now - startTime);
        }
        Serial.print(".");
        delay(Config::WIFI_RETRY_DELAY);
    }

    if (WiFi.status() != WL_CONNECTED) {
        ESPLogger.error(TAG_WIFI, "Wi-Fi connection timed out; restarting in 3 seconds");
        ESPLogger.fatal(TAG_WIFI, "Unrecoverable startup condition: Wi-Fi unavailable");
        ESPLogger.loop();
        delay(3000);
        ESP.restart();
    }

    ESPLogger.info(TAG_WIFI, "WiFi connected");
    IPAddress ipAddress = WiFi.localIP();
    ESPLogger.debug(TAG_WIFI, "IP Address: %u.%u.%u.%u", ipAddress[0], ipAddress[1], ipAddress[2], ipAddress[3]);
    ESPLogger.debug(TAG_WIFI, "Signal strength: %d dBm", WiFi.RSSI());
}

// Configure the Firebase clients and subscribe to the realtime database stream.
void initializeFirebase() {
    ESPLogger.trace(TAG_FIREBASE, "Configuring Firebase clients");

    // Keep independent clients for inbound stream traffic and outbound writes.
    sslClientStream.setInsecure();
    sslClientStream.setTimeout(Config::SSL_TIMEOUT);
    sslClientStream.setBufferSizes(FIREBASE_STREAM_SSL_RX_BUFFER_SIZE, Config::FIREBASE_SSL_TX_BUFFER_SIZE);

    // Ack/write responses are tiny, so the send client can use the minimum
    // safe receive buffer to preserve heap.
    sslClientSend.setInsecure();
    sslClientSend.setTimeout(Config::SSL_TIMEOUT);
    sslClientSend.setBufferSizes(FIREBASE_SEND_SSL_RX_BUFFER_SIZE, Config::FIREBASE_SSL_TX_BUFFER_SIZE);

    // Initialize Firebase authentication
    initializeApp(asyncClientStream, firebaseApp, getAuth(userAuth), [](AsyncResult&){}, "authTask");
    
    // Get database references
    firebaseApp.getApp<RealtimeDatabase>(databaseStream);
    firebaseApp.getApp<RealtimeDatabase>(databaseSend);
    
    databaseStream.url(Config::DATABASE_URL);
    databaseSend.url(Config::DATABASE_URL);

    // Set filters for SSE (only listen to put/patch events)
    asyncClientStream.setSSEFilters("put, patch");

    // Start listening to heatpump commands
    databaseStream.get(asyncClientStream, "/heatpump/", processFirebaseData, true, "RTDB_Listen");

    // Wait for firebaseApp to be ready before proceeding
    unsigned long startTime = millis();
    while (!firebaseApp.ready() && millis() - startTime < Config::FIREBASE_INIT_TIMEOUT) {
        delay(100);
    }

    if (!firebaseApp.ready()) {
        ESPLogger.error(TAG_FIREBASE, "Firebase initialization timed out; restarting in 3 seconds");
        ESPLogger.fatal(TAG_FIREBASE, "Unrecoverable startup condition: Firebase not ready");
        ESPLogger.loop();
        delay(3000);
        ESP.restart();
    }

    ESPLogger.info(TAG_FIREBASE, "Firebase initialized and listening for commands");
}

// Start the NTP client so the firmware can stamp sensor data with a real clock.
void initializeTimeClient() {
    timeClient.begin();
    ESPLogger.trace(TAG_MAIN, "NTP config server=%s offset=%ld interval=%lu",
                    Config::NTP_SERVER,
                    static_cast<long>(Config::TIMEZONE_OFFSET),
                    static_cast<unsigned long>(Config::NTP_UPDATE_INTERVAL));
    ESPLogger.info(TAG_MAIN, "NTP client initialized");
}

// Initialize the AM2302 temperature and humidity sensor.
void initializeSensor() {
    ESPLogger.trace(TAG_SENSOR, "Initializing AM2302 on pin %u", static_cast<unsigned>(Config::SENSOR_PIN));
    if (!temperatureSensor.begin()) {
        ESPLogger.error(TAG_SENSOR, "Failed to initialize temperature sensor");
        return;
    }
    delay(Config::SENSOR_INIT_DELAY); // Initial stabilization delay
    ESPLogger.info(TAG_SENSOR, "Temperature sensor initialized");
}

// =============== Data Processing Functions ===============
static const char* skipWhitespace(const char* input) {
    while (*input != '\0' && isspace(static_cast<unsigned char>(*input))) {
        ++input;
    }
    return input;
}

static bool parseJsonIntField(const char* jsonStr, const char* key, int& value) {
    char pattern[32];
    const size_t keyLength = strlen(key);
    if (keyLength + 3 >= sizeof(pattern)) {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* entry = strstr(jsonStr, pattern);
    if (entry == nullptr) {
        return false;
    }

    entry = strchr(entry, ':');
    if (entry == nullptr) {
        return false;
    }

    entry = skipWhitespace(entry + 1);

    if (*entry == '"') {
        ++entry;
        char buffer[32];
        size_t length = 0;
        while (length < sizeof(buffer) - 1 && entry[length] != '\0' && entry[length] != '"') {
            buffer[length] = entry[length];
            ++length;
        }
        buffer[length] = '\0';

        char* end = nullptr;
        const long parsed = strtol(buffer, &end, 10);
        if (end == buffer) {
            return false;
        }

        value = static_cast<int>(parsed);
        return true;
    }

    char* end = nullptr;
    const long parsed = strtol(entry, &end, 10);
    if (end == entry) {
        return false;
    }

    value = static_cast<int>(parsed);
    return true;
}

static bool parseJsonFloatField(const char* jsonStr, const char* key, float& value) {
    char pattern[32];
    const size_t keyLength = strlen(key);
    if (keyLength + 3 >= sizeof(pattern)) {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* entry = strstr(jsonStr, pattern);
    if (entry == nullptr) {
        return false;
    }

    entry = strchr(entry, ':');
    if (entry == nullptr) {
        return false;
    }

    entry = skipWhitespace(entry + 1);

    if (*entry == '"') {
        ++entry;
        char buffer[32];
        size_t length = 0;
        while (length < sizeof(buffer) - 1 && entry[length] != '\0' && entry[length] != '"') {
            buffer[length] = entry[length];
            ++length;
        }
        buffer[length] = '\0';

        char* end = nullptr;
        const float parsed = strtof(buffer, &end);
        if (end == buffer) {
            return false;
        }

        value = parsed;
        return true;
    }

    char* end = nullptr;
    const float parsed = strtof(entry, &end);
    if (end == entry) {
        return false;
    }

    value = parsed;
    return true;
}

static bool parseJsonBoolOrIntField(const char* jsonStr, const char* key, int& value) {
    char pattern[32];
    const size_t keyLength = strlen(key);
    if (keyLength + 3 >= sizeof(pattern)) {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* entry = strstr(jsonStr, pattern);
    if (entry == nullptr) {
        return false;
    }

    entry = strchr(entry, ':');
    if (entry == nullptr) {
        return false;
    }

    entry = skipWhitespace(entry + 1);
    if (*entry == '"') {
        ++entry;
        if (strncmp(entry, "true", 4) == 0) {
            value = 1;
            return true;
        }
        if (strncmp(entry, "false", 5) == 0) {
            value = 0;
            return true;
        }
    }

    if (strncmp(entry, "true", 4) == 0) {
        value = 1;
        return true;
    }
    if (strncmp(entry, "false", 5) == 0) {
        value = 0;
        return true;
    }

    char* end = nullptr;
    const long parsed = strtol(entry, &end, 10);
    if (end == entry) {
        return false;
    }

    value = static_cast<int>(parsed);
    return true;
}

static bool parseHeatpumpCommandPayload(const char* jsonStr, int& powerValue, int& tenModeValue, int& fanValue, float& tempValue, int& idValue) {
    return parseJsonBoolOrIntField(jsonStr, "power", powerValue) &&
           parseJsonBoolOrIntField(jsonStr, "tenDegreeMode", tenModeValue) &&
           parseJsonIntField(jsonStr, "fan", fanValue) &&
           parseJsonFloatField(jsonStr, "temp", tempValue) &&
           parseJsonIntField(jsonStr, "id", idValue);
}

String escapeJsonString(const char* value) {
    String escaped;
    if (value == nullptr) {
        return escaped;
    }

    escaped.reserve(strlen(value) + 8);
    for (size_t i = 0; value[i] != '\0'; ++i) {
        switch (value[i]) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += value[i]; break;
        }
    }

    return escaped;
}

bool publishCommandAcknowledgement(int commandId, const char* status, const char* message, const char* error, int power, int tenMode, int fan, float temp) {
    if (WiFi.status() != WL_CONNECTED || !firebaseApp.ready()) {
        ESPLogger.warn(TAG_FIREBASE, "Skipping command acknowledgement because Wi-Fi or Firebase is not ready");
        return false;
    }

    String payload;
    payload.reserve(220);
    payload = String("{\"status\":\"") + status +
              "\",\"id\":" + String(commandId) +
              ",\"message\":\"" + escapeJsonString(message) +
              "\",\"timestamp\":" + String(getCurrentUnixTime()) +
              ",\"power\":" + String(power) +
              ",\"tenDegreeMode\":" + String(tenMode) +
              ",\"fan\":" + String(fan) +
              ",\"temp\":" + String(temp, 1);

    if (error != nullptr && error[0] != '\0') {
        payload += ",\"error\":\"" + escapeJsonString(error) + "\"";
    }

    payload += "}";

    object_t ackJson(payload.c_str());
    databaseSend.set<object_t>(asyncClientSend, "/response", ackJson, logFirebaseWriteResult, TASK_UID_ACK);
    ESPLogger.debug(TAG_FIREBASE, "Published acknowledgement status=%s id=%d", status, commandId);
    return true;
}

// Firebase stream callbacks arrive here when a command is pushed into the database.
void processFirebaseData(AsyncResult& result) {
    if (!result.isResult()) {
        ESPLogger.trace(TAG_FIREBASE, "Ignoring callback that is not a result payload");
        return;
    }

    logHeapUsage("processFirebaseData:enter");

    if (result.isError()) {
        const unsigned long now = millis();
        if (now - lastFirebaseErrorLogTime >= WIFI_STATUS_LOG_INTERVAL_MS || lastFirebaseErrorLogTime == 0) {
            lastFirebaseErrorLogTime = now;
            ESPLogger.error(TAG_FIREBASE, "Error in task %s: %s (code %d)",
                            result.uid().c_str(),
                            result.error().message().c_str(),
                            result.error().code());
        }
        return;
    }

    if (!result.available()) {
        ESPLogger.trace(TAG_FIREBASE, "Result callback had no available payload yet");
        return;
    }

    RealtimeDatabaseResult& rtdb = result.to<RealtimeDatabaseResult>();

    if (rtdb.isStream()) {
        const String dataPath = rtdb.dataPath();
        const char* dataPathStr = dataPath.c_str();
        const char* jsonStr = rtdb.to<const char*>();

        ESPLogger.debug(TAG_FIREBASE, "[Event] Path: %s, Data: %s", dataPathStr, jsonStr);

        // Skip empty or heartbeat messages
        if (strlen(jsonStr) == 0 || strcmp(jsonStr, "null") == 0) {
            ESPLogger.debug(TAG_FIREBASE, "Ignoring empty/null data");
            return;
        }

        if (strcmp(dataPathStr, "/heatpump/data") != 0 && strcmp(dataPathStr, "/data") != 0 && strcmp(dataPathStr, "/restart") != 0) {
            ESPLogger.debug(TAG_FIREBASE, "Ignoring event for path: %s", dataPathStr);
            return;
        }

        if (strcmp(dataPathStr, "/restart") == 0) {
            ESPLogger.warn(TAG_FIREBASE, "Restart command received from Firebase; flushing logs before reboot");
            ESPLogger.fatal(TAG_FIREBASE, "Remote restart command accepted");
            ESPLogger.loop();
            databaseSend.set<bool>(asyncClientSend, "/heatpump/restart", false, logFirebaseWriteResult, TASK_UID_ACK);
            delay(3000);
            ESP.restart();
        }

        int powerValue = 0;
        int tenModeValue = 0;
        int fanValue = 0;
        float tempValue = 0.0f;
        int idValue = 0;

        if (!parseHeatpumpCommandPayload(jsonStr, powerValue, tenModeValue, fanValue, tempValue, idValue)) {
            ESPLogger.warn(TAG_FIREBASE, "Incoming command is missing one or more required fields");
            publishCommandAcknowledgement(0, "failed", "Command payload was invalid", "Incoming command is missing one or more required fields");
            return;
        }

        const bool commandValuesAreValid = (powerValue == 0 || powerValue == 1) &&
                                           (tenModeValue == 0 || tenModeValue == 1) &&
                                           fanValue >= 0 && fanValue <= static_cast<int>(MAX_FAN_SPEED) &&
                                           tempValue >= static_cast<float>(MIN_SUPPORTED_TEMPERATURE) &&
                                           tempValue <= static_cast<float>(MAX_SUPPORTED_TEMPERATURE);

        if (!commandValuesAreValid) {
            ESPLogger.error(TAG_FIREBASE,
                            "Command values out of range (power=%d tenMode=%d fan=%d temp=%.1f)",
                            powerValue,
                            tenModeValue,
                            fanValue,
                            tempValue);
            publishCommandAcknowledgement(idValue, "failed", "Command rejected by validation", "One or more command values were outside the supported range", powerValue, tenModeValue, fanValue, tempValue);
            return;
        }

        ESPLogger.info(TAG_FIREBASE, "Processing valid heatpump command power=%d tenMode=%d fan=%d temp=%.1f",
                       powerValue, tenModeValue, fanValue, tempValue);

        publishCommandAcknowledgement(idValue, "received", "Command received by the controller", nullptr, powerValue, tenModeValue, fanValue, tempValue);
        const bool irSent = irController.send(powerValue, tenModeValue, fanValue, static_cast<uint8_t>(tempValue));

        logHeapUsage("processFirebaseData:after_send_ir");

        ESPLogger.debug(TAG_FIREBASE, "Command ID: %d", idValue);

        if (!irSent) {
            ESPLogger.error(TAG_FIREBASE, "IR command was rejected for command ID: %d", idValue);
            publishCommandAcknowledgement(idValue, "failed", "Command could not be executed", "The IR controller rejected the command", powerValue, tenModeValue, fanValue, tempValue);
            return;
        }

        publishCommandAcknowledgement(idValue, "executed", "Command executed successfully", nullptr, powerValue, tenModeValue, fanValue, tempValue);
        ESPLogger.debug(TAG_FIREBASE, "Queued acknowledgment task; stream tasks=%u send tasks=%u",
                        static_cast<unsigned>(asyncClientStream.taskCount()),
                        static_cast<unsigned>(asyncClientSend.taskCount()));
        logHeapUsage("processFirebaseData:after_ack_schedule");
    } else {
        ESPLogger.trace(TAG_FIREBASE, "Ignoring non-stream database result");
    }
}

// =============== Sensor Handling Functions ===============
// Read the sensor on a bounded interval and publish the resulting values to Firebase.
void handleSensorReadings() {
    static unsigned long lastReadingTime = 0;
    unsigned long currentTime = millis();

    if (currentTime - lastReadingTime >= Config::SENSOR_READ_INTERVAL || lastReadingTime == 0) {
        lastReadingTime = currentTime;

        if (WiFi.status() != WL_CONNECTED) {
            ESPLogger.warn(TAG_SENSOR, "Skipping sensor upload because Wi-Fi is disconnected");
            return;
        }

        if (!firebaseApp.ready()) {
            ESPLogger.debug(TAG_SENSOR, "Skipping sensor upload because Firebase is not ready yet");
            return;
        }

        auto status = temperatureSensor.read();
        if (status != AM2302::AM2302_READ_OK) {
            ESPLogger.warn(TAG_SENSOR, "Error reading sensor data (status=%d)", static_cast<int>(status));
            return;
        }

        float temperature = temperatureSensor.get_Temperature();
        float humidity = temperatureSensor.get_Humidity();

        if (isnan(temperature) || isnan(humidity) || temperature < -40.0f || temperature > 100.0f || humidity < 0.0f || humidity > 100.0f) {
            ESPLogger.warn(TAG_SENSOR, "Ignoring invalid sensor values: temp=%.2f hum=%.2f", temperature, humidity);
            return;
        }

        unsigned long long timestamp = getCurrentUnixTime();

        char tempPath[48];
        char humPath[48];
        snprintf(tempPath, sizeof(tempPath), "/temp/%llu/temp", timestamp);
        snprintf(humPath, sizeof(humPath), "/temp/%llu/hum", timestamp);

        ESPLogger.debug(TAG_SENSOR, "Publishing sensor data at timestamp %llu", timestamp);
        databaseSend.set<float>(asyncClientSend, tempPath, temperature, logFirebaseWriteResult, TASK_UID_PUSH_TEMP);
        databaseSend.set<float>(asyncClientSend, humPath, humidity, logFirebaseWriteResult, TASK_UID_PUSH_HUM);
        ESPLogger.debug(TAG_SENSOR, "Queued sensor upload tasks; stream tasks=%u send tasks=%u",
                        static_cast<unsigned>(asyncClientStream.taskCount()),
                        static_cast<unsigned>(asyncClientSend.taskCount()));
        logHeapUsage("handleSensorReadings:after_schedule");

        ESPLogger.debug(TAG_SENSOR, "Sent sensor data - Temp: %.1fC, Hum: %.1f%%", temperature, humidity);
    }
}

// Return the current Unix timestamp from the NTP client.
unsigned long long getCurrentUnixTime() {
    if (WiFi.status() != WL_CONNECTED) {
        ESPLogger.debug(TAG_MAIN, "Skipping NTP update because Wi-Fi is disconnected");
        return static_cast<unsigned long long>(millis() / 1000U);
    }

    const bool updated = timeClient.update();
    if (!updated) {
        ESPLogger.warn(TAG_MAIN, "NTP update did not refresh time; using cached epoch value");
    }

    return timeClient.getEpochTime();
}

// =============== Heap Logging ===============
// Heap diagnostics are used to detect memory pressure before the device becomes unstable.
static unsigned long lastHeapLogTime = 0;
static unsigned long lastHeapInfoLogTime = 0;
static const unsigned long HEAP_LOG_INTERVAL = 30000; // ms
static size_t minHeapSeen = (size_t)-1;

void logHeapUsage(const char* tag, LogLevel level) {
    size_t freeHeap = ESP.getFreeHeap();
    uint8_t frag = ESP.getHeapFragmentation();
    if (freeHeap < minHeapSeen) minHeapSeen = freeHeap;
    ESPLogger.logf(level, TAG_HEAP, "%s free=%u min=%u frag=%u", tag, (unsigned)freeHeap, (unsigned)minHeapSeen, (unsigned)frag);
}

void logFirebaseWriteResult(AsyncResult& result) {
    if (!result.isResult()) {
        return;
    }

    if (result.isError()) {
        ESPLogger.error(TAG_FIREBASE, "Firebase write failed for task %s: %s (code %d)",
                        result.uid().c_str(),
                        result.error().message().c_str(),
                        result.error().code());
        return;
    }

    if (result.available()) {
        ESPLogger.trace(TAG_FIREBASE, "Firebase write completed for task %s", result.uid().c_str());
    }
}

void publishHeartbeat() {
    const unsigned long now = millis();
    if (lastHeartbeatSendTime != 0 && now - lastHeartbeatSendTime < HEARTBEAT_INTERVAL_MS) {
        return;
    }
    lastHeartbeatSendTime = now;

    if (WiFi.status() != WL_CONNECTED || !firebaseApp.ready()) {
        ESPLogger.debug(TAG_FIREBASE, "Skipping heartbeat publish until Wi-Fi and Firebase are ready");
        return;
    }

    const bool online = true;
    const int uptimeSeconds = static_cast<int>(now / 1000UL);
    const int freeHeap = static_cast<int>(ESP.getFreeHeap());
    const int frag = static_cast<int>(ESP.getHeapFragmentation());
    const int rssi = WiFi.RSSI();
    const int minHeap = static_cast<int>((minHeapSeen != (size_t)-1) ? minHeapSeen : ESP.getFreeHeap());
    const int lastSeen = static_cast<int>(getCurrentUnixTime());

    String heartbeatPayload;
    heartbeatPayload.reserve(160);
    heartbeatPayload = String("{\"online\":") + (online ? "true" : "false") +
                       ",\"uptime\":" + String(uptimeSeconds) +
                       ",\"freeHeap\":" + String(freeHeap) +
                       ",\"minHeap\":" + String(minHeap) +
                       ",\"frag\":" + String(frag) +
                       ",\"rssi\":" + String(rssi) +
                       ",\"lastSeen\":" + String(lastSeen) + "}";

    object_t heartbeatJson(heartbeatPayload.c_str());
    databaseSend.set<object_t>(asyncClientSend, "/status/heartbeat", heartbeatJson, logFirebaseWriteResult, "heartbeatPayload");

    ESPLogger.debug(TAG_FIREBASE, "Heartbeat payload: %s", heartbeatPayload.c_str());
    ESPLogger.debug(TAG_FIREBASE, "Heartbeat published: uptime=%d freeHeap=%d minHeap=%d frag=%d rssi=%d",
                    uptimeSeconds, freeHeap, minHeap, frag, rssi);
}

// Perform lightweight health checks and restart the ESP if it becomes unhealthy.
void checkSystemHealth() {
    unsigned long uptime = millis();
    size_t freeHeap = ESP.getFreeHeap();

    if (freeHeap < MIN_FREE_HEAP_BYTES) {
        ESPLogger.error(TAG_HEAP, "Low heap detected: %u bytes free (threshold %u), restarting system",
                        (unsigned)freeHeap, (unsigned)MIN_FREE_HEAP_BYTES);
        ESPLogger.fatal(TAG_HEAP, "Unrecoverable memory pressure; restarting");
        ESPLogger.loop();
        delay(3000);
        ESP.restart();
    }

    if (uptime >= MAX_UPTIME_BEFORE_RESTART_MS) {
        ESPLogger.warn(TAG_MAIN, "Restarting after %lu ms uptime", uptime);
        ESPLogger.fatal(TAG_MAIN, "Planned uptime rollover restart");
        ESPLogger.loop();
        delay(3000);
        ESP.restart();
    }
}

// =============== Main Loop ===============
// The main loop keeps the system responsive by running health checks,
// processing Firebase work, and publishing periodic logs and sensor data.
void loop() {
    const unsigned long now = millis();

    if (now - lastLoopTraceTime >= LOOP_TRACE_INTERVAL_MS || lastLoopTraceTime == 0) {
        lastLoopTraceTime = now;
        ESPLogger.trace(TAG_MAIN, "Heartbeat uptime=%lu wifi=%d firebaseReady=%d", now, WiFi.status(), firebaseApp.ready() ? 1 : 0);
    }

    checkSystemHealth();

    firebaseApp.loop(); // Handle Firebase background tasks
    if(firebaseApp.ready()) {
            handleSensorReadings();
            publishHeartbeat();
    }
    ESPLogger.loop();
    
    // Periodic heap logging
    if (now - lastHeapLogTime >= HEAP_LOG_INTERVAL || lastHeapLogTime == 0) {
        lastHeapLogTime = now;
        logHeapUsage("periodic", LogLevel::DEBUG);
    }

    // Lower-frequency operational heap heartbeat for higher-level visibility.
    if (now - lastHeapInfoLogTime >= HEAP_INFO_LOG_INTERVAL_MS || lastHeapInfoLogTime == 0) {
        lastHeapInfoLogTime = now;
        logHeapUsage("periodic:30min", LogLevel::INFO);
    }

    delay(100);
}