#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <ESP.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <FirebaseClient.h>
#include <ESPLogger.h>
#include "FirmwareVersion.h"
#include "OTAUpdateManager.h"
#include "IRController.h"
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <AM2302-Sensor.h>

#include "secrets.h"

// =============== Global Objects ===============
IRController irController;
AM2302::AM2302_Sensor temperatureSensor(Config::SENSOR_PIN);
WiFiClientSecure sslClientDiscord;
SerialSink serialSink;
DiscordSink discordSink(sslClientDiscord, Config::DISCORD_WEBHOOK_URL);

static constexpr char TAG_MAIN[] = "Main";
static constexpr char TAG_WIFI[] = "WiFi";
static constexpr char TAG_SENSOR[] = "Sensor";
static constexpr char TAG_FIREBASE[] = "Firebase";
static constexpr char TAG_HEAP[] = "Heap";

static constexpr size_t MIN_FREE_HEAP_BYTES = 6 * 1024;
static constexpr unsigned long MAX_UPTIME_BEFORE_RESTART_MS = 48UL * 60UL * 60UL * 1000UL;

// NTP Client
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, Config::NTP_SERVER, Config::TIMEZONE_OFFSET, Config::NTP_UPDATE_INTERVAL);

// Firebase Authentication
UserAuth userAuth(Config::WEB_API_KEY, Config::USER_EMAIL, Config::USER_PASSWORD);

// Firebase App and Database
FirebaseApp firebaseApp;

// For streaming data
WiFiClientSecure sslClientStream;
using AsyncClient = AsyncClientClass;
AsyncClient asyncClientStream(sslClientStream);
RealtimeDatabase databaseStream;

// For sending data
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
void logHeapUsage(const char* tag, LogLevel level = LogLevel::DEBUG);
void checkSystemHealth();
void checkOTAUpdate();

// =============== Initialization Functions ===============
void setup() {
    Serial.begin(115200);
    while (!Serial) {} // Wait for serial port to connect (for debugging)

    sslClientDiscord.setInsecure();
    sslClientDiscord.setTimeout(Config::SSL_TIMEOUT);
    sslClientDiscord.setBufferSizes(Config::DISCORD_SSL_RX_BUFFER_SIZE, Config::DISCORD_SSL_TX_BUFFER_SIZE);

    ESPLogger.clearSinks();
    ESPLogger.addSink(&serialSink, LogLevel::DEBUG);
    ESPLogger.addSink(&discordSink, LogLevel::WARN);

    ESPLogger.info(TAG_MAIN, "Logger initialized");
    ESPLogger.info(TAG_MAIN, "Firmware version: %llu", static_cast<unsigned long long>(getFirmwareVersion()));

    initializeWiFi();
    checkOTAUpdate();
    initializeTimeClient();
    initializeSensor();
    initializeFirebase();

    ESPLogger.info(TAG_MAIN, "System initialization complete");
}

void initializeWiFi() {
    ESPLogger.info(TAG_WIFI, "Connecting to WiFi");
    WiFi.begin(Config::WIFI_SSID, Config::WIFI_PASSWORD);

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && 
          millis() - startTime < Config::WIFI_CONNECT_TIMEOUT) {
        Serial.print(".");
        delay(Config::WIFI_RETRY_DELAY);
    }

    if (WiFi.status() != WL_CONNECTED) {
        ESPLogger.error(TAG_WIFI, "Failed to connect to WiFi");
        ESP.restart(); // Consider more graceful recovery
    }

    ESPLogger.info(TAG_WIFI, "WiFi connected");
    IPAddress ipAddress = WiFi.localIP();
    ESPLogger.debug(TAG_WIFI, "IP Address: %u.%u.%u.%u", ipAddress[0], ipAddress[1], ipAddress[2], ipAddress[3]);
}

