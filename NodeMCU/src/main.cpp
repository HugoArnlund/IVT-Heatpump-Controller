#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <FirebaseClient.h>
#include "IRController.h"
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <AM2302-Sensor.h>

#include "secrets.h"

// =============== Type Definitions ===============
struct HeatpumpData {
    int power;
    int tenMode;
    int fan;
    float temp;
};

// =============== Global Objects ===============
IRController irController;
AM2302::AM2302_Sensor temperatureSensor(Config::SENSOR_PIN);

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

// =============== Initialization Functions ===============
void setup() {
    Serial.begin(115200);
    while (!Serial) {} // Wait for serial port to connect (for debugging)

    initializeWiFi();
    initializeTimeClient();
    initializeSensor();
    initializeFirebase();

    Serial.println("System initialization complete");
}

void initializeWiFi() {
    Serial.print("Connecting to WiFi");
    WiFi.begin(Config::WIFI_SSID, Config::WIFI_PASSWORD);

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && 
          millis() - startTime < Config::WIFI_CONNECT_TIMEOUT) {
        Serial.print(".");
        delay(Config::WIFI_RETRY_DELAY);
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nFailed to connect to WiFi");
        ESP.restart(); // Consider more graceful recovery
    }

    Serial.println("\nWiFi connected");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
}

void initializeFirebase() {
    // Configure SSL clients
    sslClientStream.setInsecure();
    sslClientStream.setTimeout(Config::SSL_TIMEOUT);
    sslClientStream.setBufferSizes(Config::SSL_RX_BUFFER_SIZE, Config::SSL_TX_BUFFER_SIZE);

    sslClientSend.setInsecure();
    sslClientSend.setTimeout(Config::SSL_TIMEOUT);
    sslClientSend.setBufferSizes(Config::SSL_RX_BUFFER_SIZE, Config::SSL_TX_BUFFER_SIZE);

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
}

void initializeTimeClient() {
    timeClient.begin();
    Serial.println("NTP client initialized");
}

void initializeSensor() {
    if (!temperatureSensor.begin()) {
        Serial.println("Error: Failed to initialize temperature sensor");
        return;
    }
    delay(Config::SENSOR_INIT_DELAY); // Initial stabilization delay
    Serial.println("Temperature sensor initialized");
}

// =============== Data Processing Functions ===============
void processFirebaseData(AsyncResult& result) {
    if (!result.isResult()) return;

    if (result.isError()) {
        Serial.printf("Error in task %s: %s (code %d)\n",
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

        Serial.printf("[Event] Type: %s, Path: %s, Data: %s\n", 
                     eventType.c_str(), dataPath.c_str(), jsonStr);

        // Skip empty or heartbeat messages
        if (strlen(jsonStr) == 0 || strcmp(jsonStr, "null") == 0) {
            Serial.println("Ignoring empty/null data");
            return;
        }

        if(strcmp(dataPath.c_str(), "/data") != 0) {
            Serial.printf("Ignoring event for path: %s\n", dataPath.c_str());
            return;
        }

        // Parse JSON
        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, jsonStr);

        if (err) {
            Serial.print("JSON parsing failed: ");
            Serial.println(err.c_str());
            return;
        }

        // Process valid command
        Serial.println("Processing valid heatpump command");
        irController.send(
            doc["power"].as<int>(),
            doc["tenDegreeMode"].as<int>(),
            doc["fan"].as<int>(),
            doc["temp"].as<float>()
        );

        // Send acknowledgment if ID exists
        if (doc.containsKey("id")) {
            databaseSend.set(asyncClientSend, "/response", doc["id"].as<String>());
        }
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
            Serial.println("Error reading sensor data");
            return;
        }

        float temperature = temperatureSensor.get_Temperature();
        float humidity = temperatureSensor.get_Humidity();
        unsigned long long timestamp = getCurrentUnixTime();

        // Send temperature data
        databaseSend.set(asyncClientSend, 
                        "/temp/" + String(timestamp) + "/temp", 
                        temperature, 
                        [](AsyncResult&){}, 
                        "pushTempTask");

        // Send humidity data
        databaseSend.set(asyncClientSend, 
                        "/temp/" + String(timestamp) + "/hum", 
                        humidity, 
                        [](AsyncResult&){}, 
                        "pushHumTask");

        Serial.printf("Sent sensor data - Temp: %.1f°C, Hum: %.1f%%\n", temperature, humidity);
    }
}

unsigned long long getCurrentUnixTime() {
    timeClient.update();
    return timeClient.getEpochTime();
}

// =============== Main Loop ===============
void loop() {
    firebaseApp.loop(); // Handle Firebase background tasks
    if(firebaseApp.ready()) {
            handleSensorReadings();
    }
    delay(100);
}