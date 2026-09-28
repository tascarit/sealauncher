#include "localbuildsmanager.h"
#include "SettingsManager.h"
#include "minecrafthandler.h"
#include "NewDebug.h"

#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QXmlStreamReader>

LocalBuildsManager::LocalBuildsManager(SettingsManager* sm, ModrinthApi* api, QObject* parent)
    : QObject(parent),
    m_sm(sm),
    m_api(api),
    m_net(new QNetworkAccessManager(this))
{
    load();

    connect(m_api, &ModrinthApi::downloadFinished, this,
            [this](const QString& path) {
                if (m_currentBuild.isEmpty() || m_queue.isEmpty())
                    return;

                if (m_queueIndex >= m_queue.size())
                    return;

                const ModDownloadTask task = m_queue.at(m_queueIndex);
                const QString expectedPath =
                    m_sm->gameDir() + "/" + m_currentBuild + "/mods/" + task.fileName;

                if (QDir::cleanPath(path) != QDir::cleanPath(expectedPath))
                    return;

                ++m_queueIndex;

                emit creationProgress(
                    "Загрузка модов",
                    QString("%1 / %2").arg(m_queueIndex).arg(m_queue.size()),
                    m_queue.isEmpty()
                        ? 1.0
                        : double(m_queueIndex) / double(m_queue.size())
                    );

                downloadNextMod();
            });

    connect(m_api, &ModrinthApi::error, this,
            [this](const QString& msg) {
                if (m_currentBuild.isEmpty())
                    return;

                newDebug() << "[LocalBuilds] Error:" << msg;

                m_currentBuild.clear();
                m_queue.clear();
                m_queueIndex = 0;

                emit creationError(msg);
            });
}

QString LocalBuildsManager::configPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/local_builds.json";
}

bool LocalBuildsManager::cleanRuntime(const QString& buildPath)
{
    const QStringList directories = {
        "mods",
        "config",
        "defaultconfigs",
        "kubejs",
        "scripts",
        "versions",
        "libraries",
        "natives"
    };

    for (const QString& name : directories) {
        const QString path = buildPath + "/" + name;
        QDir dir(path);

        if (dir.exists() && !dir.removeRecursively()) {
            newDebug() << "[LocalBuilds] Failed to remove:" << path;
            return false;
        }
    }

    const QStringList files = {
        "legacyClassPath.txt",
        "forge-installer.jar",
        "neoforge-installer.jar",
        "installation"
    };

    for (const QString& name : files) {
        const QString path = buildPath + "/" + name;

        if (QFile::exists(path) && !QFile::remove(path)) {
            newDebug() << "[LocalBuilds] Failed to remove:" << path;
            return false;
        }
    }

    QDir().mkpath(buildPath + "/mods");

    return true;
}

void LocalBuildsManager::load()
{
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue& v : root["builds"].toArray()) {
        QJsonObject o = v.toObject();
        Entry e;
        e.name = o["name"].toString();
        e.mcVersion = o["mcVersion"].toString();
        e.loader = o["loader"].toString();
        e.loaderVersion = o["loaderVersion"].toString();
        for (const QJsonValue& mv : o["mods"].toArray()) {
            if (mv.isString()) {
                e.projectIds << mv.toString();
                e.projectNames << "Unknown";
            } else {
                QJsonObject modObj = mv.toObject();
                e.projectIds << modObj["id"].toString();
                e.projectNames << modObj["name"].toString();
            }
        }
        m_entries.append(e);
    }
    newDebug() << "[LocalBuilds] Loaded" << m_entries.size() << "builds from config";
}

