#pragma once

#ifndef MINECRAFTHANDLER_H
#define MINECRAFTHANDLER_H

#include "QmlHandler.h"
#include "SettingsManager.h"
#include "SettingsController.h"
#include "downloadmanager.h"

#include <QObject>
#include <QQuickItem>
#include <QWidget>
#include <QQuickView>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTimer>
#include <QProcess>
#include <QElapsedTimer>
#include <QDesktopServices>

#include <functional>
#include <memory>

class MinecraftHandler: public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool buildExists READ buildExists NOTIFY buildExistsChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateAvailableChanged)
    Q_PROPERTY(bool installation READ installation NOTIFY installationChanged)
    Q_PROPERTY(bool canDelete READ canDelete NOTIFY canDeleteChanged)
    Q_PROPERTY(QString installedPackVersion READ installedPackVersion NOTIFY packVersionsChanged)
    Q_PROPERTY(QString selectedPackVersion READ selectedPackVersion NOTIFY packVersionsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool offline READ offline NOTIFY offlineChanged)

public:
    explicit MinecraftHandler(QObject *parent = nullptr);
    ~MinecraftHandler() override;

    bool buildExists() const { return m_buildExists; }
    bool updateAvailable() const { return m_updateAvailable; }
    bool installation() const { return m_installation; }
    bool canDelete() const { return m_canDelete; }
    bool busy() const { return m_busy; }
    QString installedPackVersion() const { return m_shownInstalled; }
    QString selectedPackVersion() const { return m_shownSelected; }
    bool offline() const { return m_offline; }

    void Initialize(QQuickView*, QmlHandler*, SettingsManager*, SettingsController*);

    QString getVersionId() const;
    QStringList parseVersionJson(const QString& gamePath);
    bool launchMinecraft(const QStringList&, const QString&);
    void ensureLauncherProfile(const QString& gameDir);
    static bool isJvmArgSupportedFor(const QString& arg, int javaMajor);

    Q_INVOKABLE void mainButtonClick();
    Q_INVOKABLE void deleteCurrentBuild();
    Q_INVOKABLE void fetchBuildsList();
    Q_INVOKABLE void fetchNews();
    Q_INVOKABLE void openLauncherDir();
    Q_INVOKABLE void openBuildDir();
    Q_INVOKABLE void reCheckBuilds(const QString& build, const QString& version, const QString& loader,
                                   const QString& mcVer, const QString& git, const int parts, const QString& packVer);
    Q_INVOKABLE void installLocalBuild(const QString& buildName, const QString& mcVersion,
                                       const QString& loader, const QString& loaderVersion);

    Q_INVOKABLE void refreshState();
    Q_INVOKABLE void cancelInstallation();
    Q_INVOKABLE void retryLastOperation();
    Q_INVOKABLE void discardFailedOperation();

signals:
    void buildExistsChanged();
    void updateAvailableChanged();
    void installationChanged();
    void canDeleteChanged();
    void busyChanged();
    void packVersionsChanged();
    void offlineChanged();

    void buildsListReady(const QJsonArray& builds);
    void newsReady(const QJsonArray& news);
    void buildsChanged();
    void minecraftStarted();
    void minecraftStopped();

    void installSucceeded(const QString& name);
    void installFailed(const QString& name, const QString& message, bool canRetry);
    void installAbandoned(const QString& name);

