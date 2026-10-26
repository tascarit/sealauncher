#pragma once

#ifndef MODDEPENDENCYRESOLVER_H
#define MODDEPENDENCYRESOLVER_H

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QHash>
#include <QList>
#include <QString>
#include "modrinthapi.h"

struct ModDownloadTask {
    QString projectId;
    QString versionId;
    QString fileName;
    QString url;
    QString sha1;
    qint64 size = 0;
    bool userSelected = false;
};

class ModDependencyResolver : public QObject
{
    Q_OBJECT

public:
    explicit ModDependencyResolver(ModrinthApi* api, QObject* parent = nullptr);

    void resolve(const QStringList& projectIds, const QString& gameVersion, const QString& loader);
    void setIncludeOptional(bool v) { m_includeOptional = v; }
    void setProjectNames(const QHash<QString, QString>& names) { m_names = names; }

signals:
    void progress(const QString& stage, int done, int total);
    void warning(const QString& message);
    void resolved(const QList<ModDownloadTask>& tasks);
    void error(const QString& message);

private:
    void resolveProject(const QString& projectId, bool userSelected);
    void resolveVersion(const QString& versionId, bool userSelected);
    void addVersion(const QJsonObject& version, bool userSelected);
    void requestFinished();
    void failWith(const QString& message);
    QString displayName(const QString& projectId) const;
    static QJsonObject pickVersion(const QJsonArray& versions);

    ModrinthApi* m_api;
    QSet<QString> m_visitedProjects;
    QSet<QString> m_requestedVersions;
    QSet<QString> m_visitedVersions;
    QSet<QString> m_taskProjects;
    QHash<QString, QString> m_names;
    QList<ModDownloadTask> m_tasks;
    int m_pending = 0;
    bool m_includeOptional = false;
    bool m_finished = false;
    QString m_gameVersion;
    QString m_loader;
};

#endif