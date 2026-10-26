#include "localbuildsmanager.h"
#include "SettingsManager.h"
#include "minecrafthandler.h"
#include "NewDebug.h"

#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QXmlStreamReader>

namespace {

bool nameIsSafe(const QString& n)
{
    if (n.isEmpty() || n != n.trimmed()) return false;
    if (n == QLatin1String(".") || n == QLatin1String("..")) return false;
    static const QString bad = QStringLiteral("/\\:*?\"<>|");
    for (const QChar c : n)
        if (bad.contains(c)) return false;
    return true;
}

bool saveAtomic(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    if (f.write(data) != data.size()) { f.cancelWriting(); return false; }
    return f.commit();
}

QString appDataFile(const QString& name)
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QLatin1Char('/') + name;
}

}

struct LocalBuildsManager::FetchBytesCtx {
    QString url;
    int attempt = 0;
    int retries = 3;
    std::function<void(bool, const QByteArray&)> cb;
};

LocalBuildsManager::LocalBuildsManager(SettingsManager* sm, ModrinthApi* api, QObject* parent)
    : QObject(parent),
    m_sm(sm),
    m_api(api),
    m_net(new QNetworkAccessManager(this))
{
    load();
}

void LocalBuildsManager::setMinecraftHandler(MinecraftHandler* mh)
{
    m_mh = mh;
    if (!m_mh) return;
    connect(m_mh, &MinecraftHandler::installSucceeded, this, &LocalBuildsManager::onInstallSucceeded);
    connect(m_mh, &MinecraftHandler::installFailed,    this, &LocalBuildsManager::onInstallFailed);
    connect(m_mh, &MinecraftHandler::installAbandoned, this, &LocalBuildsManager::onInstallAbandoned);
}

QString LocalBuildsManager::configPath() const
{
    return appDataFile(QStringLiteral("local_builds.json"));
}

bool LocalBuildsManager::isNameSafe(const QString& name) const
{
    return nameIsSafe(name);
}

void LocalBuildsManager::load()
{
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue& v : root[QStringLiteral("builds")].toArray()) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.name = o[QStringLiteral("name")].toString();
        if (e.name.isEmpty()) continue;
        e.mcVersion = o[QStringLiteral("mcVersion")].toString();
        e.loader = o[QStringLiteral("loader")].toString();
        e.loaderVersion = o[QStringLiteral("loaderVersion")].toString();
        e.pendingSync = o[QStringLiteral("pendingSync")].toBool(false);
        for (const QJsonValue& mv : o[QStringLiteral("mods")].toArray()) {
            if (mv.isString()) {
                e.projectIds << mv.toString();
                e.projectNames << QStringLiteral("Unknown");
            } else {
                const QJsonObject modObj = mv.toObject();
                e.projectIds << modObj[QStringLiteral("id")].toString();
                e.projectNames << modObj[QStringLiteral("name")].toString();
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
        o[QStringLiteral("name")] = e.name;
        o[QStringLiteral("mcVersion")] = e.mcVersion;
        o[QStringLiteral("loader")] = e.loader;
        o[QStringLiteral("loaderVersion")] = e.loaderVersion;
        o[QStringLiteral("pendingSync")] = e.pendingSync;
        QJsonArray mods;
        for (int i = 0; i < e.projectIds.size(); ++i) {
            QJsonObject modObj;
            modObj[QStringLiteral("id")] = e.projectIds[i];
            modObj[QStringLiteral("name")] = e.projectNames.value(i, QStringLiteral("Unknown"));
            mods.append(modObj);
        }
        o[QStringLiteral("mods")] = mods;
        arr.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("builds")] = arr;

    if (!saveAtomic(configPath(), QJsonDocument(root).toJson(QJsonDocument::Indented)))
        newDebug() << "[LocalBuilds] FAILED to save" << configPath();
    else
        newDebug() << "[LocalBuilds] Saved" << m_entries.size() << "builds to config";
}

int LocalBuildsManager::indexOf(const QString& name) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).name == name) return i;
    return -1;
}

