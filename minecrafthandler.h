#pragma once

#ifndef MINECRAFTHANDLER_H
#define MINECRAFTHANDLER_H

#include "QmlHandler.h"
#include "SettingsManager.h"
#include "SettingsController.h"

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
#include <QJsonArray>
#include <QProcess>
#include <QTextStream>
#include <QSemaphore>

#include <private/qzipreader_p.h>

struct Chunk { qint64 start; qint64 end; };

class MinecraftHandler: public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool buildExists READ buildExists WRITE setBuildExists NOTIFY buildExistsChanged)
public:
    explicit MinecraftHandler(QObject *parent = nullptr);
    ~MinecraftHandler() override;

    bool buildExists() { return m_buildExists; }

    void setBuildExists(bool);

    void Initialize(QQuickView*, QmlHandler*, SettingsManager*, SettingsController*);

    QString downloadNeoforge(const QString&);
    QString installNeoforge(const QString&);
    QStringList parseNeoforgeJson(const QString&);
    void downloadVanillaLibraries(const QString& gamePath);
    void downloadAssets(const QString& gamePath, const QString& assetIndexId);
    void downloadFileParallel(const QString& url, const QString& savePath, int chunkSizeMB = 16, int maxParallel = 8);
    void downloadSingleStream(const QString& url, const QString& savePath, QNetworkAccessManager* mgr);
    void downloadChunks(QNetworkAccessManager* mgr, const QString& url, const QString& savePath, const QList<Chunk>& chunks, int maxParallel);
    bool mergeFiles(const QStringList& parts, const QString& outputPath);
    bool extractZip(const QString& zipPath, const QString& targetDir);
    void installModpack(const QString& baseUrl, const QString& gamePath, int partCount = 1);
    void onAllPartsDownloaded(const QStringList& partPaths, const QString& gamePath, int partCount);

    void ensureLauncherProfile(const QString& gameDir);
    void ensureNarratorDisabled(const QString& gamePath);

    bool launchMinecraft(const QStringList&, const QString&);

    Q_INVOKABLE void reCheckBuilds(const QString&, const QString&, const QString&, const QString&, const QString&, const int);
    Q_INVOKABLE void mainButtonClick();

signals:
    void buildExistsChanged();

    void downloadProgress(qint64, qint64);
    void downloadFinished();
    void finished();

    void installFinished();
private:
    QQuickView *view;
    QmlHandler *q;
    QNetworkAccessManager *m_manager = nullptr;
    SettingsManager *sm = nullptr;
    SettingsController *sc = nullptr;

    bool m_buildExists;
    QString m_buildName;
    QString m_buildLoader;
    QString m_buildVersion;
    QString m_mcVersion;
    QString m_buildArchiveLink;
    int m_archiveParts;
    QFile *m_file;

    QProcess* m_minecraftProcess = nullptr;
    QTimer* m_progressThrottle = nullptr;
    QString m_pendingStage;
    QString m_pendingDetails;
    double  m_pendingProgress = 0.0;
    bool    m_hasPendingUpdate = false;
};

#endif // MINECRAFTHANDLER_H
