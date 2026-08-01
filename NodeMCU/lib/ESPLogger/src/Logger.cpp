#include "Logger.h"

#include <Arduino.h>

#include <stdio.h>
#include <string.h>

Logger ESPLogger;


Logger::Logger()
    : m_buffer{0}
    , m_sinks{}
    , m_sinkCount(0)
{
}

bool Logger::addSink(LogSink* sink, LogLevel minimumLevel)
{
    if (sink == nullptr || m_sinkCount >= MAX_SINKS) {
        return false;
    }

    m_sinks[m_sinkCount].sink = sink;
    m_sinks[m_sinkCount].minimumLevel = minimumLevel;
    ++m_sinkCount;
    return true;
}

bool Logger::removeSink(LogSink* sink)
{
    if (sink == nullptr) {
        return false;
    }

    for (size_t index = 0; index < m_sinkCount; ++index) {
        if (m_sinks[index].sink == sink) {
            for (size_t moveIndex = index; moveIndex + 1 < m_sinkCount; ++moveIndex) {
                m_sinks[moveIndex] = m_sinks[moveIndex + 1];
            }

            --m_sinkCount;
            m_sinks[m_sinkCount].sink = nullptr;
            m_sinks[m_sinkCount].minimumLevel = LogLevel::INFO;
            return true;
        }
    }

    return false;
}

void Logger::clearSinks()
{
    for (size_t index = 0; index < m_sinkCount; ++index) {
        m_sinks[index].sink = nullptr;
        m_sinks[index].minimumLevel = LogLevel::INFO;
    }

    m_sinkCount = 0;
}

size_t Logger::sinkCount() const
{
    return m_sinkCount;
}

bool Logger::hasSinkFor(LogLevel level) const
{
    for (size_t index = 0; index < m_sinkCount; ++index) {
        if (m_sinks[index].sink != nullptr && static_cast<uint8_t>(level) >= static_cast<uint8_t>(m_sinks[index].minimumLevel)) {
            return true;
        }
    }

    return false;
}

void Logger::logf(LogLevel level, const char* tag, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    logv(level, tag, fmt, args);
    va_end(args);
}

void Logger::trace(const char* tag, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    logv(LogLevel::TRACE, tag, fmt, args);
    va_end(args);
}

void Logger::debug(const char* tag, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    logv(LogLevel::DEBUG, tag, fmt, args);
    va_end(args);
}

void Logger::info(const char* tag, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    logv(LogLevel::INFO, tag, fmt, args);
    va_end(args);
}

void Logger::warn(const char* tag, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    logv(LogLevel::WARN, tag, fmt, args);
    va_end(args);
}

void Logger::error(const char* tag, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    logv(LogLevel::ERROR, tag, fmt, args);
    va_end(args);
}

void Logger::fatal(const char* tag, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    logv(LogLevel::FATAL, tag, fmt, args);
    va_end(args);
}

void Logger::logv(LogLevel level, const char* tag, const char* fmt, va_list args)
{
    if (fmt == nullptr || !hasSinkFor(level)) {
        return;
    }

    m_buffer[0] = '\0';

    const int result = vsnprintf(m_buffer, sizeof(m_buffer), fmt, args);
    if (result < 0) {
        m_buffer[0] = '\0';
        return;
    }

    for (size_t index = 0; index < m_sinkCount; ++index) {
        SinkRegistration& registration = m_sinks[index];
        if (registration.sink != nullptr && static_cast<uint8_t>(level) >= static_cast<uint8_t>(registration.minimumLevel)) {
            char formattedBuffer[LOG_BUFFER_SIZE];
            registration.sink->write(level, tag, m_buffer);
        }
    }
}

void Logger::loop()
{
    for (size_t index = 0; index < m_sinkCount; ++index) {
        if (m_sinks[index].sink != nullptr) {
            m_sinks[index].sink->loop();
        }
    }
}