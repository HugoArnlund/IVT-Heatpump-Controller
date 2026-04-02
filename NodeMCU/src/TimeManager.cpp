#include "TimeManager.h"
#include <WiFiUdp.h>
#include <NTPClient.h>
#include "Config.h"
#include "Logger.h"

namespace TimeManager {
namespace {
    WiFiUDP ntpUDP;
    NTPClient timeClient(ntpUDP, Config::NTP_SERVER, Config::TIMEZONE_OFFSET, Config::NTP_UPDATE_INTERVAL);
}

void initialize() {
    timeClient.begin();
    Logger::log(Logger::LOG_INFO, "NTP client initialized", "TimeManager");
}

unsigned long long getCurrentUnixTime() {
    timeClient.update();
    return timeClient.getEpochTime();
}

} // namespace TimeManager
