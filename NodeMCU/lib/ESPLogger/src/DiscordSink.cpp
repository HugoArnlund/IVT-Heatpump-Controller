#include "DiscordSink.h"

#include <stdio.h>
#include <string.h>

namespace {

bool startsWith(const char* text, const char* prefix)
{
    if (text == nullptr || prefix == nullptr) {
        return false;
    }

    while (*prefix != '\0') {
        if (*text++ != *prefix++) {
            return false;
        }
    }

    return true;
}

void copyBounded(char* destination, size_t capacity, const char* source, size_t length)
{
    if (destination == nullptr || capacity == 0) {
        return;
    }

    size_t copyLength = length;
    if (copyLength >= capacity) {
        copyLength = capacity - 1;
    }

    if (source != nullptr && copyLength > 0) {
        memcpy(destination, source, copyLength);
    }

    destination[copyLength] = '\0';
}

} // namespace

DiscordSink::DiscordSink(WiFiClientSecure& client, const char* webhookUrl)
    : m_client(client)
    , m_host{0}
    , m_path{0}
    , m_pendingMessage{0}
    , m_port(443)
    , m_hasPending(false)
    , m_configured(false)
{
    copyWebhookUrl(webhookUrl);
}

DiscordSink::DiscordSink(WiFiClientSecure& client, const char* host, const char* path, uint16_t port)
    : m_client(client)
    , m_host{0}
    , m_path{0}
    , m_pendingMessage{0}
    , m_port(port)
    , m_hasPending(false)
    , m_configured(false)
{
    copyString(m_host, sizeof(m_host), host);
    copyString(m_path, sizeof(m_path), path);
    m_configured = (m_host[0] != '\0' && m_path[0] != '\0');
}

void DiscordSink::copyString(char* destination, size_t capacity, const char* source)
{
    if (destination == nullptr || capacity == 0) {
        return;
    }

    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }

    size_t length = strlen(source);
    if (length >= capacity) {
        length = capacity - 1;
    }

    memcpy(destination, source, length);
    destination[length] = '\0';
}

void DiscordSink::copyWebhookUrl(const char* webhookUrl)
{
    m_host[0] = '\0';
    m_path[0] = '\0';

    if (webhookUrl == nullptr || *webhookUrl == '\0') {
        m_configured = false;
        return;
    }

    const char* cursor = webhookUrl;
    if (startsWith(cursor, "https://")) {
        cursor += 8;
    } else if (startsWith(cursor, "http://")) {
        cursor += 7;
    }

    const char* hostStart = cursor;
    while (*cursor != '\0' && *cursor != '/' && *cursor != '?') {
        ++cursor;
    }

    copyBounded(m_host, sizeof(m_host), hostStart, static_cast<size_t>(cursor - hostStart));

    if (*cursor == '\0') {
        copyString(m_path, sizeof(m_path), "/");
    } else if (*cursor == '/' || *cursor == '?') {
        copyString(m_path, sizeof(m_path), cursor);
    }

    m_configured = (m_host[0] != '\0' && m_path[0] != '\0');
}

bool DiscordSink::configured() const
{
    return m_configured;
}

void DiscordSink::clearPending()
{
    m_pendingMessage[0] = '\0';
    m_hasPending = false;
}

void DiscordSink::write(LogLevel, const char*, const char* message)
{
    if (!m_configured || message == nullptr || *message == '\0') {
        clearPending();
        return;
    }

    copyString(m_pendingMessage, sizeof(m_pendingMessage), message);
    m_hasPending = true;
}

void DiscordSink::loop()
{
    if (!m_hasPending) {
        return;
    }

    if (!sendPending()) {
        clearPending();
    }
}

size_t DiscordSink::escapedLength(const char* message) const
{
    if (message == nullptr) {
        return 0;
    }

    size_t length = 0;
    while (*message != '\0') {
        switch (*message) {
            case '"':
            case '\\':
            case '\n':
            case '\r':
            case '\t':
                length += 2;
                break;
            default:
                length += 1;
                break;
        }

        ++message;
    }

    return length;
}

void DiscordSink::writeEscapedJsonString(const char* message)
{
    if (message == nullptr) {
        return;
    }

    while (*message != '\0') {
        switch (*message) {
            case '"':
                m_client.print("\\\"");
                break;
            case '\\':
                m_client.print("\\\\");
                break;
            case '\n':
                m_client.print("\\n");
                break;
            case '\r':
                m_client.print("\\r");
                break;
            case '\t':
                m_client.print("\\t");
                break;
            default:
            {
                char character[2] = { *message, '\0' };
                m_client.write(character);
                break;
            }
        }

        ++message;
    }
}

bool DiscordSink::sendPending()
{
    if (!m_configured || WiFi.status() != WL_CONNECTED) {
        return false;
    }

    if (!m_client.connect(m_host, m_port)) {
        return false;
    }

    const size_t bodyLength = 14 + escapedLength(m_pendingMessage);

    m_client.print(F("POST "));
    m_client.print(m_path);
    m_client.println(F(" HTTP/1.1"));
    m_client.print(F("Host: "));
    m_client.println(m_host);
    m_client.println(F("User-Agent: ESPLogger/1.0"));
    m_client.println(F("Content-Type: application/json"));
    m_client.print(F("Content-Length: "));
    m_client.println(bodyLength);
    m_client.println(F("Connection: close"));
    m_client.println();
    m_client.print(F("{\"content\":\""));
    writeEscapedJsonString(m_pendingMessage);
    m_client.print(F("\"}"));

    m_client.flush();
    m_client.stop();
    clearPending();
    return true;
}