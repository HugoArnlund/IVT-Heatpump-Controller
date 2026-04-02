#pragma once
#include <Arduino.h>

namespace Logger {

enum LogLevel {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_CRITICAL
};

void initialize();
void log(LogLevel level, const String &message, const String &tag = "");
void setWebhook(const String &webhookUrl);
void flushInfoBuffer();

} // namespace Logger