bool LocalBuildsManager::contains(const QString& name) const
{
    return indexOf(name) >= 0;
}

LocalBuildsManager::Entry LocalBuildsManager::findEntry(const QString& name) const
{
    const int i = indexOf(name);
    return i >= 0 ? m_entries.at(i) : Entry();
}

QJsonArray LocalBuildsManager::getInstalledModsForBuild(const QString& name) const
{
    const Entry e = findEntry(name);
    QJsonArray arr;
    for (int i = 0; i < e.projectIds.size(); ++i) {
        QJsonObject modObj;
        modObj[QStringLiteral("id")] = e.projectIds[i];
        modObj[QStringLiteral("name")] = e.projectNames.value(i, QStringLiteral("Unknown"));
        arr.append(modObj);
    }
    return arr;
}

QJsonArray LocalBuildsManager::buildsArray() const
{
    QJsonArray arr;
    for (const Entry& e : m_entries) {
        QJsonObject o;
        o[QStringLiteral("name")] = e.name;
        o[QStringLiteral("version")] = e.mcVersion + QStringLiteral(" (") + e.loader + QLatin1Char(' ') + e.loaderVersion + QLatin1Char(')');
        o[QStringLiteral("packVersion")] = readMetaPackVersion(m_sm->gameDir() + QLatin1Char('/') + e.name);
        o[QStringLiteral("icon")] = QString();
        o[QStringLiteral("v")] = e.loaderVersion;
        o[QStringLiteral("loader")] = e.loader;
        o[QStringLiteral("mcV")] = e.mcVersion;
        o[QStringLiteral("git")] = QString();
        o[QStringLiteral("parts")] = 1;
        o[QStringLiteral("category")] = QStringLiteral("local");
        o[QStringLiteral("needsSync")] = e.pendingSync;
        arr.append(o);
    }
    return arr;
}

QString LocalBuildsManager::readMetaPackVersion(const QString& gamePath) const
{
    QFile f(gamePath + QStringLiteral("/sealauncher_meta.json"));
    if (!f.open(QIODevice::ReadOnly)) return QStringLiteral("1");
    const QString ver = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("packVersion")).toString();
    return ver.isEmpty() ? QStringLiteral("1") : ver;
}

bool LocalBuildsManager::cleanRuntime(const QString& buildPath)
{
    const QStringList directories = {
        QStringLiteral("mods"), QStringLiteral("config"), QStringLiteral("defaultconfigs"),
        QStringLiteral("kubejs"), QStringLiteral("scripts"), QStringLiteral("versions"),
        QStringLiteral("libraries"), QStringLiteral("natives"), QStringLiteral(".sl_tmp")
    };
    for (const QString& name : directories) {
        const QString path = buildPath + QLatin1Char('/') + name;
        QDir dir(path);
        if (dir.exists() && !dir.removeRecursively()) {
            newDebug() << "[LocalBuilds] Failed to remove:" << path;
            return false;
        }
    }

    const QStringList files = {
        QStringLiteral("legacyClassPath.txt"), QStringLiteral("forge-installer.jar"),
        QStringLiteral("neoforge-installer.jar"), QStringLiteral("installation"),
        QStringLiteral("sealauncher_meta.json")
    };
    for (const QString& name : files) {
        const QString path = buildPath + QLatin1Char('/') + name;
        if (QFile::exists(path) && !QFile::remove(path)) {
            newDebug() << "[LocalBuilds] Failed to remove:" << path;
            return false;
        }
    }

    QDir().mkpath(buildPath + QStringLiteral("/mods"));
    return true;
}

void LocalBuildsManager::clearOperation()
{
    if (m_resolver) { m_resolver->disconnect(this); m_resolver->deleteLater(); m_resolver = nullptr; }
    if (m_modsBatch) { m_modsBatch->disconnect(this); m_modsBatch->cancel(); }
    m_opActive = false;
    m_awaitingLoader = false;
    m_isCreate = false;
    m_installLoaderAfterMods = true;
    m_currentBuild.clear();
}