void LocalBuildsManager::save()
{
    QJsonArray arr;
    for (const Entry& e : m_entries) {
        QJsonObject o;
        o["name"] = e.name;
        o["mcVersion"] = e.mcVersion;
        o["loader"] = e.loader;
        o["loaderVersion"] = e.loaderVersion;
        QJsonArray mods;
        for (int i = 0; i < e.projectIds.size(); ++i) {
            QJsonObject modObj;
            modObj["id"] = e.projectIds[i];
            modObj["name"] = e.projectNames.value(i, "Unknown");
            mods.append(modObj);
        }
        o["mods"] = mods;
        arr.append(o);
    }
    QJsonObject root;
    root["builds"] = arr;

    QFile f(configPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    newDebug() << "[LocalBuilds] Saved" << m_entries.size() << "builds to config";
}

QJsonArray LocalBuildsManager::getInstalledModsForBuild(const QString& name) const
{
    Entry e = findEntry(name);
    QJsonArray arr;
    for (int i = 0; i < e.projectIds.size(); ++i) {
        QJsonObject modObj;
        modObj["id"] = e.projectIds[i];
        modObj["name"] = e.projectNames.value(i, "Unknown");
        arr.append(modObj);
    }
    newDebug() << "[LocalBuilds] getInstalledModsForBuild:" << name << "count:" << arr.size();
    return arr;
}

void LocalBuildsManager::startModDownloadQueue(const QString& name, bool installLoaderAfterMods)
{
    m_installLoaderAfterMods = installLoaderAfterMods;

    if (m_queue.isEmpty()) {
        if (m_installLoaderAfterMods) {
            startLoaderInstall(name);
        } else {
            m_currentBuild.clear();
            emit creationFinished(name);
        }
        return;
    }

    downloadNextMod();
}

void LocalBuildsManager::createBuild(const QString& name, const QString& mcVersion, const QString& loader, const QString& loaderVersion, const QJsonArray& mods)
{
    QString realMcVersion = mcVersion;
    QString realLoaderVersion = loaderVersion;

    if (!mcVersion.startsWith("1.") && loaderVersion.startsWith("1.")) {
        realMcVersion = loaderVersion;
        realLoaderVersion = mcVersion;
        newDebug() << "[LocalBuilds] createBuild: WARNING - mcVersion and loaderVersion arguments were swapped! Auto-correcting.";
    }

    const QString cleanName = name.trimmed();
    if (cleanName.isEmpty() || contains(cleanName)) {
        newDebug() << "[LocalBuilds] createBuild: Name empty or exists:" << cleanName;
        emit creationError("Сборка с таким именем уже существует");
        return;
    }

    Entry e;
    e.name = cleanName;
    e.mcVersion = realMcVersion;
    e.loader = loader;
    e.loaderVersion = realLoaderVersion;
    for (const QJsonValue& v : mods) {
        QJsonObject modObj = v.toObject();
        e.projectIds << modObj["id"].toString();
        e.projectNames << modObj["name"].toString();
    }

    m_entries.append(e);
    save();
    emit buildsChanged();

    m_currentBuild = cleanName;
    QDir().mkpath(m_sm->gameDir() + "/" + cleanName + "/mods");
    emit creationProgress("Подготовка", "Поиск зависимостей...", -1.0);

    newDebug() << "[LocalBuilds] createBuild: Resolving" << e.projectIds.size() << "mods for" << cleanName;
    ModDependencyResolver* resolver = new ModDependencyResolver(m_api, this);

    connect(resolver, &ModDependencyResolver::warning, this, [](const QString& msg) {
        newDebug() << "[LocalBuilds] Resolver warning:" << msg;
    });

    connect(resolver, &ModDependencyResolver::resolved, this, [this, resolver, cleanName](const QList<ModDownloadTask>& tasks) {
        resolver->deleteLater();
        m_queue = tasks;
        m_queueIndex = 0;
        newDebug() << "[LocalBuilds] createBuild: Resolved" << tasks.size() << "files for" << cleanName;
        startModDownloadQueue(cleanName, true);
    });
    connect(resolver, &ModDependencyResolver::error, this, [this, resolver](const QString& msg) {
        resolver->deleteLater();
        m_currentBuild.clear();
        emit creationError(msg);
    });
    resolver->resolve(e.projectIds, realMcVersion, loader);
}

QJsonArray LocalBuildsManager::buildsArray() const
{
    QJsonArray arr;
    for (const Entry& e : m_entries) {
        QJsonObject o;
        o["name"] = e.name;
        o["version"] = e.mcVersion + " (" + e.loader + " " + e.loaderVersion + ")";
        o["packVersion"] = readMetaPackVersion(m_sm->gameDir() + "/" + e.name);
        o["icon"] = QString();
        o["v"] = e.loaderVersion;
        o["loader"] = e.loader;
        o["mcV"] = e.mcVersion;
        o["git"] = QString();
        o["parts"] = 1;
        o["category"] = QStringLiteral("local");
        arr.append(o);
    }
    return arr;
}

bool LocalBuildsManager::contains(const QString& name) const
{
    for (const Entry& e : m_entries)
        if (e.name == name) return true;
    return false;
}

LocalBuildsManager::Entry LocalBuildsManager::findEntry(const QString& name) const
{
    for (const Entry& e : m_entries)
        if (e.name == name) return e;
    return Entry();
}

void LocalBuildsManager::downloadNextMod()
{
    if (m_queueIndex >= m_queue.size()) {
        const QString build = m_currentBuild;

        if (m_installLoaderAfterMods) {
            startLoaderInstall(build);
        } else {
            m_currentBuild.clear();
            emit creationFinished(build);
        }

        return;
    }

    const ModDownloadTask task = m_queue.at(m_queueIndex);

    const QString savePath =
        m_sm->gameDir() + "/" + m_currentBuild + "/mods/" + task.fileName;

    if (QFile::exists(savePath) &&
        QFileInfo(savePath).size() == task.size) {
        ++m_queueIndex;
        downloadNextMod();
        return;
    }

    emit creationProgress(
        "Загрузка модов",
        QString("%1 (%2 / %3)")
            .arg(task.fileName)
            .arg(m_queueIndex + 1)
            .arg(m_queue.size()),
        m_queue.isEmpty()
            ? 1.0
            : double(m_queueIndex) / double(m_queue.size())
        );

    m_api->downloadFile(task.url, savePath);
}

void LocalBuildsManager::startLoaderInstall(const QString& name)
{
    const Entry e = findEntry(name);

    if (e.name.isEmpty()) {
        m_currentBuild.clear();
        emit creationError("Сборка не найдена");
        return;
    }

    emit creationProgress(
        "Установка загрузчика",
        e.loader + " " + e.loaderVersion,
        -1.0
        );

    if (m_finishedConn)
        disconnect(m_finishedConn);

    m_finishedConn = connect(
        m_mh,
        &MinecraftHandler::finished,
        this,
        [this, name]() {
            if (m_finishedConn)
                disconnect(m_finishedConn);

            m_finishedConn = QMetaObject::Connection();

            m_currentBuild.clear();
            m_queue.clear();
            m_queueIndex = 0;

            emit creationFinished(name);
        },
        Qt::SingleShotConnection
        );

    m_mh->installLocalBuild(
        name,
        e.mcVersion,
        e.loader,
        e.loaderVersion
        );
}

void LocalBuildsManager::editBuild(
    const QString& oldName,
    const QString& newName,
    const QString& mcVersion,
    const QString& loader,
    const QString& loaderVersion,
    const QJsonArray& mods)
{
    QString realMcVersion = mcVersion;
    QString realLoaderVersion = loaderVersion;

    if (!mcVersion.startsWith("1.") &&
        loaderVersion.startsWith("1.")) {
        realMcVersion = loaderVersion;
        realLoaderVersion = mcVersion;
    }

    const QString cleanOld = oldName.trimmed();
    const QString cleanNew = newName.trimmed();

    if (cleanOld.isEmpty() || !contains(cleanOld)) {
        emit creationError("Исходная сборка не найдена или имя пустое");
        return;
    }

    if (cleanNew.isEmpty()) {
        emit creationError("Имя сборки не может быть пустым");
        return;
    }

    if (cleanNew != cleanOld && contains(cleanNew)) {
        emit creationError(
            "Сборка с именем '" + cleanNew + "' уже существует"
            );
        return;
    }

    Entry oldEntry = findEntry(cleanOld);

    const QString finalMcVersion =
        realMcVersion.isEmpty()
            ? oldEntry.mcVersion
            : realMcVersion;

    const QString finalLoader =
        loader.isEmpty()
            ? oldEntry.loader
            : loader;

    const QString finalLoaderVersion =
        realLoaderVersion.isEmpty()
            ? oldEntry.loaderVersion
            : realLoaderVersion;

    const QString oldPath =
        m_sm->gameDir() + "/" + cleanOld;

    QString newPath =
        m_sm->gameDir() + "/" + cleanNew;

    QFile testFile(oldPath + "/.lock_test");

    if (!testFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit creationError(
            "Невозможно изменить сборку: Minecraft запущен или папка заблокирована."
            );
        return;
    }

    testFile.close();
    testFile.remove();

    QStringList newModsIds;
    QStringList newModsNames;

    for (const QJsonValue& value : mods) {
        const QJsonObject object = value.toObject();

        const QString id = object.value("id").toString().trimmed();

        if (id.isEmpty())
            continue;

        newModsIds << id;
        newModsNames << object.value("name").toString();
    }

    const bool coreChanged =
        oldEntry.mcVersion != finalMcVersion ||
        oldEntry.loader != finalLoader ||
        oldEntry.loaderVersion != finalLoaderVersion;

    const bool modsChanged =
        oldEntry.projectIds != newModsIds;

    if (!coreChanged && !modsChanged && cleanOld == cleanNew) {
        emit creationFinished(cleanNew);
        return;
    }

    if (cleanOld != cleanNew) {
        if (!QDir().rename(oldPath, newPath)) {
            emit creationError(
                "Ошибка файловой системы: не удалось переименовать папку сборки"
                );
            return;
        }
    } else {
        newPath = oldPath;
    }

    if (coreChanged) {
        if (!cleanRuntime(newPath)) {
            if (cleanOld != cleanNew) {
                QDir().rename(newPath, oldPath);
            }

            emit creationError(
                "Не удалось очистить старую версию Minecraft"
                );
            return;
        }
    } else if (modsChanged) {
        const QString modsPath = newPath + "/mods";
        QDir modsDir(modsPath);

        if (modsDir.exists() && !modsDir.removeRecursively()) {
            if (cleanOld != cleanNew) {
                QDir().rename(newPath, oldPath);
            }

            emit creationError(
                "Не удалось очистить папку модов"
                );
            return;
        }

        QDir().mkpath(modsPath);
    }

    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].name == cleanOld) {
            m_entries[i].name = cleanNew;
            m_entries[i].mcVersion = finalMcVersion;
            m_entries[i].loader = finalLoader;
            m_entries[i].loaderVersion = finalLoaderVersion;
            m_entries[i].projectIds = newModsIds;
            m_entries[i].projectNames = newModsNames;
            break;
        }
    }

    save();
    emit buildsChanged();

    // Раньше при простом переименовании (coreChanged == false и
    // modsChanged == false, изменилось только имя) код всё равно проваливался
    // в резолвер зависимостей и заново дёргал Modrinth API по каждому моду —
    // лишние сетевые запросы и задержка без единой причины. Файлы модов при
    // этом всё равно не перекачивались бы (downloadNextMod проверяет
    // существование+размер), но сама сверка версий по сети выполнялась зря.
    if (!coreChanged && !modsChanged) {
        m_currentBuild.clear();
        emit creationFinished(cleanNew);
        return;
    }

    m_currentBuild = cleanNew;
    m_queue.clear();
    m_queueIndex = 0;

    emit creationProgress(
        coreChanged
            ? "Обновление ядра"
            : "Синхронизация модов",
        coreChanged
            ? "Установка Minecraft и загрузчика..."
            : "Поиск зависимостей...",
        -1.0
        );

    if (coreChanged) {
        if (m_finishedConn)
            disconnect(m_finishedConn);

        m_finishedConn = connect(
            m_mh,
            &MinecraftHandler::finished,
            this,
            [this,
             cleanNew,
             newModsIds,
             finalMcVersion,
             finalLoader]() {

                if (m_finishedConn)
                    disconnect(m_finishedConn);

                m_finishedConn = QMetaObject::Connection();

                ModDependencyResolver* resolver =
                    new ModDependencyResolver(m_api, this);

                connect(
                    resolver,
                    &ModDependencyResolver::warning,
                    this,
                    [](const QString& msg) {
                        newDebug()
                        << "[LocalBuilds] Resolver warning:"
                        << msg;
                    }
                    );

                connect(
                    resolver,
                    &ModDependencyResolver::resolved,
                    this,
                    [this, resolver, cleanNew]
                    (const QList<ModDownloadTask>& tasks) {

                        resolver->deleteLater();

                        m_queue = tasks;
                        m_queueIndex = 0;
                        m_currentBuild = cleanNew;

                        startModDownloadQueue(cleanNew, false);
                    }
                    );

                connect(
                    resolver,
                    &ModDependencyResolver::error,
                    this,
                    [this, resolver](const QString& msg) {

                        resolver->deleteLater();

                        m_currentBuild.clear();
                        m_queue.clear();
                        m_queueIndex = 0;

                        emit creationError(
                            "Ошибка резолва модов: " + msg
                            );
                    }
                    );

                resolver->resolve(
                    newModsIds,
                    finalMcVersion,
                    finalLoader
                    );
            },
            Qt::SingleShotConnection
            );

        m_mh->installLocalBuild(
            cleanNew,
            finalMcVersion,
            finalLoader,
            finalLoaderVersion
            );

        return;
    }

    ModDependencyResolver* resolver =
        new ModDependencyResolver(m_api, this);

    connect(
        resolver,
        &ModDependencyResolver::warning,
        this,
        [](const QString& msg) {
            newDebug()
            << "[LocalBuilds] Resolver warning:"
            << msg;
        }
        );

    connect(
        resolver,
        &ModDependencyResolver::resolved,
        this,
        [this, resolver, cleanNew]
        (const QList<ModDownloadTask>& tasks) {

            resolver->deleteLater();

            m_queue = tasks;
            m_queueIndex = 0;
            m_currentBuild = cleanNew;

            startModDownloadQueue(cleanNew, false);
        }
        );

    connect(
        resolver,
        &ModDependencyResolver::error,
        this,
        [this, resolver](const QString& msg) {

            resolver->deleteLater();

            m_currentBuild.clear();
            m_queue.clear();
            m_queueIndex = 0;

            emit creationError(
                "Ошибка резолва модов: " + msg
                );
        }
        );

    resolver->resolve(
        newModsIds,
        finalMcVersion,
        finalLoader
        );
}

