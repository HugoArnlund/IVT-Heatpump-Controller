#ifndef ESPLOGGER_LOGLEVEL_H
#define ESPLOGGER_LOGLEVEL_H

#include <stdint.h>

enum class LogLevel : uint8_t
{
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    FATAL = 5
};

constexpr const char* toString(LogLevel level)
{
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARN: return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default: return "INFO";
    }
}

#endif