#ifndef ESPLOGGER_SERIALSINK_H
#define ESPLOGGER_SERIALSINK_H

#include <Arduino.h>

#include "LogSink.h"

class SerialSink : public LogSink
{
public:
    void write(LogLevel level, const char* tag, const char* message) override
    {
        if (message == nullptr) {
            return;
        }

        const char* levelStr = toString(level);
        const char* tagStr = tag != nullptr ? tag : "Logger";

        Serial.printf(
            "[%8lu] %-5s %-12s %s\n",
            millis(),
            levelStr,
            tagStr,
            message
        );
    }
};

#endif