void LocalBuildsManager::removeBuild(const QString& name)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).name == name) {
            m_entries.removeAt(i);
            save();
            emit buildsChanged();
            return;
        }
    }
}

void LocalBuildsManager::requestMinecraftVersions()
{
    QNetworkRequest req{QUrl("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json")};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QStringList out;
        if (reply->error() == QNetworkReply::NoError) {
            QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).object()["versions"].toArray();
            for (const QJsonValue& v : arr) {
                QJsonObject o = v.toObject();
                if (o["type"].toString() == "release") out << o["id"].toString();
            }
        }
        reply->deleteLater();
        newDebug() << "[LocalBuilds] Emitting minecraftVersionsReady with" << out.size() << "versions";
        emit minecraftVersionsReady(out);
    });
}

void LocalBuildsManager::deleteBuild(const QString& name)
{
    const QString path = m_sm->gameDir() + "/" + name;
    QDir dir(path);
    if (dir.exists()) {
        if (!dir.removeRecursively())
            newDebug() << "[LocalBuilds] Failed to remove dir:" << path;
    }
    removeBuild(name);
}

QString LocalBuildsManager::readMetaPackVersion(const QString& gamePath) const
{
    QFile f(gamePath + "/sealauncher_meta.json");
    if (!f.open(QIODevice::ReadOnly)) return "1";

    QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    QString ver = obj.value("packVersion").toString();

    return ver.isEmpty() ? "1" : ver;
}

