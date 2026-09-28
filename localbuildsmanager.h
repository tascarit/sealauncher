#pragma once

#ifndef LOCALBUILDSMANAGER_H
#define LOCALBUILDSMANAGER_H

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QNetworkAccessManager>
#include "modrinthapi.h"
#include "moddependencyresolver.h"

class SettingsManager;
class MinecraftHandler;

class LocalBuildsManager : public QObject
{
    Q_OBJECT

public:
    explicit LocalBuildsManager(SettingsManager* sm, ModrinthApi* api, QObject* parent = nullptr);
    void setMinecraftHandler(MinecraftHandler* mh) { m_mh = mh; }

    QNetworkRequest mkRequest(const QString& url) const;
    void requestForgeVersions(const QString& mcVersion);
    void requestForgePromotions(const QString& mcVersion);

    Q_INVOKABLE QJsonArray buildsArray() const;
    Q_INVOKABLE bool contains(const QString& name) const;
    Q_INVOKABLE void createBuild(const QString& name, const QString& mcVersion, const QString& loader, const QString& loaderVersion, const QJsonArray& mods);
    Q_INVOKABLE void removeBuild(const QString& name);
    Q_INVOKABLE void requestLoaderVersions(const QString& loader, const QString& mcVersion);
    Q_INVOKABLE void requestMinecraftVersions();
    Q_INVOKABLE void deleteBuild(const QString& name);
    Q_INVOKABLE void editBuild(const QString& oldName, const QString& newName,
                               const QString& mcVersion, const QString& loader,
                               const QString& loaderVersion, const QJsonArray& mods);
    Q_INVOKABLE QJsonArray getInstalledModsForBuild(const QString& name) const;

signals:
    void buildsChanged();
    void loaderVersionsReady(const QStringList& versions);
    void creationProgress(const QString& stage, const QString& details, double progress);
    void creationFinished(const QString& name);
    void creationError(const QString& message);
    void minecraftVersionsReady(const QStringList& versions);

private:
    struct Entry {
        QString name;
        QString mcVersion;
        QString loader;
        QString loaderVersion;
        QStringList projectIds;
        QStringList projectNames;
    };

    void load();
    void save();
    QString configPath() const;
    Entry findEntry(const QString& name) const;
    QString readMetaPackVersion(const QString& gamePath) const;

    SettingsManager* m_sm = nullptr;
    ModrinthApi* m_api = nullptr;
    MinecraftHandler* m_mh = nullptr;
    QNetworkAccessManager* m_net = nullptr;
    QList<Entry> m_entries;

    QList<ModDownloadTask> m_queue;
    int m_queueIndex = 0;
    QString m_currentBuild;
    QMetaObject::Connection m_finishedConn;

    void startModDownloadQueue(const QString& name, bool installLoaderAfterMods);
    void downloadNextMod();
    void startLoaderInstall(const QString& name);
    bool cleanRuntime(const QString& buildPath);

    bool m_installLoaderAfterMods = true;
};

#endif
