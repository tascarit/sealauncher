#pragma once

#ifndef MODDEPENDENCYRESOLVER_H
#define MODDEPENDENCYRESOLVER_H

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QList>
#include <QString>
#include "modrinthapi.h"

struct ModDownloadTask {
    QString projectId;
    QString versionId;
    QString fileName;
    QString url;
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

signals:
    void progress(const QString& stage, int done, int total);
    void warning(const QString& message);
    void resolved(const QList<ModDownloadTask>& tasks);
    void error(const QString& message);

private:
    void resolveProject(const QString& projectId, bool userSelected);
    void resolveVersion(const QString& versionId, bool userSelected);
    void addVersion(const QJsonObject& version, bool userSelected);
    void finishIfDone();

    ModrinthApi* m_api;
    QSet<QString> m_visitedProjects;
    QSet<QString> m_visitedVersions;
    QList<ModDownloadTask> m_tasks;
    int m_pending = 0;
    bool m_includeOptional = false;
    QString m_gameVersion;
    QString m_loader;
    bool m_hasError = false;
};

#endif