QNetworkRequest LocalBuildsManager::mkRequest(const QString& url) const
{
    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    req.setRawHeader("Accept", "application/json, application/xml, */*");
    req.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    return req;
}

void LocalBuildsManager::requestLoaderVersions(const QString& loader, const QString& mcVersion)
{
    if (loader == "vanilla") {
        emit loaderVersionsReady(QStringList() << "-");
        return;
    }

    if (loader == "fabric") {
        QNetworkReply* reply = m_net->get(mkRequest("https://meta.fabricmc.net/v2/versions/loader/" + mcVersion));
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            QStringList out;
            if (reply->error() == QNetworkReply::NoError) {
                QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).array();
                for (const QJsonValue& v : arr)
                    out << v.toObject()["loader"].toObject()["version"].toString();
            } else {
                newDebug() << "[LocalBuilds] fabric error:" << reply->errorString();
            }
            reply->deleteLater();
            emit loaderVersionsReady(out);
        });
        return;
    }

    if (loader == "forge") {
        requestForgeVersions(mcVersion);
        return;
    }

    if (loader == "neoforge") {
        QNetworkReply* reply = m_net->get(mkRequest("https://maven.neoforged.net/api/maven/versions/releases/net/neoforged/neoforge"));
        connect(reply, &QNetworkReply::finished, this, [this, reply, mcVersion]() {
            QStringList out;
            if (reply->error() == QNetworkReply::NoError) {
                QJsonArray versions = QJsonDocument::fromJson(reply->readAll()).array();
                QStringList mcParts = mcVersion.split('.');
                QString expectedPrefix = (mcParts.size() >= 2) ? (mcParts[1] + ".") : "";
                for (const QJsonValue& v : versions) {
                    QString ver = v.toString();
                    if (expectedPrefix.isEmpty() || ver.startsWith(expectedPrefix)) {
                        out.prepend(ver);
                    }
                }
                newDebug() << "[LocalBuilds] neoforge API success:" << out.size() << "versions found for prefix" << expectedPrefix;
            } else {
                newDebug() << "[LocalBuilds] neoforge API error:" << reply->errorString() << "HTTP:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            }
            reply->deleteLater();
            emit loaderVersionsReady(out);
        });
        return;
    }

    emit loaderVersionsReady(QStringList());
}

