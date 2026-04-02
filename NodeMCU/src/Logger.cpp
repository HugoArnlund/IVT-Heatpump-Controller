
#include "Logger.h"
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include "Secrets.h" // For DISCORD_WEBHOOK_URL
#include "TimeManager.h"

namespace Logger {

namespace {
    String webhookUrl;
    String infoBuffer[40];
    int infoBufferCount = 0;
}

void initialize() {
    webhookUrl = Secrets::DISCORD_WEBHOOK_URL;
}

void setWebhook(const String &url) {
    webhookUrl = url;
}

void sendToDiscord(const String &message, bool mentionEveryone) {
    if (webhookUrl.length() == 0) return;
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;
    if (https.begin(client, webhookUrl)) {
        String payload = "{";
        if (mentionEveryone) {
            payload += "\"content\": \"@everyone ";
        } else {
            payload += "\"content\": \"";
        }
        payload += message;
        payload += "\"}";
        https.addHeader("Content-Type", "application/json");
        https.POST(payload);
        https.end();
    }
}

void log(LogLevel level, const String &message, const String &tag) {
    String levelStr;
    switch (level) {
        case LOG_DEBUG: levelStr = "[DEBUG] "; break;
        case LOG_INFO: levelStr = "[INFO] "; break;
        case LOG_WARNING: levelStr = "[WARNING] "; break;
        case LOG_ERROR: levelStr = "[ERROR] "; break;
        case LOG_CRITICAL: levelStr = "[CRITICAL] "; break;
    }
    String timestamp = String(TimeManager::getCurrentUnixTime());
    String tagStr = tag.length() > 0 ? ("[" + tag + "] ") : "";
    String fullMsg = "[" + timestamp + "] " + levelStr + tagStr + message;
    Serial.println(fullMsg);

    if (level == LOG_INFO) {
        if (infoBufferCount < 40) {
            infoBuffer[infoBufferCount++] = fullMsg;
        }
        if (infoBufferCount >= 40) {
            String payload = "Info log batch (" + String(infoBufferCount) + "):\n";
            for (int i = 0; i < infoBufferCount; ++i) {
                payload += infoBuffer[i] + "\n";
            }
            sendToDiscord(payload, false);
            infoBufferCount = 0;
        }
    }
    if (level >= LOG_WARNING) {
        bool mention = (level == LOG_CRITICAL);
        sendToDiscord(fullMsg, mention);
    }
}

void flushInfoBuffer() {
    if (infoBufferCount == 0) return;
    String payload = "Info log batch (" + String(infoBufferCount) + "):\n";
    for (int i = 0; i < infoBufferCount; ++i) {
        payload += infoBuffer[i] + "\n";
    }
    sendToDiscord(payload, false);
    infoBufferCount = 0;
}

} // namespace Logger
