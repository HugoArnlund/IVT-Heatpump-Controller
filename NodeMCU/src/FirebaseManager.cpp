// Enable Firebase user authentication and database features
#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <FirebaseClient.h>
#include "FirebaseManager.h"
#include <ESP8266WiFi.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266HTTPClient.h>
#include "Secrets.h"
#include "Config.h"
#include "IRController.h"
#include "OTAUpdateManager.h"
#include "Logger.h"

// --- Constants for readability and maintainability ---
namespace {
    constexpr float MIN_TEMPERATURE = 18.0f;
    constexpr float MAX_TEMPERATURE = 30.0f;
    constexpr size_t JSON_DOC_SIZE_COMMAND = 256;
    constexpr size_t JSON_DOC_SIZE_OTA = 64;
    constexpr char DATA_PATH[] = "/data";
    constexpr char TEMP_PATH[] = "/temp/";
    constexpr char RESPONSE_PATH[] = "/response";
    constexpr char OTA_PATH[] = "/ota";
}

namespace FirebaseManager {

namespace {
    // Firebase user authentication object
    UserAuth userAuth(Secrets::WEB_API_KEY, Secrets::USER_EMAIL, Secrets::USER_PASSWORD);
    // Main Firebase app instance
    FirebaseApp firebaseApp;
    // Secure WiFi clients for streaming and sending
    WiFiClientSecure sslClientStream;
    using AsyncClient = AsyncClientClass;
    AsyncClient asyncClientStream(sslClientStream);
    RealtimeDatabase databaseStream;
    WiFiClientSecure sslClientSend;
    AsyncClient asyncClientSend(sslClientSend);
    RealtimeDatabase databaseSend;

    // Internal state to track Firebase readiness
    bool firebaseReady = false;

    // Helper: Clamp temperature to allowed range
    static float clampTemperature(const float value) {
        if (value < MIN_TEMPERATURE) return MIN_TEMPERATURE;
        if (value > MAX_TEMPERATURE) return MAX_TEMPERATURE;
        return value;
    }

    // Handle incoming heatpump commands from Firebase
    void processFirebaseData(AsyncResult& result) {
        // Guard: result must be valid
        if (!result.isResult()) {
            Serial.println("[Firebase] Result not valid, skipping.");
            return;
        }
        
        // Guard: result must be valid
        if (!result.isResult()) {
            Logger::log(Logger::LOG_WARNING, "Result not valid, skipping.", "FirebaseManager");
            return;
        }
        if (result.isError()) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Error in task %s: %s (code %d)",
                result.uid().c_str(),
                result.error().message().c_str(),
                result.error().code());
            Logger::log(Logger::LOG_ERROR, buf, "FirebaseManager");
            return;
        }
        if (!result.available()) {
            Logger::log(Logger::LOG_DEBUG, "Result not available, skipping.", "FirebaseManager");
            return;
        }

        RealtimeDatabaseResult& rtdb = result.to<RealtimeDatabaseResult>();
        if (!rtdb.isStream()) {
            Logger::log(Logger::LOG_DEBUG, "Not a stream event, skipping.", "FirebaseManager");
            return;
        }

        const String& eventType = rtdb.event();
        const String& dataPath = rtdb.dataPath();
        const char* jsonStr = rtdb.to<const char*>();
        {
            char buf[128];
            snprintf(buf, sizeof(buf), "[Event] Type: %s, Path: %s, Data: %s", eventType.c_str(), dataPath.c_str(), jsonStr ? jsonStr : "<null>");
            Logger::log(Logger::LOG_DEBUG, buf, "FirebaseManager");
        }

        // Guard: Ignore empty or null data
        if (!jsonStr || strlen(jsonStr) == 0 || strcmp(jsonStr, "null") == 0) {
            Logger::log(Logger::LOG_INFO, "Ignoring empty/null data", "FirebaseManager");
            return;
        }