void LocalBuildsManager::beginPipeline(const QString& name, bool installLoader, bool isCreate)
{
    clearOperation();
    m_opActive = true;
    m_currentBuild = name;
    m_isCreate = isCreate;
    m_installLoaderAfterMods = installLoader;

    const Entry e = findEntry(name);
    QDir().mkpath(m_sm->gameDir() + QLatin1Char('/') + name + QStringLiteral("/mods"));
    emit creationProgress(QStringLiteral("Подготовка"), QStringLiteral("Поиск зависимостей..."), -1.0);

    if (e.projectIds.isEmpty()) {
        QTimer::singleShot(0, this, [this]() { if (m_opActive) onResolved({}); });
        return;
    }

    QHash<QString, QString> names;
    for (int i = 0; i < e.projectIds.size(); ++i)
        names.insert(e.projectIds.at(i), e.projectNames.value(i));

    m_resolver = new ModDependencyResolver(m_api, this);
    m_resolver->setProjectNames(names);

    connect(m_resolver, &ModDependencyResolver::warning, this, [](const QString& msg) {
        newDebug() << "[LocalBuilds] Resolver warning:" << msg;
    });
    connect(m_resolver, &ModDependencyResolver::progress, this, [this](const QString& stage, int done, int) {
        if (m_opActive) emit creationProgress(stage, QStringLiteral("Найдено файлов: %1").arg(done), -1.0);
    });
    connect(m_resolver, &ModDependencyResolver::resolved, this, [this](const QList<ModDownloadTask>& tasks) {
        onResolved(tasks);
    });
    connect(m_resolver, &ModDependencyResolver::error, this, [this](const QString& msg) {
        failOperation(msg);
    });

    m_resolver->resolve(e.projectIds, e.mcVersion, e.loader);
}

void LocalBuildsManager::onResolved(const QList<ModDownloadTask>& tasks)
{
    if (!m_opActive) return;

    if (m_resolver) { m_resolver->disconnect(this); m_resolver->deleteLater(); m_resolver = nullptr; }
    newDebug() << "[LocalBuilds] Resolved" << tasks.size() << "files for" << m_currentBuild;

    const QString modsDir = m_sm->gameDir() + QLatin1Char('/') + m_currentBuild + QStringLiteral("/mods");
    QDir().mkpath(modsDir);

    QList<DownloadItem> items;
    for (const ModDownloadTask& t : tasks) {
        const QString fileName = QFileInfo(t.fileName).fileName();
        if (fileName.isEmpty() || t.url.isEmpty()) continue;
        DownloadItem it;
        it.url = t.url;
        it.path = modsDir + QLatin1Char('/') + fileName;
        it.size = t.size > 0 ? t.size : -1;
        it.sha1 = t.sha1;
        items << it;
    }

    if (items.isEmpty()) { modsDone(); return; }

    if (!m_modsBatch) m_modsBatch = new DownloadBatch(this);
    m_modsBatch->disconnect(this);

    connect(m_modsBatch, &DownloadBatch::progress, this,
            [this](qint64 doneB, qint64 totalB, int doneF, int totalF, double speed) {
                if (!m_opActive) return;
                QString details = QStringLiteral("Файлов: %1 / %2").arg(doneF).arg(totalF);
                double p = totalF > 0 ? double(doneF) / double(totalF) : -1.0;
                if (totalB > 0) {
                    details = QStringLiteral("%1 / %2 МБ").arg(doneB / 1048576.0, 0, 'f', 1).arg(totalB / 1048576.0, 0, 'f', 1)
                              + QStringLiteral("  •  файлов: %1 / %2").arg(doneF).arg(totalF);
                    if (speed > 0) details += QStringLiteral("  (%1 МБ/с)").arg(speed / 1048576.0, 0, 'f', 1);
                    if (speed > 1024.0) {
                        const qint64 sec = qint64(double(totalB - doneB) / speed);
                        if (sec >= 5)
                            details += sec < 60 ? QStringLiteral("  •  ещё ~%1 с").arg(sec)
                                                : QStringLiteral("  •  ещё ~%1 мин %2 с").arg(sec / 60).arg(sec % 60);
                    }
                    p = double(doneB) / double(totalB);
                }
                emit creationProgress(QStringLiteral("Загрузка модов"), details, qBound(0.0, p, 1.0));
            });
    connect(m_modsBatch, &DownloadBatch::finished, this, [this](bool ok, const QString& err) {
        if (!m_opActive) return;
        if (!ok) {
            failOperation(QStringLiteral("Не удалось скачать моды (проблемы с сетью?). %1").arg(err));
            return;
        }
        modsDone();
    });

    emit creationProgress(QStringLiteral("Загрузка модов"), QStringLiteral("Файлов: %1").arg(items.size()), 0.0);
    m_modsBatch->start(items, 6);
}

