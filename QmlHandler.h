#pragma once

#ifndef QMLHANDLER_H
#define QMLHANDLER_H

#include <QObject>

class QmlHandler: public QObject {
    Q_OBJECT
public:
    explicit QmlHandler(QObject *parent = nullptr): QObject(parent) {}

signals:
    void installProgress(const QString& stage, const QString& details, double progress);
    void installError(const QString& title, const QString& message, const QString& details, bool canRetry);
};

#endif // QMLHANDLER_H
