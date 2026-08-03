#ifndef ESPLOGGER_FIREBASESINK_H
#define ESPLOGGER_FIREBASESINK_H

#include <Arduino.h>

#include <stdio.h>
#include <string.h>

#include "LogSink.h"

class FirebaseSink : public LogSink
{
public:
    using PublishCallback = bool (*)(const char* path, const char* payload);

    explicit FirebaseSink(PublishCallback publishCallback = nullptr, const char* path = "/status/logs")
        : m_publishCallback(publishCallback)
        , m_path(path != nullptr ? path : "/status/logs")
        , m_entries{}
        , m_writeIndex(0)
        , m_count(0)
        , m_oldestIndex(0)
        , m_dirty(false)
        , m_lastPublishTime(0)
    {
    }

    void setPublishCallback(PublishCallback publishCallback)
    {
        m_publishCallback = publishCallback;
    }

    void setPath(const char* path)
    {
        m_path = (path != nullptr) ? path : "/status/logs";
    }

    void write(LogLevel level, const char* tag, const char* message) override
    {
        if (message == nullptr) {
            return;
        }

        if (m_count < MAX_LOGS) {
            appendEntry(level, tag, message);
            return;
        }

        overwriteEntry(level, tag, message);
    }

    void loop() override
    {
        if (!m_dirty) {
            return;
        }

        const unsigned long now = millis();
        if (m_lastPublishTime != 0 && now - m_lastPublishTime < FLUSH_INTERVAL_MS) {
            return;
        }

        if (m_publishCallback != nullptr) {
            if (m_publishCallback(m_path, buildPayloadBuffer())) {
                m_dirty = false;
                m_lastPublishTime = now;
            }
        }
    }

private:
    static constexpr size_t MAX_LOGS = 10;
    static constexpr size_t MAX_TAG_LENGTH = 16;
    static constexpr size_t MAX_MESSAGE_LENGTH = 64;
    static constexpr size_t PAYLOAD_BUFFER_SIZE = 1024;
    static constexpr unsigned long FLUSH_INTERVAL_MS = 10000UL;

    struct LogEntry
    {
        unsigned long timestamp;
        LogLevel level;
        char tag[MAX_TAG_LENGTH];
        char message[MAX_MESSAGE_LENGTH];
    };

    void appendEntry(LogLevel level, const char* tag, const char* message)
    {
        LogEntry& entry = m_entries[m_writeIndex];
        entry.timestamp = millis();
        entry.level = level;
        copyString(entry.tag, sizeof(entry.tag), tag != nullptr ? tag : "Logger");
        copyString(entry.message, sizeof(entry.message), message);
        m_writeIndex = (m_writeIndex + 1u) % MAX_LOGS;
        ++m_count;
        m_dirty = true;
    }

    void overwriteEntry(LogLevel level, const char* tag, const char* message)
    {
        const size_t slot = m_oldestIndex;
        LogEntry& entry = m_entries[slot];
        entry.timestamp = millis();
        entry.level = level;
        copyString(entry.tag, sizeof(entry.tag), tag != nullptr ? tag : "Logger");
        copyString(entry.message, sizeof(entry.message), message);
        m_oldestIndex = (m_oldestIndex + 1u) % MAX_LOGS;
        m_dirty = true;
    }

    static void copyString(char* destination, size_t destinationSize, const char* source)
    {
        if (destination == nullptr || destinationSize == 0) {
            return;
        }

        if (source == nullptr) {
            destination[0] = '\0';
            return;
        }

        const size_t length = strlen(source);
        const size_t toCopy = (length < destinationSize - 1) ? length : destinationSize - 1;
        if (toCopy > 0) {
            memcpy(destination, source, toCopy);
        }
        destination[toCopy] = '\0';
    }

    const char* buildPayloadBuffer()
    {
        size_t used = 0;
        used += snprintf(m_payloadBuffer + used, sizeof(m_payloadBuffer) - used, "{\"logs\":{");

        size_t index = m_oldestIndex;
        for (size_t entryIndex = 0; entryIndex < m_count; ++entryIndex) {
            const LogEntry& entry = m_entries[index];
            if (entryIndex > 0) {
                used += snprintf(m_payloadBuffer + used, sizeof(m_payloadBuffer) - used, ",");
            }

            char entryKey[24];
            snprintf(entryKey, sizeof(entryKey), "e%lu", entry.timestamp);
            used += snprintf(
                m_payloadBuffer + used,
                sizeof(m_payloadBuffer) - used,
                "\"%s\":{\"ts\":%lu,\"lvl\":\"%s\",\"tag\":\"%s\",\"msg\":\"%s\"}",
                entryKey,
                entry.timestamp,
                toString(entry.level),
                entry.tag,
                entry.message
            );
            index = (index + 1u) % MAX_LOGS;
        }

        used += snprintf(m_payloadBuffer + used, sizeof(m_payloadBuffer) - used, "}});
        return m_payloadBuffer;
    }

    PublishCallback m_publishCallback;
    const char* m_path;
    LogEntry m_entries[MAX_LOGS];
    size_t m_writeIndex;
    size_t m_count;
    size_t m_oldestIndex;
    bool m_dirty;
    unsigned long m_lastPublishTime;
    char m_payloadBuffer[PAYLOAD_BUFFER_SIZE];
};

#endif