void LocalBuildsManager::modsDone()
{
    if (!m_opActive) return;
    if (m_installLoaderAfterMods) startLoaderInstall();
    else finishOperation();
}

void LocalBuildsManager::startLoaderInstall()
{
    const Entry e = findEntry(m_currentBuild);
    if (e.name.isEmpty()) { failOperation(QStringLiteral("Сборка не найдена")); return; }
    if (!m_mh) { failOperation(QStringLiteral("Внутренняя ошибка: установщик недоступен")); return; }

    m_awaitingLoader = true;
    emit creationProgress(QStringLiteral("Установка загрузчика"), e.loader + QLatin1Char(' ') + e.loaderVersion, -1.0);
    m_mh->installLocalBuild(e.name, e.mcVersion, e.loader, e.loaderVersion);
}

void LocalBuildsManager::onInstallSucceeded(const QString& name)
{
    if (m_opActive && m_awaitingLoader && name == m_currentBuild) finishOperation();
}

void LocalBuildsManager::onInstallFailed(const QString& name, const QString& message, bool canRetry)
{
    if (!(m_opActive && m_awaitingLoader && name == m_currentBuild)) return;
    if (!canRetry) failOperation(message);
}

void LocalBuildsManager::onInstallAbandoned(const QString& name)
{
    if (m_opActive && m_awaitingLoader && name == m_currentBuild)
        failOperation(QStringLiteral("Установка отменена"), true);
}

void LocalBuildsManager::finishOperation()
{
    const QString name = m_currentBuild;
    const int i = indexOf(name);
    if (i >= 0 && m_entries[i].pendingSync) {
        m_entries[i].pendingSync = false;
        save();
    }
    clearOperation();
    emit creationFinished(name);
    emit buildsChanged();
}

void LocalBuildsManager::failOperation(const QString& message, bool cancelled)
{
    if (!m_opActive) return;
    const QString name = m_currentBuild;
    const bool create = m_isCreate;
    newDebug() << "[LocalBuilds] Operation failed:" << name << message << "cancelled:" << cancelled;

    clearOperation();
    if (create) rollbackCreate(name);

    if (cancelled) emit creationCancelled(name);
    else           emit creationError(message);
}

void LocalBuildsManager::rollbackCreate(const QString& name)
{
    const int i = indexOf(name);
    if (i >= 0) {
        m_entries.removeAt(i);
        save();
    }
    QDir(m_sm->gameDir() + QLatin1Char('/') + name).removeRecursively();
    emit buildsChanged();
}

void LocalBuildsManager::cancelCreation()
{
    if (!m_opActive) return;
    if (m_awaitingLoader && m_mh && m_mh->busy()) {
        m_mh->cancelInstallation();
        return;
    }
    failOperation(QStringLiteral("Отменено"), true);
}