void initializeFirebase() {
    // Configure SSL clients
    sslClientStream.setInsecure();
    sslClientStream.setTimeout(Config::SSL_TIMEOUT);
    sslClientStream.setBufferSizes(Config::FIREBASE_SSL_RX_BUFFER_SIZE, Config::FIREBASE_SSL_TX_BUFFER_SIZE);

    sslClientSend.setInsecure();
    sslClientSend.setTimeout(Config::SSL_TIMEOUT);
    sslClientSend.setBufferSizes(Config::FIREBASE_SSL_RX_BUFFER_SIZE, Config::FIREBASE_SSL_TX_BUFFER_SIZE);

    // Initialize Firebase authentication
    initializeApp(asyncClientStream, firebaseApp, getAuth(userAuth), [](AsyncResult&){}, "authTask");
    
    // Get database references
    firebaseApp.getApp<RealtimeDatabase>(databaseStream);
    firebaseApp.getApp<RealtimeDatabase>(databaseSend);
    
    databaseStream.url(Config::DATABASE_URL);
    databaseSend.url(Config::DATABASE_URL);

    // Set filters for SSE (only listen to put/patch events)
    asyncClientStream.setSSEFilters("put, patch");

    // Start listening to heatpump commands and restart requests
    databaseStream.get(asyncClientStream, "/", processFirebaseData, true, "RTDB_Listen");

    ESPLogger.info(TAG_FIREBASE, "Firebase initialized");
}

void initializeTimeClient() {
    timeClient.begin();
    ESPLogger.info(TAG_MAIN, "NTP client initialized");
}

void initializeSensor() {
    if (!temperatureSensor.begin()) {
        ESPLogger.error(TAG_SENSOR, "Failed to initialize temperature sensor");
        return;
    }
    delay(Config::SENSOR_INIT_DELAY); // Initial stabilization delay
    ESPLogger.info(TAG_SENSOR, "Temperature sensor initialized");
}

// =============== Data Processing Functions ===============
void processFirebaseData(AsyncResult& result) {
    if (!result.isResult()) return;

    logHeapUsage("processFirebaseData:enter");

    if (result.isError()) {
        ESPLogger.error(TAG_FIREBASE, "Error in task %s: %s (code %d)",
                        result.uid().c_str(),
                        result.error().message().c_str(),
                        result.error().code());
        return;
    }

    if (!result.available()) return;

    RealtimeDatabaseResult& rtdb = result.to<RealtimeDatabaseResult>();

    if (rtdb.isStream()) {
        const String eventType = rtdb.event();
        const String dataPath = rtdb.dataPath();
        const char* jsonStr = rtdb.to<const char*>();

        ESPLogger.debug(TAG_FIREBASE, "[Event] Type: %s, Path: %s, Data: %s",
                        eventType.c_str(), dataPath.c_str(), jsonStr);

        // Skip empty or heartbeat messages
        if (strlen(jsonStr) == 0 || strcmp(jsonStr, "null") == 0) {
            ESPLogger.debug(TAG_FIREBASE, "Ignoring empty/null data");
            return;
        }

        if (strcmp(dataPath.c_str(), "/restart") == 0) {
            bool restartRequested = false;

            if (strcmp(jsonStr, "true") == 0 || strcmp(jsonStr, "1") == 0) {
                restartRequested = true;
            } else {
                JsonDocument restartDoc;
                DeserializationError restartErr = deserializeJson(restartDoc, jsonStr);
                if (!restartErr) {
                    if (restartDoc.is<bool>()) {
                        restartRequested = restartDoc.as<bool>();
                    } else if (restartDoc.is<const char*>()) {
                        const char* text = restartDoc.as<const char*>();
                        restartRequested = text != nullptr && strcmp(text, "true") == 0;
                    }
                }
            }

            if (!restartRequested) {
                ESPLogger.debug(TAG_FIREBASE, "Restart flag is false; ignoring");
                return;
            }

            ESPLogger.warn(TAG_FIREBASE, "Restart requested via Firebase");
            databaseSend.set(asyncClientSend, "/restart", false, [](AsyncResult& result) {
                if (result.isError()) {
                    ESPLogger.error(TAG_FIREBASE, "Failed to acknowledge restart request: %s (code %d)",
                                    result.error().message().c_str(),
                                    result.error().code());
                    return;
                }

                ESPLogger.warn(TAG_MAIN, "Restarting after Firebase restart request");
                ESPLogger.loop();
                delay(300);
                ESP.restart();
            }, "restartAckTask");
            return;
        }

        if(strcmp(dataPath.c_str(), "/heatpump/data") != 0 && strcmp(dataPath.c_str(), "/data") != 0) {
            ESPLogger.debug(TAG_FIREBASE, "Ignoring event for path: %s", dataPath.c_str());
            return;
        }

        // Parse JSON
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, jsonStr);

        if (err) {
            ESPLogger.warn(TAG_FIREBASE, "JSON parsing failed: %s", err.c_str());
            return;
        }

        // Process valid command
        ESPLogger.info(TAG_FIREBASE, "Processing valid heatpump command");
        irController.send(
            doc["power"].as<int>(),
            doc["tenDegreeMode"].as<int>(),
            doc["fan"].as<int>(),
            doc["temp"].as<float>()
        );

        logHeapUsage("processFirebaseData:after_send_ir");

        // Send acknowledgment if ID exists

        const int idStr = doc["id"].as<int>();
        ESPLogger.debug(TAG_FIREBASE, "Command ID: %d", idStr);

        if(idStr) {
            ESPLogger.info(TAG_FIREBASE, "Sending acknowledgment for command ID: %d", idStr);
            databaseSend.set(asyncClientSend, "/response", idStr, [](AsyncResult&){}, "acknowledgmentTask");
        } else {
            ESPLogger.debug(TAG_FIREBASE, "No valid command ID found; skipping acknowledgment");
        }
        /*if(idStr && strlen(idStr) > 0) {
            Serial.printf("Sending acknowledgment for command ID: %s\n", idStr);
            databaseSend.set(asyncClientSend, "/response", idStr);
        } else {
            Serial.printf("No command ID found; skipping acknowledgment, %s", idStr);
        }*/
    }
}

