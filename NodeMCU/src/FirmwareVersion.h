#pragma once

#include <stdint.h>

#ifndef FW_VERSION
#define FW_VERSION 0
#endif

inline uint64_t getFirmwareVersion()
{
    return static_cast<uint64_t>(FW_VERSION);
}