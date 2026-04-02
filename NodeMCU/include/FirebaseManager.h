#pragma once
#include <FirebaseClient.h>
#include <ArduinoJson.h>
#include "Config.h"

// FirebaseManager namespace provides functions to initialize, loop, and interact with Firebase
namespace FirebaseManager {
    // Initialize Firebase and set up listeners
    void initialize();
    // Call in main loop to keep Firebase connection alive
    void loop();
    // Returns true if Firebase is ready
    bool ready();
    // Send sensor data (timestamp, temperature, humidity) to Firebase
    void sendSensorData(unsigned long long timestamp, float temperature, float humidity);
}