void LocalBuildsManager::createBuild(const QString& name, const QString& mcVersion, const QString& loader,
                                     const QString& loaderVersion, const QJsonArray& mods)
{
    if (m_opActive) { emit creationError(QStringLiteral("Дождитесь завершения текущей операции.")); return; }
    if (m_mh && m_mh->busy()) {
        emit creationError(QStringLiteral("Сейчас идёт установка другой сборки. Дождитесь её окончания."));
        return;
    }

    QString realMcVersion = mcVersion;
    QString realLoaderVersion = loaderVersion;
    if (!mcVersion.startsWith(QLatin1String("1.")) && loaderVersion.startsWith(QLatin1String("1."))) {
        realMcVersion = loaderVersion;
        realLoaderVersion = mcVersion;
        newDebug() << "[LocalBuilds] createBuild: mcVersion and loaderVersion were swapped, auto-corrected";
    }

    const QString cleanName = name.trimmed();
    if (cleanName.isEmpty()) { emit creationError(QStringLiteral("Введите название сборки")); return; }
    if (!nameIsSafe(cleanName)) {
        emit creationError(QStringLiteral("В названии нельзя использовать символы: \\ / : * ? \" < > |"));
        return;
    }
    if (contains(cleanName)) { emit creationError(QStringLiteral("Сборка с таким именем уже существует")); return; }
    if (QDir(m_sm->gameDir() + QLatin1Char('/') + cleanName).exists()) {
        emit creationError(QStringLiteral("Папка с таким именем уже существует (возможно, это другая сборка)"));
        return;
    }
    if (loader != QLatin1String("vanilla") && realLoaderVersion.trimmed().isEmpty()) {
        emit creationError(QStringLiteral("Не выбрана версия загрузчика"));
        return;
    }
    if (realMcVersion.trimmed().isEmpty()) { emit creationError(QStringLiteral("Не выбрана версия Minecraft")); return; }

    Entry e;
    e.name = cleanName;
    e.mcVersion = realMcVersion;
    e.loader = loader;
    e.loaderVersion = realLoaderVersion;
    if (loader != QLatin1String("vanilla")) {
        for (const QJsonValue& v : mods) {
            const QJsonObject modObj = v.toObject();
            const QString id = modObj[QStringLiteral("id")].toString().trimmed();
            if (id.isEmpty() || e.projectIds.contains(id)) continue;
            e.projectIds << id;
            e.projectNames << modObj[QStringLiteral("name")].toString();
        }
    }

    m_entries.append(e);
    save();
    emit buildsChanged();

    newDebug() << "[LocalBuilds] createBuild:" << cleanName << e.projectIds.size() << "mods";
    beginPipeline(cleanName, true, true);
}