void LocalBuildsManager::requestForgeVersions(const QString& mcVersion)
{
    QNetworkReply* reply = m_net->get(mkRequest("https://files.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml"));
    connect(reply, &QNetworkReply::finished, this, [this, reply, mcVersion]() {
        QStringList out;
        if (reply->error() == QNetworkReply::NoError) {
            const QString prefix = mcVersion + "-";
            QXmlStreamReader xml(reply->readAll());
            bool inVersion = false;
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement() && xml.name().toString() == "version") inVersion = true;
                else if (xml.isEndElement() && xml.name().toString() == "version") inVersion = false;
                else if (inVersion && xml.isCharacters()) {
                    const QString v = xml.text().toString().trimmed();
                    if (v.startsWith(prefix)) out.prepend(v.mid(prefix.size()));
                }
            }
        }
        newDebug() << "[LocalBuilds] forge metadata:" << out.size() << "versions, error:" << reply->errorString();
        reply->deleteLater();
        if (out.isEmpty()) requestForgePromotions(mcVersion);
        else emit loaderVersionsReady(out);
    });
}

void LocalBuildsManager::requestForgePromotions(const QString& mcVersion)
{
    QNetworkReply* reply = m_net->get(mkRequest("https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json"));
    connect(reply, &QNetworkReply::finished, this, [this, reply, mcVersion]() {
        QStringList out;
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject promos = QJsonDocument::fromJson(reply->readAll()).object()["promos"].toObject();
            const QString rec = promos.value(mcVersion + "-recommended").toString();
            const QString lat = promos.value(mcVersion + "-latest").toString();
            if (!rec.isEmpty()) out << rec;
            if (!lat.isEmpty() && lat != rec) out << lat;
        }
        newDebug() << "[LocalBuilds] forge promotions fallback:" << out.size() << "versions";
        reply->deleteLater();
        emit loaderVersionsReady(out);
    });
}
