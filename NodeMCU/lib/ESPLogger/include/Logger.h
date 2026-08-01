#ifndef ESPLOGGER_LOGGER_H
#define ESPLOGGER_LOGGER_H

#include <stdarg.h>
#include <stddef.h>

#include "ESPLoggerConfig.h"
#include "LogSink.h"

struct SinkRegistration
{
    LogSink* sink;
    LogLevel minimumLevel;
};

class Logger
{
public:
    Logger();

    bool addSink(LogSink* sink, LogLevel minimumLevel);
    bool removeSink(LogSink* sink);
    void clearSinks();
    size_t sinkCount() const;

    void logf(LogLevel level, const char* tag, const char* fmt, ...);

    void trace(const char* tag, const char* fmt, ...);
    void debug(const char* tag, const char* fmt, ...);
    void info(const char* tag, const char* fmt, ...);
    void warn(const char* tag, const char* fmt, ...);
    void error(const char* tag, const char* fmt, ...);
    void fatal(const char* tag, const char* fmt, ...);

    void loop();

private:
    void logv(LogLevel level, const char* tag, const char* fmt, va_list args);
    bool hasSinkFor(LogLevel level) const;

    char m_buffer[LOG_BUFFER_SIZE];
    SinkRegistration m_sinks[MAX_SINKS];
    size_t m_sinkCount;
};

extern Logger ESPLogger;

#define LOGT(tag, fmt, ...) ESPLogger.trace(tag, fmt, ##__VA_ARGS__)
#define LOGD(tag, fmt, ...) ESPLogger.debug(tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) ESPLogger.info(tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) ESPLogger.warn(tag, fmt, ##__VA_ARGS__)
#define LOGE(tag, fmt, ...) ESPLogger.error(tag, fmt, ##__VA_ARGS__)
#define LOGF(tag, fmt, ...) ESPLogger.fatal(tag, fmt, ##__VA_ARGS__)

#endif