void LocalBuildsManager::editBuild(const QString& oldName, const QString& newName,
                                   const QString& mcVersion, const QString& loader,
                                   const QString& loaderVersion, const QJsonArray& mods)
{
    if (m_opActive) { emit creationError(QStringLiteral("Дождитесь завершения текущей операции.")); return; }
    if (m_mh && m_mh->busy()) {
        emit creationError(QStringLiteral("Сейчас идёт установка. Дождитесь её окончания."));
        return;
    }

    QString realMcVersion = mcVersion;
    QString realLoaderVersion = loaderVersion;
    if (!mcVersion.startsWith(QLatin1String("1.")) && loaderVersion.startsWith(QLatin1String("1."))) {
        realMcVersion = loaderVersion;
        realLoaderVersion = mcVersion;
    }

    const QString cleanOld = oldName.trimmed();
    const QString cleanNew = newName.trimmed();

    if (cleanOld.isEmpty() || !contains(cleanOld)) { emit creationError(QStringLiteral("Исходная сборка не найдена или имя пустое")); return; }
    if (cleanNew.isEmpty()) { emit creationError(QStringLiteral("Имя сборки не может быть пустым")); return; }
    if (!nameIsSafe(cleanNew)) {
        emit creationError(QStringLiteral("В названии нельзя использовать символы: \\ / : * ? \" < > |"));
        return;
    }
    if (cleanNew != cleanOld && contains(cleanNew)) {
        emit creationError(QStringLiteral("Сборка с именем '%1' уже существует").arg(cleanNew));
        return;
    }

    const Entry oldEntry = findEntry(cleanOld);
    const QString finalMc = realMcVersion.isEmpty() ? oldEntry.mcVersion : realMcVersion;
    const QString finalLoader = loader.isEmpty() ? oldEntry.loader : loader;
    const QString finalLoaderVersion = realLoaderVersion.isEmpty() ? oldEntry.loaderVersion : realLoaderVersion;

    const QString oldPath = m_sm->gameDir() + QLatin1Char('/') + cleanOld;
    QString newPath = m_sm->gameDir() + QLatin1Char('/') + cleanNew;

    if (cleanNew != cleanOld && QDir(newPath).exists()) {
        emit creationError(QStringLiteral("Папка «%1» уже существует").arg(cleanNew));
        return;
    }

    QDir().mkpath(oldPath);
    {
        QFile testFile(oldPath + QStringLiteral("/.lock_test"));
        if (!testFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit creationError(QStringLiteral("Невозможно изменить сборку: Minecraft запущен или папка заблокирована."));
            return;
        }
        testFile.close();
        testFile.remove();
    }

    QStringList newIds, newNames;
    for (const QJsonValue& value : mods) {
        const QJsonObject object = value.toObject();
        const QString id = object.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty() || newIds.contains(id)) continue;
        newIds << id;
        newNames << object.value(QStringLiteral("name")).toString();
    }

    const bool coreChanged = oldEntry.mcVersion != finalMc || oldEntry.loader != finalLoader
                             || oldEntry.loaderVersion != finalLoaderVersion;
    const bool modsChanged = oldEntry.projectIds != newIds;
    const bool wasPending = oldEntry.pendingSync;

    if (!coreChanged && !modsChanged && !wasPending && cleanOld == cleanNew) {
        emit creationFinished(cleanNew);
        return;
    }

    if (cleanOld != cleanNew) {
        if (!QDir().rename(oldPath, newPath)) {
            emit creationError(QStringLiteral("Ошибка файловой системы: не удалось переименовать папку сборки"));
            return;
        }
    } else {
        newPath = oldPath;
    }

    const bool needSync = coreChanged || modsChanged || wasPending;
    if (needSync) {
        bool cleaned = true;
        if (coreChanged) {
            cleaned = cleanRuntime(newPath);
        } else {
            QDir modsDir(newPath + QStringLiteral("/mods"));
            cleaned = !modsDir.exists() || modsDir.removeRecursively();
            QDir().mkpath(newPath + QStringLiteral("/mods"));
        }
        if (!cleaned) {
            if (cleanOld != cleanNew) QDir().rename(newPath, oldPath);
            emit creationError(coreChanged ? QStringLiteral("Не удалось очистить старую версию Minecraft")
                                           : QStringLiteral("Не удалось очистить папку модов"));
            return;
        }
    }

    const int idx = indexOf(cleanOld);
    if (idx >= 0) {
        Entry& e = m_entries[idx];
        e.name = cleanNew;
        e.mcVersion = finalMc;
        e.loader = finalLoader;
        e.loaderVersion = finalLoaderVersion;
        e.projectIds = newIds;
        e.projectNames = newNames;
        e.pendingSync = needSync;
    }
    save();
    emit buildsChanged();

    if (!needSync) {
        emit creationFinished(cleanNew);
        return;
    }

    const bool installLoader = coreChanged || QFileInfo::exists(newPath + QStringLiteral("/installation"));
    beginPipeline(cleanNew, installLoader, false);
}

void LocalBuildsManager::removeBuild(const QString& name)
{
    const int i = indexOf(name);
    if (i < 0) return;
    m_entries.removeAt(i);
    save();
    emit buildsChanged();
}

void LocalBuildsManager::deleteBuild(const QString& name)
{
    if (m_opActive || (m_mh && m_mh->busy())) {
        emit creationError(QStringLiteral("Нельзя удалять сборки, пока идёт установка. Дождитесь её окончания или отмените."));
        return;
    }

    const QString path = m_sm->gameDir() + QLatin1Char('/') + name;
    QDir dir(path);
    if (dir.exists() && !dir.removeRecursively()) {
        newDebug() << "[LocalBuilds] Failed to remove dir:" << path;
        emit creationError(QStringLiteral("Не удалось удалить папку сборки — возможно, запущен Minecraft или файлы заняты. Сборка сохранена в списке."));
        return;
    }
    removeBuild(name);
}

