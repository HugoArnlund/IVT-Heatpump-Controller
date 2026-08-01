#ifndef ESPLOGGER_LOGSINK_H
#define ESPLOGGER_LOGSINK_H

#include "LogLevel.h"

class LogSink
{
public:
    virtual void write(LogLevel level, const char* tag, const char* message) = 0;

    virtual void loop()
    {
    }

    virtual ~LogSink() {}
};

#endif