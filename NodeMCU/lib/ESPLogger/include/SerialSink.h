#ifndef ESPLOGGER_SERIALSINK_H
#define ESPLOGGER_SERIALSINK_H

#include <Arduino.h>

#include "LogSink.h"

class SerialSink : public LogSink
{
public:
    void write(LogLevel, const char*, const char* message) override
    {
        if (message != nullptr) {
            Serial.println(message);
        }
    }
};

#endif