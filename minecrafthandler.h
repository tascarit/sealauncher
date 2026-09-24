#pragma once

#ifndef MINECRAFTHANDLER_H
#define MINECRAFTHANDLER_H

#include "QmlHandler.h"
#include "SettingsManager.h"

#include <QObject>
#include <QQuickItem>
#include <QWidget>
#include <QQuickView>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTimer>

class MinecraftHandler: public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool buildExists READ buildExists WRITE setBuildExists NOTIFY buildExistsChanged)
public:
    explicit MinecraftHandler(QObject *parent = nullptr);
    ~MinecraftHandler() override;

    bool buildExists() { return m_buildExists; }

    void setBuildExists(bool);

    void Initialize(QQuickView*, QmlHandler*, SettingsManager*);

    QString downloadNeoforge(const QString&);
    QString installNeoforge(const QString&);
    QString parseNeoforgeJson(const QString&);

    void ensureLauncherProfile(const QString& gameDir);

    Q_INVOKABLE void reCheckBuilds(const QString&, const QString&, const QString&);
    Q_INVOKABLE void mainButtonClick();

signals:
    void buildExistsChanged();

    void downloadProgress(qint64, qint64);
    void downloadFinished();
private:
    QQuickView *view;
    QmlHandler *q;
    QNetworkAccessManager *m_manager = nullptr;
    SettingsManager *sm = nullptr;

    bool m_buildExists;
    QString m_buildName;
    QString m_buildLoader;
    QString m_buildVersion;
    QFile *m_file;

    QTimer* m_progressThrottle = nullptr;
    QString m_pendingStage;
    QString m_pendingDetails;
    double  m_pendingProgress = 0.0;
    bool    m_hasPendingUpdate = false;
};

#endif // MINECRAFTHANDLER_H