// =============== Sensor Handling Functions ===============
void handleSensorReadings() {
    static unsigned long lastReadingTime = 0;
    unsigned long currentTime = millis();

    if (currentTime - lastReadingTime >= Config::SENSOR_READ_INTERVAL || lastReadingTime == 0) {
        lastReadingTime = currentTime;

        auto status = temperatureSensor.read();
        if (status != AM2302::AM2302_READ_OK) {
            ESPLogger.warn(TAG_SENSOR, "Error reading sensor data");
            return;
        }

        float temperature = temperatureSensor.get_Temperature();
        float humidity = temperatureSensor.get_Humidity();
        unsigned long long timestamp = getCurrentUnixTime();

        char tempPath[48];
        char humPath[48];
        snprintf(tempPath, sizeof(tempPath), "/temp/%llu/temp", timestamp);
        snprintf(humPath, sizeof(humPath), "/temp/%llu/hum", timestamp);

        databaseSend.set(asyncClientSend, tempPath, temperature, [](AsyncResult&){}, "pushTempTask");
        databaseSend.set(asyncClientSend, humPath, humidity, [](AsyncResult&){}, "pushHumTask");

        ESPLogger.debug(TAG_SENSOR, "Sent sensor data - Temp: %.1fC, Hum: %.1f%%", temperature, humidity);
    }
}

unsigned long long getCurrentUnixTime() {
    timeClient.update();
    return timeClient.getEpochTime();
}

// =============== Heap Logging ===============
static unsigned long lastHeapLogTime = 0;
static const unsigned long HEAP_LOG_INTERVAL = 30000; // ms
static size_t minHeapSeen = (size_t)-1;

void logHeapUsage(const char* tag, LogLevel level) {
    size_t freeHeap = ESP.getFreeHeap();
    uint8_t frag = ESP.getHeapFragmentation();
    if (freeHeap < minHeapSeen) minHeapSeen = freeHeap;
    ESPLogger.logf(level, TAG_HEAP, "%s free=%u min=%u frag=%u", tag, (unsigned)freeHeap, (unsigned)minHeapSeen, (unsigned)frag);
}

void checkSystemHealth() {
    unsigned long uptime = millis();
    size_t freeHeap = ESP.getFreeHeap();

    if (freeHeap < MIN_FREE_HEAP_BYTES) {
        ESPLogger.error(TAG_HEAP, "Low heap detected: %u bytes free (threshold %u), restarting system",
                        (unsigned)freeHeap, (unsigned)MIN_FREE_HEAP_BYTES);
        ESPLogger.loop();
        delay(3000);
        ESP.restart();
    }

    if (uptime >= MAX_UPTIME_BEFORE_RESTART_MS) {
        ESPLogger.warn(TAG_MAIN, "Restarting after %lu ms uptime", uptime);
        ESPLogger.loop();
        delay(3000);
        ESP.restart();
    }
}

// =============== Main Loop ===============
void loop() {
    checkSystemHealth();

    firebaseApp.loop(); // Handle Firebase background tasks
    if(firebaseApp.ready()) {
            handleSensorReadings();
    }
    ESPLogger.loop();
    
    // Periodic heap logging
    unsigned long now = millis();
    if (now - lastHeapLogTime >= HEAP_LOG_INTERVAL || lastHeapLogTime == 0) {
        lastHeapLogTime = now;
        logHeapUsage("periodic", LogLevel::DEBUG);
    }

    delay(100);
}