        // Guard: Only process events for the /data path
        if (dataPath != DATA_PATH) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Ignoring event for path: %s", dataPath.c_str());
            Logger::log(Logger::LOG_INFO, buf, "FirebaseManager");
            return;
        }

        // Parse JSON command
        StaticJsonDocument<JSON_DOC_SIZE_COMMAND> doc;
        DeserializationError err = deserializeJson(doc, jsonStr);
        if (err) {
            String msg = String("JSON parsing failed: ") + err.c_str();
            Logger::log(Logger::LOG_ERROR, msg, "FirebaseManager");
            return;
        }

        // Defensive: Check required fields
        if (!doc.containsKey("power") || !doc.containsKey("tenDegreeMode") || !doc.containsKey("fan") || !doc.containsKey("temp")) {
            Logger::log(Logger::LOG_WARNING, "Missing required fields in command JSON.", "FirebaseManager");
            return;
        }
        Logger::log(Logger::LOG_INFO, "Processing valid heatpump command", "FirebaseManager");

        // Extract and clamp values
        uint8_t power = static_cast<uint8_t>(doc["power"].as<int>());
        uint8_t tenDegreeMode = static_cast<uint8_t>(doc["tenDegreeMode"].as<bool>());
        uint8_t fan = static_cast<uint8_t>(doc["fan"].as<int>());
        float temperatureValue = clampTemperature(doc["temp"].as<float>());
        uint8_t temp = static_cast<uint8_t>(temperatureValue);

        // Send IR command to heatpump
        IRController::send(power, tenDegreeMode, fan, temp);
        Logger::log(Logger::LOG_INFO, "IR command sent to heatpump", "FirebaseManager");

        // Respond with command ID if present
        if (doc.containsKey("id")) {
            databaseSend.set(asyncClientSend, RESPONSE_PATH, doc["id"].as<int>(), "responseTask");
            Logger::log(Logger::LOG_DEBUG, "Sent responseTask to database", "FirebaseManager");
        }
    };

    // Handle OTA update commands from Firebase
    void processOtaCommand(AsyncResult& result) {
        // Guard: result must be valid
        if (!result.isResult()) {
            Logger::log(Logger::LOG_WARNING, "OTA result not valid, skipping.", "FirebaseManager");
            return;
        }
        if (result.isError()) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Error in OTA task %s: %s (code %d)",
                result.uid().c_str(),
                result.error().message().c_str(),
                result.error().code());
            Logger::log(Logger::LOG_ERROR, buf, "FirebaseManager");
            return;
        }
        if (!result.available()) {
            Logger::log(Logger::LOG_DEBUG, "[OTA] Result not available, skipping.", "FirebaseManager");
            return;
        }
        RealtimeDatabaseResult& rtdb = result.to<RealtimeDatabaseResult>();
        if (!rtdb.isStream()) {
            Logger::log(Logger::LOG_DEBUG, "[OTA] Not a stream event, skipping.", "FirebaseManager");
            return;
        }

        const String& eventType = rtdb.event();
        const String& dataPath = rtdb.dataPath();
        const char* jsonStr = rtdb.to<const char*>();
        {
            char buf[128];
            snprintf(buf, sizeof(buf), "[OTA Event] Type: %s, Path: %s, Data: %s", eventType.c_str(), dataPath.c_str(), jsonStr ? jsonStr : "<null>");
            Logger::log(Logger::LOG_DEBUG, buf, "FirebaseManager");
        }

        // Guard: Ignore empty or null OTA data
        if (!jsonStr || strlen(jsonStr) == 0 || strcmp(jsonStr, "null") == 0) {
            Logger::log(Logger::LOG_INFO, "Ignoring empty/null OTA data", "FirebaseManager");
            return;
        }

        // Accept either boolean or string "true"
        bool doOta = false;
        if (strcmp(jsonStr, "true") == 0) {
            doOta = true;
        } else {
            StaticJsonDocument<JSON_DOC_SIZE_OTA> doc;
            DeserializationError err = deserializeJson(doc, jsonStr);
            if (!err && doc.is<bool>()) {
                doOta = doc.as<bool>();
            } else if (err) {
                String msg = String("[OTA] JSON parsing failed: ") + err.c_str();
                Logger::log(Logger::LOG_ERROR, msg, "FirebaseManager");
            }
        }
        if (!doOta) {
            Logger::log(Logger::LOG_INFO, "[OTA] OTA command not true, skipping update.", "FirebaseManager");
            return;
        }
        Logger::log(Logger::LOG_INFO, "OTA update command received from /ota!", "FirebaseManager");
        OTAUpdateManager::performOTAUpdate();
        // Reset OTA key in database after update
        databaseSend.set(asyncClientSend, OTA_PATH, false, "resetOtaKey");
        Logger::log(Logger::LOG_DEBUG, "OTA key reset in database", "FirebaseManager");
    }
}

    // Send sensor data (temperature and humidity) to Firebase
