#ifndef ESPLOGGER_DISCORDSINK_H
#define ESPLOGGER_DISCORDSINK_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>

#include <string.h>

#include "ESPLoggerConfig.h"
#include "LogSink.h"

class DiscordSink : public LogSink
{
public:
    DiscordSink(WiFiClientSecure& client, const char* webhookUrl);
    DiscordSink(WiFiClientSecure& client, const char* host, const char* path, uint16_t port = 443);

    void write(LogLevel level, const char* tag, const char* message) override;
    void loop() override;

    bool configured() const;

private:
    static constexpr size_t DISCORD_HOST_SIZE = 64;
    static constexpr size_t DISCORD_PATH_SIZE = 224;

    void clearPending();
    void copyString(char* destination, size_t capacity, const char* source);
    void copyWebhookUrl(const char* webhookUrl);
    bool sendPending();
    size_t escapedLength(const char* message) const;
    void writeEscapedJsonString(const char* message);

    WiFiClientSecure& m_client;
    char m_host[DISCORD_HOST_SIZE];
    char m_path[DISCORD_PATH_SIZE];
    char m_pendingMessage[DISCORD_BUFFER_SIZE];
    uint16_t m_port;
    bool m_hasPending;
    bool m_configured;
};

#endif