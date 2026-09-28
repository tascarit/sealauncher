#include "NewDebug.h"
#include <QMutex>
#include <QMutexLocker>

void NewDebug::writeLine(const QString& line)
{
    static QMutex mutex;
    static QFile file;
    static QString cachedDateKey;

    QMutexLocker locker(&mutex);

    const QString todayKey = QString::fromStdString(getCurrentTime());
    if (!file.isOpen() || cachedDateKey != todayKey) {
        if (file.isOpen())
            file.close();

        cachedDateKey = todayKey;
        const QString path = configPath();
        const QString dir = QFileInfo(path).absolutePath();
        QDir().mkpath(dir);

        file.setFileName(path);
        if (!file.open(QIODeviceBase::Append | QIODeviceBase::Text)) {
            qDebug() << "ERROR: Failed to open log file";
            return;
        }
    }

    QTextStream out(&file);
    out << line << "\n";
    out.flush();
}

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