void sendSensorData(const unsigned long long timestamp, const float temperature, const float humidity) {
    String basePath = String(TEMP_PATH) + String(timestamp);
    databaseSend.set(asyncClientSend, basePath + "/temp", temperature, "pushTempTask");
    databaseSend.set(asyncClientSend, basePath + "/hum", humidity, "pushHumTask");
    char buf[96];
    snprintf(buf, sizeof(buf), "Sensor data sent to Firebase: ts=%llu, temp=%.2f, hum=%.2f", timestamp, temperature, humidity);
    Logger::log(Logger::LOG_INFO, buf, "FirebaseManager");
}



// Initialize Firebase, authentication, and database listeners
void initialize() {
    Logger::log(Logger::LOG_INFO, "Initializing FirebaseManager", "FirebaseManager");
    // Configure secure clients
    sslClientStream.setInsecure();
    sslClientStream.setTimeout(Config::SSL_TIMEOUT);
    sslClientStream.setBufferSizes(Config::SSL_RX_BUFFER_SIZE, Config::SSL_TX_BUFFER_SIZE);
    sslClientSend.setInsecure();
    sslClientSend.setTimeout(Config::SSL_TIMEOUT);
    sslClientSend.setBufferSizes(Config::SSL_RX_BUFFER_SIZE, Config::SSL_TX_BUFFER_SIZE);

    Logger::log(Logger::LOG_DEBUG, "Secure WiFi clients configured", "FirebaseManager");


    initializeApp(asyncClientSend, firebaseApp, getAuth(userAuth), [](AsyncResult&){
    }, "authSendTask");
    // Initialize Firebase app and authentication
    initializeApp(asyncClientStream, firebaseApp, getAuth(userAuth), [](AsyncResult&){
        firebaseReady = true;
        Logger::log(Logger::LOG_INFO, "Firebase authentication successful", "FirebaseManager");
    }, "authTask");
    firebaseApp.getApp<RealtimeDatabase>(databaseStream);
    firebaseApp.getApp<RealtimeDatabase>(databaseSend);
    databaseStream.url(Secrets::DATABASE_URL);
    databaseSend.url(Secrets::DATABASE_URL);

    Logger::log(Logger::LOG_DEBUG, "Firebase app and database initialized", "FirebaseManager");

    // Listen for changes on heatpump and OTA paths
    asyncClientStream.setSSEFilters("put, patch");
    databaseStream.get(asyncClientStream, "/heatpump/", processFirebaseData, true, "RTDB_Listen");
    Logger::log(Logger::LOG_DEBUG, "Listening for heatpump commands", "FirebaseManager");
    databaseStream.get(asyncClientStream, OTA_PATH, processOtaCommand, true, "RTDB_OTA_Listen");
    Logger::log(Logger::LOG_DEBUG, "Listening for OTA commands", "FirebaseManager");
}


// Call this in the main loop to keep Firebase connection alive
void loop() {
    // Add error handling for connection loss if needed
    firebaseApp.loop();
}


// Returns true if Firebase is ready
bool ready() {
    bool isReady = firebaseApp.ready();
    return isReady;
}

} // namespace FirebaseManager
