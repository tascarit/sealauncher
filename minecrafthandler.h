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
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QHash>
#include <QThread>
#include <QDesktopServices>

#include <private/qzipreader_p.h>

struct Chunk { qint64 start; qint64 end; };

class MinecraftHandler: public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool buildExists READ buildExists WRITE setBuildExists NOTIFY buildExistsChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable WRITE setUpdateAvailable NOTIFY updateAvailableChanged)
    Q_PROPERTY(bool installation READ installation WRITE setInstallation NOTIFY installationChanged)
public:
    explicit MinecraftHandler(QObject *parent = nullptr);
    ~MinecraftHandler() override;

    bool buildExists() const { return m_buildExists; }
    bool updateAvailable() const { return m_updateAvailable; }
    bool installation () const { return m_installation; }

    void setBuildExists(bool);

    void Initialize(QQuickView*, QmlHandler*, SettingsManager*, SettingsController*);

    QString downloadNeoforge(const QString&);
    QString installNeoforge(const QString&);

    void installVanilla(const QString& mcVersion);
    void installFabric(const QString& mcVersion, const QString& loaderVersion);
    void installForge(const QString& mcVersion, const QString& forgeVersion);

    void downloadVanillaFiles(const QString& gamePath, const QString& mcVersion);
    void downloadLibrariesFromJson(const QJsonObject& json, const QString& gamePath);
    void downloadVanillaLibraries(const QString& gamePath);
    void downloadAssets(const QString& gamePath, const QString& assetIndexId);
    void downloadFileParallel(const QString& url, const QString& savePath, int chunkSizeMB = 16, int maxParallel = 8);
    void downloadSingleStream(const QString& url, const QString& savePath, QNetworkAccessManager* mgr);
    void downloadChunks(QNetworkAccessManager* mgr, const QString& url, const QString& savePath, const QList<Chunk>& chunks, int maxParallel);
    bool mergeFiles(const QStringList& parts, const QString& outputPath);
    bool extractZip(const QString& zipPath, const QString& targetDir);
    void installModpack(const QString& baseUrl, const QString& gamePath, int partCount = 1);
    void onAllPartsDownloaded(const QStringList& partPaths, const QString& gamePath, int partCount);
    QString getVersionId() const;
    QString getVersionJsonPath(const QString&) const;

    void ensureLauncherProfile(const QString& gameDir);
    void ensureNarratorDisabled(const QString& gamePath);

    void startInstallation(const QString& gamePath);
    void stopInstallation(const QString& gamePath);
    void checkInstallation(const QString& gamePath);

    QStringList parseVersionJson(const QString&);
    bool launchMinecraft(const QStringList&, const QString&);

    Q_INVOKABLE void mainButtonClick();
    Q_INVOKABLE void deleteCurrentBuild();
    Q_INVOKABLE void fetchBuildsList();
    Q_INVOKABLE void fetchNews();
    Q_INVOKABLE void openLauncherDir();
    Q_INVOKABLE void reCheckBuilds(const QString& build, const QString& version, const QString& loader, const QString& mcVer, const QString& git, const int parts, const QString& packVer);
    Q_INVOKABLE void installLocalBuild(const QString& buildName, const QString& mcVersion, const QString& loader, const QString& loaderVersion);

signals:
    void buildExistsChanged();
    void updateAvailableChanged();
    void installationChanged();

    void downloadProgress(qint64, qint64);
    void downloadFinished(const QString& path);
    void finished();

    void installFinished();
    void downloadFailed(const QString& path, const QString& reason);

    void buildsListReady(const QJsonArray& builds);
    void newsReady(const QJsonArray& news);
    void minecraftStarted();
    void minecraftStopped();
private:
    QQuickView *view;
    QmlHandler *q;
    QNetworkAccessManager *m_manager = nullptr;
    SettingsManager *sm = nullptr;
    SettingsController *sc = nullptr;

    bool m_buildExists;
    bool m_installation;
    int m_javaMajorVersion = -1;
    QString m_buildName;
    QString m_buildLoader;
    QString m_buildVersion;
    QString m_mcVersion;
    QString m_buildArchiveLink;
    int m_archiveParts;
    QFile *m_file;
    QString m_launchJavaPath;

    QProcess* m_minecraftProcess = nullptr;
    QTimer* m_progressThrottle = nullptr;
    QString m_pendingStage;
    QString m_pendingDetails;
    double  m_pendingProgress = 0.0;
    bool    m_hasPendingUpdate = false;

    bool m_updateAvailable = false;

    QString m_packVersion;
    QString m_installedPackVersion;

    QString readInstalledPackVersion(const QString& gamePath) const;
    bool loaderAlreadyInstalled(const QString& gamePath) const;
    void setUpdateAvailable(bool v);
    void setInstallation(bool v);

    void finalizeInstall(const QString& gamePath);

    void writeBuildMeta(const QString& gamePath);
    void prepareForUpdate(const QString& gamePath);

    QString fetchUrl(const QString& url, int timeoutMs = 15000, int retries = 3);
    bool downloadFile(const QString& url, const QString& savePath, int timeoutMs = 20000, int retries = 3);
    QString getVersionJsonPath(const QString& gamePath, const QString& versionId);

    int detectJavaMajorVersion();
    static bool isJvmArgSupported(const QString& arg, int javaMajor);
};

#endif // MINECRAFTHANDLER_H
