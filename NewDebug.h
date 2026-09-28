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
    static void writeLine(const QString& line);

public:
    NewDebug() {
        buffer += QString::fromStdString(getHour()) + " ";
    }

    ~NewDebug() {
        writeLine(buffer);
        qDebug().noquote() << buffer;
    }

    static QString configPath();
    static std::string getCurrentTime();
    static std::string getHour();

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