QNetworkRequest LocalBuildsManager::mkRequest(const QString& url) const
{
    QNetworkRequest req = DownloadUtil::makeRequest(QUrl(url));
    req.setRawHeader("Accept", "application/json, application/xml, */*");
    req.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    return req;
}

void LocalBuildsManager::fetchBytes(const QString& url, int retries, std::function<void(bool, const QByteArray&)> cb)
{
    auto ctx = std::make_shared<FetchBytesCtx>();
    ctx->url = url;
    ctx->retries = retries;
    ctx->cb = std::move(cb);
    fetchBytesAttempt(ctx);
}

void LocalBuildsManager::fetchBytesAttempt(const std::shared_ptr<FetchBytesCtx>& ctx)
{
    QNetworkReply* reply = m_net->get(mkRequest(ctx->url));

    auto* wd = new QTimer(reply);
    wd->setSingleShot(true);
    wd->setInterval(15000);
    connect(wd, &QTimer::timeout, reply, [reply]() { reply->abort(); });
    connect(reply, &QNetworkReply::downloadProgress, wd, [wd]() { wd->start(); });
    wd->start();

    connect(reply, &QNetworkReply::finished, this, [this, ctx, reply]() {
        const int st = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && st >= 200 && st < 300;
        const QByteArray data = ok ? reply->readAll() : QByteArray();
        newDebug() << "[LocalBuilds] fetch" << ctx->url << "ok:" << ok << "HTTP" << st << reply->errorString();
        reply->deleteLater();

        if (ok) { ctx->cb(true, data); return; }
        if (DownloadUtil::isRetryableHttp(st) && ctx->attempt < ctx->retries) {
            ctx->attempt++;
            QTimer::singleShot(DownloadUtil::backoffMs(ctx->attempt), this, [this, ctx]() { fetchBytesAttempt(ctx); });
            return;
        }
        ctx->cb(false, QByteArray());
    });
}

void LocalBuildsManager::emitMinecraftVersions(const QStringList& list)
{
    newDebug() << "[LocalBuilds] minecraftVersionsReady:" << list.size();
    emit minecraftVersionsReady(list);
}

void LocalBuildsManager::requestMinecraftVersions()
{
    if (!m_mcVersionsCache.isEmpty()) {
        const QStringList cached = m_mcVersionsCache;
        QTimer::singleShot(0, this, [this, cached]() { emitMinecraftVersions(cached); });
        return;
    }

    fetchBytes(QStringLiteral("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"), 3,
               [this](bool ok, const QByteArray& data) {
                   QStringList out;
                   if (ok) {
                       for (const QJsonValue& v : QJsonDocument::fromJson(data).object()[QStringLiteral("versions")].toArray()) {
                           const QJsonObject o = v.toObject();
                           if (o[QStringLiteral("type")].toString() == QLatin1String("release"))
                               out << o[QStringLiteral("id")].toString();
                       }
                   }

                   const QString cachePath = appDataFile(QStringLiteral("mc_versions_cache.json"));
                   if (!out.isEmpty()) {
                       m_mcVersionsCache = out;
                       saveAtomic(cachePath, QJsonDocument(QJsonArray::fromStringList(out)).toJson(QJsonDocument::Compact));
                   } else {
                       QFile f(cachePath);
                       if (f.open(QIODevice::ReadOnly))
                           for (const QJsonValue& v : QJsonDocument::fromJson(f.readAll()).array()) out << v.toString();
                   }
                   emitMinecraftVersions(out);
               });
}

