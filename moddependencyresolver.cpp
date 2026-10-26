#include "moddependencyresolver.h"

ModDependencyResolver::ModDependencyResolver(ModrinthApi* api, QObject* parent)
    : QObject(parent), m_api(api)
{
}

QString ModDependencyResolver::displayName(const QString& projectId) const
{
    const QString n = m_names.value(projectId);
    return n.isEmpty() ? projectId : n;
}

void ModDependencyResolver::resolve(const QStringList& projectIds, const QString& gameVersion, const QString& loader)
{
    m_visitedProjects.clear();
    m_requestedVersions.clear();
    m_visitedVersions.clear();
    m_taskProjects.clear();
    m_tasks.clear();
    m_pending = 0;
    m_finished = false;
    m_gameVersion = gameVersion;
    m_loader = loader;

    if (projectIds.isEmpty()) {
        m_finished = true;
        emit resolved(m_tasks);
        return;
    }

    for (const QString& id : projectIds)
        resolveProject(id, true);
}

QJsonObject ModDependencyResolver::pickVersion(const QJsonArray& versions)
{
    for (const QJsonValue& v : versions) {
        const QJsonObject o = v.toObject();
        if (o.value("version_type").toString() == "release") return o;
    }
    return versions.isEmpty() ? QJsonObject() : versions.first().toObject();
}

void ModDependencyResolver::failWith(const QString& message)
{
    if (m_finished) return;
    m_finished = true;
    emit error(message);
}

void ModDependencyResolver::resolveProject(const QString& projectId, bool userSelected)
{
    if (m_visitedProjects.contains(projectId) || m_taskProjects.contains(projectId)) {
        if (userSelected) {
            for (ModDownloadTask& t : m_tasks)
                if (t.projectId == projectId) t.userSelected = true;
        }
        return;
    }

    m_visitedProjects.insert(projectId);
    m_pending++;

    m_api->requestVersions(
        projectId, m_gameVersion, m_loader,
        [this, userSelected, projectId](const QJsonArray& versions) {
            if (m_finished) return;

            const QJsonObject chosen = pickVersion(versions);
            if (chosen.isEmpty()) {
                const QString msg = QString("Мод «%1» не имеет версии для Minecraft %2 (%3)")
                                        .arg(displayName(projectId), m_gameVersion, m_loader);
                if (userSelected) { failWith(msg); return; }
                emit warning("Зависимость пропущена: " + msg);
            } else {
                addVersion(chosen, userSelected);
            }
            requestFinished();
        },
        [this, userSelected, projectId](const QString& err) {
            if (m_finished) return;
            const QString msg = QString("Не удалось получить версии мода «%1»: %2").arg(displayName(projectId), err);
            if (userSelected) { failWith(msg); return; }
            emit warning(msg);
            requestFinished();
        },
        this);
}

void ModDependencyResolver::resolveVersion(const QString& versionId, bool userSelected)
{
    if (m_visitedVersions.contains(versionId) || m_requestedVersions.contains(versionId)) return;
    m_requestedVersions.insert(versionId);
    m_pending++;

    m_api->requestVersion(
        versionId,
        [this, userSelected](const QJsonObject& version) {
            if (m_finished) return;
            if (!version.isEmpty()) addVersion(version, userSelected);
            requestFinished();
        },
        [this](const QString& err) {
            if (m_finished) return;
            emit warning("Не удалось получить версию зависимости: " + err);
            requestFinished();
        },
        this);
}

void ModDependencyResolver::requestFinished()
{
    m_pending--;
    if (m_finished || m_pending > 0) return;

    m_finished = true;
    emit progress("Зависимости найдены", m_tasks.size(), m_tasks.size());
    emit resolved(m_tasks);
}

void ModDependencyResolver::addVersion(const QJsonObject& version, bool userSelected)
{
    const QString versionId = version["id"].toString();
    if (m_visitedVersions.contains(versionId)) return;
    m_visitedVersions.insert(versionId);

    const QString projectId = version["project_id"].toString();

    if (!projectId.isEmpty() && m_taskProjects.contains(projectId)) {
        if (userSelected)
            for (ModDownloadTask& t : m_tasks)
                if (t.projectId == projectId) t.userSelected = true;
        return;
    }

    const QJsonArray files = version["files"].toArray();
    QJsonObject primary;
    for (const QJsonValue& fv : files) {
        const QJsonObject fo = fv.toObject();
        if (primary.isEmpty()) primary = fo;
        if (fo["primary"].toBool()) { primary = fo; break; }
    }
    if (primary.isEmpty()) return;

    ModDownloadTask task;
    task.projectId = projectId;
    task.versionId = versionId;
    task.fileName = primary["filename"].toString();
    task.url = primary["url"].toString();
    task.sha1 = primary["hashes"].toObject()["sha1"].toString();
    task.size = (qint64)primary["size"].toDouble();
    task.userSelected = userSelected;
    m_tasks.append(task);
    if (!projectId.isEmpty()) m_taskProjects.insert(projectId);

    emit progress("Поиск зависимостей", m_tasks.size(), m_tasks.size() + m_pending);

    const QJsonArray deps = version["dependencies"].toArray();
    for (const QJsonValue& dv : deps) {
        const QJsonObject d = dv.toObject();
        const QString type = d["dependency_type"].toString();

        if (type == "embedded") continue;
        if (type == "incompatible") {
            emit warning("Несовместим с: " + displayName(d["project_id"].toString()));
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