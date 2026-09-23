#include "NewDebug.h"

std::string NewDebug::getCurrentTime(){
    auto now = std::chrono::system_clock::now();

    std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime;

    localtime_s(&localTime, &currentTime);

    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "%d.%m.%y", &localTime);

    return std::string(buffer);
}

std::string NewDebug::getHour(){
    auto now = std::chrono::system_clock::now();

    std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime;

    localtime_s(&localTime, &currentTime);

    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "[%H:%M]", &localTime);

    return std::string(buffer);
}

QString NewDebug::configPath(){
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return path + QString("/logs/log-") + getCurrentTime().c_str() + ".txt";
}