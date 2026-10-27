#pragma once

#ifndef LOCALBUILDSMANAGER_H
#define LOCALBUILDSMANAGER_H

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QNetworkAccessManager>
#include <QPointer>
#include <functional>
#include <memory>
#include "modrinthapi.h"
#include "moddependencyresolver.h"
#include "downloadmanager.h"

class SettingsManager;
class MinecraftHandler;

class LocalBuildsManager : public QObject
{
    Q_OBJECT

public:
    explicit LocalBuildsManager(SettingsManager* sm, ModrinthApi* api, QObject* parent = nullptr);
    void setMinecraftHandler(MinecraftHandler* mh);

    QNetworkRequest mkRequest(const QString& url) const;
    void requestForgeVersions(const QString& mcVersion, quint64 seq);
    void requestForgePromotions(const QString& mcVersion, quint64 seq);

    Q_INVOKABLE QJsonArray buildsArray() const;
    Q_INVOKABLE bool contains(const QString& name) const;
    Q_INVOKABLE void createBuild(const QString& name, const QString& mcVersion, const QString& loader,
                                 const QString& loaderVersion, const QJsonArray& mods);
    Q_INVOKABLE void removeBuild(const QString& name);
    Q_INVOKABLE void requestLoaderVersions(const QString& loader, const QString& mcVersion);
    Q_INVOKABLE void requestMinecraftVersions();
    Q_INVOKABLE void deleteBuild(const QString& name);
    Q_INVOKABLE void editBuild(const QString& oldName, const QString& newName,
                               const QString& mcVersion, const QString& loader,
                               const QString& loaderVersion, const QJsonArray& mods);
    Q_INVOKABLE QJsonArray getInstalledModsForBuild(const QString& name) const;
    Q_INVOKABLE void cancelCreation();
    Q_INVOKABLE void repairBuild(const QString& name);
    Q_INVOKABLE void setReservedNames(const QStringList& names);
    Q_INVOKABLE bool isBusy() const { return m_opActive; }

signals:
    void buildsChanged();
    void loaderVersionsReady(const QStringList& versions);
    void creationProgress(const QString& stage, const QString& details, double progress);
    void creationFinished(const QString& name);
    void creationError(const QString& message);
    void creationCancelled(const QString& name);
    void minecraftVersionsReady(const QStringList& versions);

private:
    struct Entry {
        QString name;
        QString mcVersion;
        QString loader;
        QString loaderVersion;
        QStringList projectIds;
        QStringList projectNames;
        bool pendingSync = false;
    };

    void load();
    void save();
    QString configPath() const;
    Entry findEntry(const QString& name) const;
    int indexOf(const QString& name) const;
    QString readMetaPackVersion(const QString& gamePath) const;
    bool cleanRuntime(const QString& buildPath);
    bool isNameSafe(const QString& name) const;
    bool nameConflict(const QString& name) const;

    void beginPipeline(const QString& name, bool installLoader, bool isCreate);
    void onResolved(const QList<ModDownloadTask>& tasks);
    void modsDone();
    void startLoaderInstall();
    void finishOperation();
    void failOperation(const QString& message, bool cancelled = false);
    void clearOperation();
    void rollbackCreate(const QString& name);
    void onInstallSucceeded(const QString& name);
    void onInstallFailed(const QString& name, const QString& message, bool canRetry);
    void onInstallAbandoned(const QString& name);

    struct FetchBytesCtx;
    void fetchBytes(const QString& url, int retries, std::function<void(bool, const QByteArray&)> cb);
    void fetchBytesAttempt(const std::shared_ptr<FetchBytesCtx>& ctx);
    void emitMinecraftVersions(const QStringList& list);

    SettingsManager* m_sm = nullptr;
    ModrinthApi* m_api = nullptr;
    MinecraftHandler* m_mh = nullptr;
    QNetworkAccessManager* m_net = nullptr;
    QList<Entry> m_entries;

    bool m_opActive = false;
    bool m_isCreate = false;
    bool m_installLoaderAfterMods = true;
    bool m_awaitingLoader = false;
    QString m_currentBuild;
    ModDependencyResolver* m_resolver = nullptr;
    DownloadBatch* m_modsBatch = nullptr;

    QStringList m_reserved;
    quint64 m_loaderReqSeq = 0;
    QStringList m_mcVersionsCache;
};

#endif