void LocalBuildsManager::requestLoaderVersions(const QString& loader, const QString& mcVersion)
{
    const quint64 seq = ++m_loaderReqSeq;
    auto emitIfCurrent = [this, seq](const QStringList& list) {
        if (seq == m_loaderReqSeq) emit loaderVersionsReady(list);
    };

    if (loader == QLatin1String("vanilla")) {
        emitIfCurrent(QStringList() << QStringLiteral("-"));
        return;
    }

    if (loader == QLatin1String("fabric")) {
        fetchBytes(QStringLiteral("https://meta.fabricmc.net/v2/versions/loader/") + mcVersion, 3,
                   [emitIfCurrent](bool ok, const QByteArray& data) {
                       QStringList out;
                       if (ok)
                           for (const QJsonValue& v : QJsonDocument::fromJson(data).array())
                               out << v.toObject()[QStringLiteral("loader")].toObject()[QStringLiteral("version")].toString();
                       emitIfCurrent(out);
                   });
        return;
    }

    if (loader == QLatin1String("forge")) {
        requestForgeVersions(mcVersion, seq);
        return;
    }

    if (loader == QLatin1String("neoforge")) {
        fetchBytes(QStringLiteral("https://maven.neoforged.net/api/maven/versions/releases/net/neoforged/neoforge"), 3,
                   [emitIfCurrent, mcVersion](bool ok, const QByteArray& data) {
                       QStringList out;
                       if (ok) {
                           const QJsonDocument doc = QJsonDocument::fromJson(data);
                           const QJsonArray versions = doc.isArray() ? doc.array() : doc.object()[QStringLiteral("versions")].toArray();

                           const QStringList mcParts = mcVersion.split('.');
                           QString prefix;
                           if (mcParts.size() >= 2)
                               prefix = mcParts[1] + QLatin1Char('.') + (mcParts.size() >= 3 ? mcParts[2] : QStringLiteral("0")) + QLatin1Char('.');

                           for (const QJsonValue& v : versions) {
                               const QString ver = v.toString();
                               if (prefix.isEmpty() || ver.startsWith(prefix)) out.prepend(ver);
                           }
                           newDebug() << "[LocalBuilds] neoforge:" << out.size() << "versions for prefix" << prefix;
                       }
                       emitIfCurrent(out);
                   });
        return;
    }

    emitIfCurrent(QStringList());
}

void LocalBuildsManager::requestForgeVersions(const QString& mcVersion, quint64 seq)
{
    const QString first = QStringLiteral("https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml");
    const QString second = QStringLiteral("https://files.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml");

    auto parse = [mcVersion](const QByteArray& data) {
        QStringList out;
        const QString prefix = mcVersion + QLatin1Char('-');
        QXmlStreamReader xml(data);
        bool inVersion = false;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name().toString() == QLatin1String("version")) inVersion = true;
            else if (xml.isEndElement() && xml.name().toString() == QLatin1String("version")) inVersion = false;
            else if (inVersion && xml.isCharacters()) {
                const QString v = xml.text().toString().trimmed();
                if (v.startsWith(prefix)) out.prepend(v.mid(prefix.size()));
            }
        }
        return out;
    };

    fetchBytes(first, 2, [this, parse, second, mcVersion, seq](bool ok, const QByteArray& data) {
        const QStringList out = ok ? parse(data) : QStringList();
        if (!out.isEmpty()) { if (seq == m_loaderReqSeq) emit loaderVersionsReady(out); return; }

        fetchBytes(second, 2, [this, parse, mcVersion, seq](bool ok2, const QByteArray& data2) {
            const QStringList out2 = ok2 ? parse(data2) : QStringList();
            if (!out2.isEmpty()) { if (seq == m_loaderReqSeq) emit loaderVersionsReady(out2); return; }
            requestForgePromotions(mcVersion, seq);
        });
    });
}

void LocalBuildsManager::requestForgePromotions(const QString& mcVersion, quint64 seq)
{
    fetchBytes(QStringLiteral("https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json"), 2,
               [this, mcVersion, seq](bool ok, const QByteArray& data) {
                   QStringList out;
                   if (ok) {
                       const QJsonObject promos = QJsonDocument::fromJson(data).object()[QStringLiteral("promos")].toObject();
                       const QString rec = promos.value(mcVersion + QStringLiteral("-recommended")).toString();
                       const QString lat = promos.value(mcVersion + QStringLiteral("-latest")).toString();
                       if (!rec.isEmpty()) out << rec;
                       if (!lat.isEmpty() && lat != rec) out << lat;
                   }
                   newDebug() << "[LocalBuilds] forge promotions fallback:" << out.size() << "versions";
                   if (seq == m_loaderReqSeq) emit loaderVersionsReady(out);
               });
}