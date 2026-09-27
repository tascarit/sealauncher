#include "moddependencyresolver.h"

ModDependencyResolver::ModDependencyResolver(ModrinthApi* api, QObject* parent)
    : QObject(parent), m_api(api)
{
}

void ModDependencyResolver::resolve(const QStringList& projectIds, const QString& gameVersion, const QString& loader)
{
    m_visitedProjects.clear();
    m_visitedVersions.clear();
    m_tasks.clear();
    m_pending = 0;
    m_gameVersion = gameVersion;
    m_loader = loader;
    m_hasError = false;

    if (projectIds.isEmpty()) {
        emit resolved(m_tasks);
        return;
    }

    for (const QString& id : projectIds)
        resolveProject(id, true);
}

void ModDependencyResolver::resolveProject(const QString& projectId,
                                           bool userSelected)
{
    if (m_visitedProjects.contains(projectId)) {
        if (userSelected) {
            for (ModDownloadTask& t : m_tasks) {
                if (t.projectId == projectId)
                    t.userSelected = true;
            }
        }
        return;
    }

    m_visitedProjects.insert(projectId);
    m_pending++;

    m_api->requestVersions(
        projectId,
        m_gameVersion,
        m_loader,
        [this, userSelected, projectId](const QJsonArray& versions) {

            if (versions.isEmpty()) {
                if (userSelected) {
                    m_hasError = true;

                    emit error(
                        QString("Мод %1 не имеет версии для Minecraft %2 / %3")
                            .arg(projectId)
                            .arg(m_gameVersion)
                            .arg(m_loader)
                        );
                } else {
                    emit warning(
                        QString("Dependency %1 has no compatible version for %2 / %3")
                            .arg(projectId)
                            .arg(m_gameVersion)
                            .arg(m_loader)
                        );
                }
            } else {
                addVersion(versions.first().toObject(), userSelected);
            }

            m_pending--;
            finishIfDone();
        }
        );
}

void ModDependencyResolver::resolveVersion(const QString& versionId, bool userSelected)
{
    if (m_visitedVersions.contains(versionId)) return;
    m_pending++;

    m_api->requestVersion(versionId, [this, userSelected](const QJsonObject& version) {
        if (!version.isEmpty()) addVersion(version, userSelected);
        m_pending--;
        finishIfDone();
    });
}

void ModDependencyResolver::finishIfDone()
{
    if (m_pending > 0)
        return;

    if (m_hasError)
        return;

    emit progress("Dependencies resolved",
                  m_tasks.size(),
                  m_tasks.size());

    emit resolved(m_tasks);
}

void ModDependencyResolver::addVersion(const QJsonObject& version, bool userSelected)
{
    const QString versionId = version["id"].toString();
    if (m_visitedVersions.contains(versionId)) return;
    m_visitedVersions.insert(versionId);

    const QJsonArray files = version["files"].toArray();
    QJsonObject primary;
    for (const QJsonValue& fv : files) {
        QJsonObject fo = fv.toObject();
        if (primary.isEmpty()) primary = fo;
        if (fo["primary"].toBool()) { primary = fo; break; }
    }
    if (primary.isEmpty()) return;

    ModDownloadTask task;
    task.projectId = version["project_id"].toString();
    task.versionId = versionId;
    task.fileName = primary["filename"].toString();
    task.url = primary["url"].toString();
    task.size = (qint64)primary["size"].toDouble();
    task.userSelected = userSelected;
    m_tasks.append(task);

    const QJsonArray deps = version["dependencies"].toArray();
    for (const QJsonValue& dv : deps) {
        const QJsonObject d = dv.toObject();
        const QString type = d["dependency_type"].toString();

        if (type == "embedded") continue;
        if (type == "incompatible") {
            emit warning("Incompatible with: " + d["project_id"].toString());
            continue;
        }
        if (type == "optional" && !m_includeOptional) continue;
        if (type != "required" && type != "optional") continue;

        const QString depVersionId = d["version_id"].toString();
        const QString depProjectId = d["project_id"].toString();

        if (!depVersionId.isEmpty()) resolveVersion(depVersionId, false);
        else if (!depProjectId.isEmpty()) resolveProject(depProjectId, false);
    }
}