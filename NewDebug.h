#pragma once
#ifndef NEWDEBUG_H
#define NEWDEBUG_H

#include <QString>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <chrono>

class NewDebug {
private:
    QString buffer;

public:
    NewDebug() {
        buffer += QString::fromStdString(getHour()) + " ";
    }

    ~NewDebug() {
        const QString path = configPath();
        const QString dir = QFileInfo(path).absolutePath();

        QDir().mkpath(dir);
        QFile f(path);

        if (!f.open(QIODeviceBase::Append | QIODeviceBase::Text)){
            qDebug() << "ERROR: Failed to open log file";
        } else {
            QTextStream out(&f);
            out << buffer << "\n";
        }
        f.close();

        qDebug().noquote() << buffer;
    }

    QString configPath();
    std::string getCurrentTime();
    std::string getHour();

    template <typename T>
    NewDebug& operator<<(const T& value) {
        QTextStream stream(&buffer);
        stream << value;
        return *this;
    }
};

inline NewDebug newDebug() {
    return NewDebug();
}

#endif