private:
    using Cont = std::function<void()>;
    using FetchCb = std::function<void(bool ok, const QByteArray& data, const QString& error)>;

    struct BuildSpec {
        QString name;
        QString loader;
        QString loaderVersion;
        QString mcVersion;
        QString archiveLink;
        QString packVersion = QStringLiteral("0.1");
        int parts = 1;
    };
    struct FetchCtx;
    struct MergeState;
    struct ExtractState;

    void refreshSelectedState();
    void setBuildExists(bool v);
    void setUpdateAvailable(bool v);
    void setInstallation(bool v);
    void setCanDelete(bool v);
    void setBusy(bool v);
    void setOffline(bool v);

    QString gamePathFor(const QString& name) const;
    static QString versionIdFor(const BuildSpec& s);
    QString readInstalledPackVersion(const QString& gamePath) const;
    bool loaderAlreadyInstalled(const QString& gamePath, const BuildSpec& spec) const;
    bool writeBuildMeta(const QString& gamePath, const BuildSpec& spec);
    void prepareForUpdate(const QString& gamePath, bool keepLoader);
    void writeInstallMarker(const QString& gamePath) const;
    void removeInstallMarker(const QString& gamePath) const;

    void beginInstall(const BuildSpec& spec, bool update);
    void endJob();
    void failJob(const QString& title, const QString& message, const QString& details, bool canRetry);
    void finishInstall();
    void defer(quint64 gen, std::function<void()> fn);
    void reportProgress(const QString& stage, const QString& details, double progress, bool immediate = false);
    void flushProgress();

    void stageArchive();
    void downloadNextPart();
    void mergeParts();
    void mergeStep();
    void stagePrepare();
    void stageLoader();
    void stageVanilla(const Cont& next);
    void stageVanillaFiles(const Cont& next);
    void stageAssets(const QJsonObject& vanillaJson, const Cont& next);
    void stageFabric();
    void stageForge();
    void stageNeoforge();
    void stageExtract();
    void startExtract(const QString& zipPath, const QString& targetDir);
    void extractStep();
    void discardArchive();

    void runBatch(const QString& title, const QList<DownloadItem>& items, int parallel, const Cont& onOk);
    void runInstaller(const QString& installerPath, const QString& label, const Cont& onSuccess);
    QList<DownloadItem> libraryItems(const QJsonObject& json, const QString& gamePath) const;

    void fetchText(const QString& url, int retries, bool bindToJob, FetchCb cb);
    void fetchAttempt(const std::shared_ptr<FetchCtx>& ctx);
    void scheduleFetchRetry();

    QByteArray loadCache(const QString& fileName) const;
    void saveCache(const QString& fileName, const QByteArray& data) const;

    QQuickView *view = nullptr;
    QmlHandler *q = nullptr;
    QNetworkAccessManager *m_manager = nullptr;
    SettingsManager *sm = nullptr;
    SettingsController *sc = nullptr;

    BuildSpec m_sel;
    QString m_installedPackVersion;
    QString m_shownInstalled;
    QString m_shownSelected;
    bool m_buildExists = false;
    bool m_updateAvailable = false;
    bool m_installation = false;
    bool m_canDelete = false;
    bool m_busy = false;
    bool m_offline = false;

    BuildSpec m_job;
    QString m_jobPath;
    QString m_tmpDir;
    QString m_zipPath;
    QStringList m_partPaths;
    QStringList m_partUrls;
    int m_partIndex = 0;
    bool m_jobUpdate = false;
    bool m_jobSkipLoader = false;
    bool m_installing = false;
    int m_installerAttempt = 0;
    quint64 m_opGen = 1;

    DownloadBatch* m_batch = nullptr;
    RangedDownload* m_ranged = nullptr;
    QProcess* m_installerProc = nullptr;
    std::shared_ptr<MergeState> m_merge;
    std::shared_ptr<ExtractState> m_extract;

    std::function<void()> m_retryFn;
    QString m_failedJobName;

    QTimer* m_progressThrottle = nullptr;
    QElapsedTimer m_progressClock;
    QString m_pendingStage;
    QString m_pendingDetails;
    double m_pendingProgress = -1.0;
    bool m_hasPendingUpdate = false;

    bool m_fetchingBuilds = false;
    bool m_fetchingNews = false;
    QTimer* m_fetchRetryTimer = nullptr;

    QProcess* m_minecraftProcess = nullptr;
    QString m_launchJavaPath;
};

#endif