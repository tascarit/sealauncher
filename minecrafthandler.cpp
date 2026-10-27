#include "minecrafthandler.h"
#include "NewDebug.h"

#include <QSaveFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QDateTime>
#include <QSet>
#include <QSysInfo>
#include <QUrl>
#include <algorithm>

#include <private/qzipreader_p.h>

namespace {

constexpr const char* kManifestUrl  = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
constexpr const char* kBuildsUrl    = "https://raw.githubusercontent.com/tascarit/MinecraftSborki/refs/heads/main/builds.json";
constexpr const char* kNewsUrl      = "https://raw.githubusercontent.com/tascarit/MinecraftSborki/refs/heads/main/news.json";
constexpr const char* kAssetsUrl    = "https://resources.download.minecraft.net/";
constexpr const char* kFabricMaven  = "https://maven.fabricmc.net/";

constexpr qint64 kMinFreeSpaceBytes = 1LL * 1024 * 1024 * 1024;

QString fmtMb(qint64 bytes)   { return QString::number(double(bytes) / 1048576.0, 'f', 1); }
QString fmtSpeed(double bps)  { return QString::number(bps / 1048576.0, 'f', 1); }

QString fmtEta(qint64 remainingBytes, double bps)
{
    if (bps < 1024.0 || remainingBytes <= 0) return QString();
    const qint64 sec = qint64(double(remainingBytes) / bps);
    if (sec < 5) return QString();
    if (sec < 60) return QStringLiteral("  •  ещё ~%1 с").arg(sec);
    if (sec < 3600) return QStringLiteral("  •  ещё ~%1 мин %2 с").arg(sec / 60).arg(sec % 60);
    return QStringLiteral("  •  ещё ~%1 ч %2 мин").arg(sec / 3600).arg((sec % 3600) / 60);
}

QString normZipPath(QString p)
{
    p.replace('\\', '/');
    while (p.startsWith('/')) p.remove(0, 1);
    while (p.endsWith('/')) p.chop(1);
    return p;
}

bool isSafeBuildName(const QString& n)
{
    const QString t = n.trimmed();
    if (t.isEmpty() || t != n || t.size() > 50) return false;
    if (t == QLatin1String(".") || t == QLatin1String("..") || t.endsWith('.')) return false;
    static const QString bad = QStringLiteral("/\\:*?\"<>|");
    for (const QChar c : t)
        if (bad.contains(c) || c.unicode() < 32) return false;
    static const QStringList reserved = {
        QStringLiteral("con"), QStringLiteral("prn"), QStringLiteral("aux"), QStringLiteral("nul"),
        QStringLiteral("com1"), QStringLiteral("com2"), QStringLiteral("com3"), QStringLiteral("com4"),
        QStringLiteral("com5"), QStringLiteral("com6"), QStringLiteral("com7"), QStringLiteral("com8"), QStringLiteral("com9"),
        QStringLiteral("lpt1"), QStringLiteral("lpt2"), QStringLiteral("lpt3"), QStringLiteral("lpt4"),
        QStringLiteral("lpt5"), QStringLiteral("lpt6"), QStringLiteral("lpt7"), QStringLiteral("lpt8"), QStringLiteral("lpt9")
    };
    if (reserved.contains(t.section('.', 0, 0).toLower())) return false;
    return true;
}

bool ruleMatchesOs(const QJsonObject& rule)
{
    const QJsonObject os = rule.value(QStringLiteral("os")).toObject();
    if (os.isEmpty()) return true;

    if (os.contains(QStringLiteral("name")) &&
        os.value(QStringLiteral("name")).toString().toLower() != QLatin1String("windows"))
        return false;

    if (os.contains(QStringLiteral("arch"))) {
        const QString cpu = QSysInfo::currentCpuArchitecture();
        const QString cur = cpu.contains(QLatin1String("arm"), Qt::CaseInsensitive) ? QStringLiteral("arm64")
                                                                                    : (cpu.contains(QLatin1String("64")) ? QStringLiteral("x86_64") : QStringLiteral("x86"));
        if (os.value(QStringLiteral("arch")).toString() != cur) return false;
    }
    return true;
}

bool rulesAllow(const QJsonArray& rules)
{
    if (rules.isEmpty()) return true;
    bool allowed = false;
    for (const QJsonValue& rv : rules) {
        const QJsonObject rule = rv.toObject();
        if (ruleMatchesOs(rule))
            allowed = (rule.value(QStringLiteral("action")).toString() == QLatin1String("allow"));
    }
    return allowed;
}

bool writeFileAtomic(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    if (f.write(data) != data.size()) { f.cancelWriting(); return false; }
    return f.commit();
}

QJsonObject readJsonObject(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QJsonObject();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return QJsonObject();
    return doc.object();
}

QString mavenPathFromName(const QString& name)
{
    QString n = name;
    QString ext = QStringLiteral("jar");
    const int at = n.indexOf('@');
    if (at >= 0) { ext = n.mid(at + 1); n = n.left(at); }
    const QStringList parts = n.split(':');
    if (parts.size() < 3) return QString();
    QString group = parts[0];
    group.replace('.', '/');
    const QString file = parts[1] + QLatin1Char('-') + parts[2]
                         + (parts.size() >= 4 ? QLatin1Char('-') + parts[3] : QString())
                         + QLatin1Char('.') + ext;
    return group + QLatin1Char('/') + parts[1] + QLatin1Char('/') + parts[2] + QLatin1Char('/') + file;
}

QString versionJsonFile(const QString& gamePath, const QString& versionId)
{
    return gamePath + QStringLiteral("/versions/") + versionId + QLatin1Char('/') + versionId + QStringLiteral(".json");
}

}

struct MinecraftHandler::FetchCtx {
    QString url;
    int attempt = 0;
    int retries = 3;
    bool bind = false;
    quint64 gen = 0;
    FetchCb cb;
};

struct MinecraftHandler::MergeState {
    QFile out;
    QFile in;
    int partIdx = 0;
    qint64 copied = 0;
    qint64 total = 0;
    quint64 gen = 0;
};

struct MinecraftHandler::ExtractState {
    std::unique_ptr<QZipReader> zip;
    QList<QZipReader::FileInfo> files;
    QString prefix;
    QString targetDir;
    QString targetClean;
    int index = 0;
    int written = 0;
    int failed = 0;
    QString firstError;
    quint64 gen = 0;
};

MinecraftHandler::MinecraftHandler(QObject *parent)
    : QObject(parent), m_manager(new QNetworkAccessManager(this))
{
    m_batch = new DownloadBatch(this);
    m_ranged = new RangedDownload(this);

    m_progressThrottle = new QTimer(this);
    m_progressThrottle->setInterval(100);
    connect(m_progressThrottle, &QTimer::timeout, this, &MinecraftHandler::flushProgress);

    m_fetchRetryTimer = new QTimer(this);
    m_fetchRetryTimer->setSingleShot(true);
    m_fetchRetryTimer->setInterval(30000);
    connect(m_fetchRetryTimer, &QTimer::timeout, this, [this]() {
        if (!m_offline) return;
        fetchBuildsList();
        fetchNews();
    });
}

MinecraftHandler::~MinecraftHandler()
{
    if (m_installerProc) {
        m_installerProc->disconnect(this);
        m_installerProc->kill();
        m_installerProc->waitForFinished(1000);
    }
}

void MinecraftHandler::Initialize(QQuickView* v, QmlHandler* _q, SettingsManager* _sm, SettingsController* _sc)
{
    view = v; q = _q; sm = _sm; sc = _sc;
}

QString MinecraftHandler::gamePathFor(const QString& name) const
{
    return sm->gameDir() + QLatin1Char('/') + name;
}

QString MinecraftHandler::versionIdFor(const BuildSpec& s)
{
    if (s.loader == QLatin1String("vanilla"))  return s.mcVersion;
    if (s.loader == QLatin1String("fabric"))   return QStringLiteral("fabric-loader-%1-%2").arg(s.loaderVersion, s.mcVersion);
    if (s.loader == QLatin1String("forge"))    return QStringLiteral("%1-forge-%2").arg(s.mcVersion, s.loaderVersion);
    if (s.loader == QLatin1String("neoforge")) return QStringLiteral("neoforge-%1").arg(s.loaderVersion);
    return s.mcVersion;
}

QString MinecraftHandler::getVersionId() const
{
    return versionIdFor(m_sel);
}

void MinecraftHandler::setBuildExists(bool v)
{
    if (m_buildExists == v) return;
    m_buildExists = v;
    emit buildExistsChanged();
}

void MinecraftHandler::setUpdateAvailable(bool v)
{
    if (m_updateAvailable == v) return;
    m_updateAvailable = v;
    emit updateAvailableChanged();
}

void MinecraftHandler::setInstallation(bool v)
{
    if (m_installation == v) return;
    m_installation = v;
    emit installationChanged();
}

void MinecraftHandler::setCanDelete(bool v)
{
    if (m_canDelete == v) return;
    m_canDelete = v;
    emit canDeleteChanged();
}

void MinecraftHandler::setBusy(bool v)
{
    if (m_busy == v) return;
    m_busy = v;
    emit busyChanged();
}

void MinecraftHandler::setOffline(bool v)
{
    if (m_offline == v) return;
    m_offline = v;
    emit offlineChanged();
}

void MinecraftHandler::refreshSelectedState()
{
    bool exists = false;
    bool update = false;
    bool hasFiles = false;

    const bool activeIsSelected = m_installing && !m_sel.name.isEmpty() && m_job.name == m_sel.name;

    if (sm && !m_sel.name.isEmpty() && isSafeBuildName(m_sel.name)) {
        const QString gp = gamePathFor(m_sel.name);
        hasFiles = QDir(gp).exists();

        if (!activeIsSelected && hasFiles) {
            const bool interrupted = QFileInfo::exists(gp + QStringLiteral("/installation"));
            const bool hasVersion = QFileInfo::exists(versionJsonFile(gp, versionIdFor(m_sel)));
            exists = hasVersion && !interrupted;

            if (exists) {
                m_installedPackVersion = readInstalledPackVersion(gp);
                update = (m_installedPackVersion.trimmed() != m_sel.packVersion.trimmed());
            }
        }
    }

    newDebug() << "[State]" << m_sel.name << "exists:" << exists << "update:" << update
               << "installing:" << activeIsSelected << "hasFiles:" << hasFiles;

    setInstallation(activeIsSelected);
    setBuildExists(exists);
    setUpdateAvailable(update);
    setCanDelete(hasFiles && !activeIsSelected);

    const QString shownInstalled = exists ? m_installedPackVersion : QString();
    if (shownInstalled != m_shownInstalled || m_sel.packVersion != m_shownSelected) {
        m_shownInstalled = shownInstalled;
        m_shownSelected = m_sel.packVersion;
        emit packVersionsChanged();
    }
}

void MinecraftHandler::refreshState()
{
    refreshSelectedState();
}

void MinecraftHandler::reCheckBuilds(const QString& build, const QString& version, const QString& loader,
                                     const QString& mcVer, const QString& git, const int parts, const QString& packVer)
{
    newDebug() << "Rechecking build:" << build << "MC" << mcVer << loader << version << "pack" << packVer;

    m_sel.name = build;
    m_sel.loaderVersion = version;
    m_sel.loader = loader.isEmpty() ? QStringLiteral("vanilla") : loader;
    m_sel.mcVersion = mcVer;
    m_sel.archiveLink = git;
    m_sel.parts = qMax(1, parts);
    m_sel.packVersion = packVer.trimmed().isEmpty() ? QStringLiteral("0.1") : packVer.trimmed();

    refreshSelectedState();
}

QString MinecraftHandler::readInstalledPackVersion(const QString& gamePath) const
{
    const QJsonObject o = readJsonObject(gamePath + QStringLiteral("/sealauncher_meta.json"));
    const QString ver = o.value(QStringLiteral("packVersion")).toString();
    return ver.isEmpty() ? QStringLiteral("0.1") : ver;
}

bool MinecraftHandler::loaderAlreadyInstalled(const QString& gamePath, const BuildSpec& spec) const
{
    const QJsonObject o = readJsonObject(gamePath + QStringLiteral("/sealauncher_meta.json"));
    if (o.isEmpty()) return false;
    if (o.value(QStringLiteral("loader")).toString() != spec.loader) return false;
    if (o.value(QStringLiteral("loaderVersion")).toString() != spec.loaderVersion) return false;
    if (o.value(QStringLiteral("mcVersion")).toString() != spec.mcVersion) return false;
    return QFileInfo::exists(versionJsonFile(gamePath, versionIdFor(spec)));
}

bool MinecraftHandler::writeBuildMeta(const QString& gamePath, const BuildSpec& spec)
{
    QJsonObject o;
    o[QStringLiteral("packVersion")] = spec.packVersion;
    o[QStringLiteral("loaderVersion")] = spec.loaderVersion;
    o[QStringLiteral("loader")] = spec.loader;
    o[QStringLiteral("mcVersion")] = spec.mcVersion;
    o[QStringLiteral("git")] = spec.archiveLink;
    o[QStringLiteral("parts")] = spec.parts;
    o[QStringLiteral("installedAt")] = double(QDateTime::currentSecsSinceEpoch());

    const bool written = writeFileAtomic(gamePath + QStringLiteral("/sealauncher_meta.json"),
                                         QJsonDocument(o).toJson(QJsonDocument::Indented));
    const bool verified = written && readInstalledPackVersion(gamePath).trimmed() == spec.packVersion.trimmed();
    if (!verified)
        newDebug() << "[Meta] Cannot write or verify meta in" << gamePath;
    else
        newDebug() << "[Meta] Written pack version:" << spec.packVersion;
    return verified;
}

void MinecraftHandler::writeInstallMarker(const QString& gamePath) const
{
    QFile f(gamePath + QStringLiteral("/installation"));
    if (f.open(QIODevice::WriteOnly)) f.write("1");
}

void MinecraftHandler::removeInstallMarker(const QString& gamePath) const
{
    QFile::remove(gamePath + QStringLiteral("/installation"));
}

void MinecraftHandler::prepareForUpdate(const QString& gamePath, bool keepLoader)
{
    QStringList keep = {
        QStringLiteral("saves"), QStringLiteral("options.txt"), QStringLiteral("optionsof.txt"),
        QStringLiteral("servers.dat"), QStringLiteral("screenshots"), QStringLiteral("resourcepacks"),
        QStringLiteral("shaderpacks"), QStringLiteral("sealauncher_meta.json"), QStringLiteral("assets"),
        QStringLiteral("installation"), QStringLiteral(".sl_tmp")
    };
    if (keepLoader) keep << QStringLiteral("versions") << QStringLiteral("libraries");

    const QFileInfoList entries = QDir(gamePath).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QFileInfo& fi : entries) {
        if (keep.contains(fi.fileName(), Qt::CaseInsensitive)) continue;

        bool ok = fi.isDir() && !fi.isSymLink()
                      ? QDir(fi.absoluteFilePath()).removeRecursively()
                      : QFile::remove(fi.absoluteFilePath());
        if (!ok) newDebug() << "[Update] Failed to remove:" << fi.absoluteFilePath();
    }
}

void MinecraftHandler::reportProgress(const QString& stage, const QString& details, double progress, bool immediate)
{
    if (!m_installing || !q) return;

    m_pendingStage = stage;
    m_pendingDetails = details;
    m_pendingProgress = progress;
    m_hasPendingUpdate = true;

    if (immediate || !m_progressClock.isValid() || m_progressClock.elapsed() >= 100)
        flushProgress();
}

void MinecraftHandler::flushProgress()
{
    if (!m_hasPendingUpdate || !m_installing || !q) return;
    m_hasPendingUpdate = false;
    m_progressClock.restart();
    emit q->installProgress(m_pendingStage, m_pendingDetails, m_pendingProgress);
}

void MinecraftHandler::defer(quint64 gen, std::function<void()> fn)
{
    QTimer::singleShot(0, this, [this, gen, fn = std::move(fn)]() {
        if (gen != m_opGen || !m_installing) return;
        fn();
    });
}

void MinecraftHandler::beginInstall(const BuildSpec& spec, bool update)
{
    if (m_installing || !sm || !q) return;

    auto reject = [this, &spec](const QString& title, const QString& msg, const QString& details) {
        emit q->installError(title, msg, details, false);
        emit installFailed(spec.name, msg, false);
    };

    if (!isSafeBuildName(spec.name)) {
        reject(QStringLiteral("Некорректное имя сборки"),
               QStringLiteral("Имя «%1» нельзя использовать как имя папки.").arg(spec.name), QString());
        return;
    }

    const QString gamePath = gamePathFor(spec.name);
    if (!QDir().mkpath(gamePath)) {
        reject(QStringLiteral("Не удалось создать папку сборки"),
               QStringLiteral("Проверьте путь установки и права на запись."), gamePath);
        return;
    }

    const QStorageInfo storage(gamePath);
    if (storage.isValid() && storage.bytesAvailable() >= 0 && storage.bytesAvailable() < kMinFreeSpaceBytes) {
        reject(QStringLiteral("Недостаточно места на диске"),
               QStringLiteral("Для установки нужно не менее 1 ГБ свободного места (сейчас %1 МБ).")
                   .arg(storage.bytesAvailable() / 1048576), gamePath);
        return;
    }

    m_job = spec;
    m_jobUpdate = update;
    m_jobPath = gamePath;
    m_tmpDir = gamePath + QStringLiteral("/.sl_tmp");
    m_jobSkipLoader = update && !spec.archiveLink.trimmed().isEmpty() && loaderAlreadyInstalled(gamePath, spec);
    m_zipPath.clear();
    m_installerAttempt = 0;
    m_retryFn = nullptr;
    m_failedJobName.clear();

    ++m_opGen;
    m_installing = true;

    QDir().mkpath(m_tmpDir);
    if (!update) writeInstallMarker(gamePath);

    m_progressClock.invalidate();
    m_progressThrottle->start();
    setBusy(true);
    refreshSelectedState();

    newDebug() << "[Install] begin" << spec.name << "MC" << spec.mcVersion << spec.loader << spec.loaderVersion
               << "update:" << update << "skipLoader:" << m_jobSkipLoader;

    reportProgress(update ? QStringLiteral("Обновление сборки") : QStringLiteral("Установка сборки"),
                   spec.name, -1.0, true);
    stageArchive();
}

void MinecraftHandler::endJob()
{
    m_installing = false;
    ++m_opGen;

    m_progressThrottle->stop();
    m_hasPendingUpdate = false;

    if (m_batch) m_batch->cancel();
    if (m_ranged) m_ranged->cancel();

    if (m_installerProc) {
        m_installerProc->disconnect(this);
        m_installerProc->kill();
        m_installerProc->deleteLater();
        m_installerProc = nullptr;
    }

    if (m_merge) { m_merge->in.close(); m_merge->out.close(); m_merge->out.remove(); m_merge.reset(); }
    m_extract.reset();

    setBusy(false);
    if (q) emit q->installEnded();
}

void MinecraftHandler::failJob(const QString& title, const QString& message, const QString& details, bool canRetry)
{
    if (!m_installing) return;

    newDebug() << "[Install] FAILED:" << title << message << details.left(300);

    const BuildSpec job = m_job;
    const bool update = m_jobUpdate;
    endJob();

    if (canRetry) {
        m_failedJobName = job.name;
        m_retryFn = [this, job, update]() { beginInstall(job, update); };
    } else {
        m_failedJobName.clear();
        m_retryFn = nullptr;
    }

    refreshSelectedState();
    if (sc && view && !view->isActive())
        sc->notify(QStringLiteral("Ошибка установки"), QStringLiteral("«%1»: %2").arg(job.name, title));
    if (q) emit q->installError(title, message, details, canRetry);
    emit installFailed(job.name, message, canRetry);
}

void MinecraftHandler::finishInstall()
{
    if (!m_installing) return;

    const BuildSpec job = m_job;
    const QString path = m_jobPath;

    if (!writeBuildMeta(path, job)) {
        failJob(QStringLiteral("Не удалось сохранить данные сборки"),
                QStringLiteral("Файл sealauncher_meta.json не записался: проверьте права на папку и антивирус, затем нажмите «Повторить»."),
                path, true);
        return;
    }
    removeInstallMarker(path);
    QDir(m_tmpDir).removeRecursively();

    reportProgress(QStringLiteral("Готово"), QStringLiteral("Сборка установлена"), 1.0, true);
    endJob();
    m_retryFn = nullptr;
    m_failedJobName.clear();

    newDebug() << "[Install] done" << job.name;
    refreshSelectedState();
    if (sc && view && !view->isActive())
        sc->notify(QStringLiteral("SeaLauncher"), QStringLiteral("Сборка «%1» установлена — можно играть!").arg(job.name));
    emit installSucceeded(job.name);
    emit buildsChanged();
}

void MinecraftHandler::cancelInstallation()
{
    if (!m_installing) return;
    const QString name = m_job.name;
    newDebug() << "[Install] cancelled by user:" << name;

    endJob();
    m_retryFn = nullptr;
    m_failedJobName.clear();
    refreshSelectedState();
    emit installAbandoned(name);
}

void MinecraftHandler::retryLastOperation()
{
    if (m_installing) return;
    const std::function<void()> fn = m_retryFn;
    m_retryFn = nullptr;
    m_failedJobName.clear();
    if (fn) fn();
}

void MinecraftHandler::discardFailedOperation()
{
    const QString name = m_failedJobName;
    m_retryFn = nullptr;
    m_failedJobName.clear();
    if (!name.isEmpty()) emit installAbandoned(name);
}

void MinecraftHandler::installLocalBuild(const QString& buildName, const QString& mcVersion,
                                         const QString& loader, const QString& loaderVersion)
{
    if (m_installing) {
        if (q) emit q->installError(QStringLiteral("Идёт другая установка"),
                                 QStringLiteral("Дождитесь окончания установки «%1».").arg(m_job.name), QString(), false);
        emit installFailed(buildName, QStringLiteral("Идёт другая установка"), false);
        return;
    }

    BuildSpec spec;
    spec.name = buildName;
    spec.mcVersion = mcVersion;
    spec.loader = loader;
    spec.loaderVersion = loaderVersion;
    spec.packVersion = QStringLiteral("0.1");
    beginInstall(spec, false);
}

void MinecraftHandler::deleteCurrentBuild()
{
    if (!q || !sm || m_sel.name.isEmpty() || !isSafeBuildName(m_sel.name)) return;

    if (m_installing && m_job.name == m_sel.name) {
        emit q->installError(QStringLiteral("Сборка устанавливается"),
                             QStringLiteral("Отмените установку или дождитесь её окончания."), QString(), false);
        return;
    }
    if (m_minecraftProcess && m_minecraftProcess->state() != QProcess::NotRunning) {
        emit q->installError(QStringLiteral("Minecraft запущен"),
                             QStringLiteral("Закройте игру, чтобы удалить сборку."), QString(), false);
        return;
    }

    const QString path = gamePathFor(m_sel.name);
    QDir dir(path);
    if (dir.exists() && !dir.removeRecursively()) {
        emit q->installError(QStringLiteral("Не удалось удалить сборку"),
                             QStringLiteral("Часть файлов занята другой программой. Закройте её и повторите."), path, false);
    } else {
        newDebug() << "[Delete] Removed:" << path;
    }
    refreshSelectedState();
}

void MinecraftHandler::openLauncherDir()
{
    if (!sm) return;
    const QString path = sm->gameDir();
    QDir().mkpath(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MinecraftHandler::openBuildDir()
{
    if (!sm || m_sel.name.isEmpty() || !isSafeBuildName(m_sel.name)) return;
    const QString path = gamePathFor(m_sel.name);
    if (!QDir(path).exists()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MinecraftHandler::ensureLauncherProfile(const QString& gameDir)
{
    QDir().mkpath(gameDir);
    const QString path = gameDir + QStringLiteral("/launcher_profiles.json");
    if (QFileInfo::exists(path)) return;

    QJsonObject root;
    root[QStringLiteral("profiles")] = QJsonObject();
    root[QStringLiteral("selectedProfile")] = QString();
    root[QStringLiteral("clientToken")] = QStringLiteral("8aa19a0b-733b-4153-8098-6214d930881c");
    root[QStringLiteral("authenticationDatabase")] = QJsonObject();
    root[QStringLiteral("launcherVersion")] = QJsonObject{{"name", "SeaLauncher"}, {"format", 21}, {"profilesFormat", 21}};

    if (!writeFileAtomic(path, QJsonDocument(root).toJson(QJsonDocument::Indented)))
        qWarning() << "[Installer] Не удалось создать launcher_profiles.json в" << path;
}

namespace {

bool versionJsonUsable(const QString& path)
{
    const QJsonObject o = readJsonObject(path);
    return !o.isEmpty() && o.contains(QStringLiteral("libraries"));
}

void extractNatives(const QList<QJsonObject>& chain, const QString& gamePath)
{
    const QString dir = gamePath + QStringLiteral("/natives");
    QDir().mkpath(dir);
    if (!QDir(dir).entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty()) return;

    const QString arch = QSysInfo::currentCpuArchitecture().contains(QLatin1String("64")) ? QStringLiteral("64") : QStringLiteral("32");
    const QString cleanDir = QDir::cleanPath(QDir(dir).absolutePath());

    for (const QJsonObject& j : chain) {
        for (const QJsonValue& v : j.value(QStringLiteral("libraries")).toArray()) {
            const QJsonObject lib = v.toObject();
            if (!rulesAllow(lib.value(QStringLiteral("rules")).toArray())) continue;

            QString classifier = lib.value(QStringLiteral("natives")).toObject().value(QStringLiteral("windows")).toString();
            if (classifier.isEmpty()) continue;
            classifier.replace(QStringLiteral("${arch}"), arch);

            const QString rel = lib.value(QStringLiteral("downloads")).toObject().value(QStringLiteral("classifiers")).toObject()
                                    .value(classifier).toObject().value(QStringLiteral("path")).toString();
            if (rel.isEmpty()) continue;

            QZipReader zip(gamePath + QStringLiteral("/libraries/") + rel);
            if (!zip.isReadable()) continue;

            QStringList exclude;
            for (const QJsonValue& e : lib.value(QStringLiteral("extract")).toObject().value(QStringLiteral("exclude")).toArray())
                exclude << e.toString();

            for (const QZipReader::FileInfo& fi : zip.fileInfoList()) {
                if (!fi.isFile) continue;
                const QString name = normZipPath(fi.filePath);
                bool skip = name.isEmpty() || name.startsWith(QStringLiteral("META-INF"), Qt::CaseInsensitive);
                for (const QString& ex : exclude)
                    if (!ex.isEmpty() && name.startsWith(ex)) skip = true;
                if (skip) continue;

                const QString outPath = QDir::cleanPath(QDir(dir).absoluteFilePath(name));
                if (!outPath.startsWith(cleanDir + QLatin1Char('/'))) continue;
                QDir().mkpath(QFileInfo(outPath).absolutePath());

                QFile out(outPath);
                if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) out.write(zip.fileData(fi.filePath));
            }
        }
    }
}

QString detectWrapperPrefix(const QList<QZipReader::FileInfo>& files)
{
    static const QStringList gameDirs = {
        QStringLiteral("mods"), QStringLiteral("config"), QStringLiteral("saves"),
        QStringLiteral("resourcepacks"), QStringLiteral("shaderpacks"), QStringLiteral("versions"),
        QStringLiteral("libraries"), QStringLiteral("kubejs"), QStringLiteral("defaultconfigs"),
        QStringLiteral("scripts"), QStringLiteral("assets")
    };

    QStringList paths;
    paths.reserve(files.size());
    for (const QZipReader::FileInfo& fi : files) paths << normZipPath(fi.filePath);

    QString prefix;
    while (true) {
        QSet<QString> top;
        for (const QString& p0 : paths) {
            if (!p0.startsWith(prefix)) continue;
            const QString p = p0.mid(prefix.length());
            if (p.isEmpty()) continue;
            const int slash = p.indexOf('/');
            top.insert(slash < 0 ? p : p.left(slash));
        }
        if (top.size() != 1) break;

        const QString candidate = *top.constBegin();
        if (gameDirs.contains(candidate, Qt::CaseInsensitive)) break;

        bool valid = true;
        bool hasContent = false;
        for (const QString& p0 : paths) {
            if (!p0.startsWith(prefix)) continue;
            const QString p = p0.mid(prefix.length());
            if (p.isEmpty() || p == candidate) continue;
            if (!p.startsWith(candidate + QLatin1Char('/'))) { valid = false; break; }
            hasContent = true;
        }
        if (!valid || !hasContent) break;
        prefix += candidate + QLatin1Char('/');
    }
    return prefix;
}

}

void MinecraftHandler::runBatch(const QString& title, const QList<DownloadItem>& items, int parallel, const Cont& onOk)
{
    if (items.isEmpty()) { defer(m_opGen, onOk); return; }

    m_batch->disconnect(this);
    const quint64 gen = m_opGen;

    connect(m_batch, &DownloadBatch::progress, this,
            [this, gen, title](qint64 doneB, qint64 totalB, int doneF, int totalF, double speed) {
                if (gen != m_opGen || !m_installing) return;
                QString details;
                double p = -1.0;
                if (totalB > 0) {
                    details = QStringLiteral("%1 / %2 МБ").arg(fmtMb(doneB), fmtMb(totalB));
                    if (speed > 0) details += QStringLiteral("  (%1 МБ/с)").arg(fmtSpeed(speed));
                    details += fmtEta(totalB - doneB, speed);
                    if (totalF > 1) details += QStringLiteral("  •  файлов: %1 / %2").arg(doneF).arg(totalF);
                    p = double(doneB) / double(totalB);
                } else {
                    details = QStringLiteral("Файлов: %1 / %2").arg(doneF).arg(totalF);
                    p = totalF > 0 ? double(doneF) / double(totalF) : -1.0;
                }
                reportProgress(title, details, qBound(0.0, p, 1.0));
            });

    connect(m_batch, &DownloadBatch::finished, this, [this, gen, title, onOk](bool ok, const QString& err) {
        if (gen != m_opGen || !m_installing) return;
        if (!ok) {
            failJob(title + QStringLiteral(": ошибка загрузки"),
                    QStringLiteral("Не удалось скачать часть файлов (проблемы с сетью?). "
                                   "Уже скачанное сохранено — нажмите «Повторить», загрузка продолжится."),
                    err, true);
            return;
        }
        defer(gen, onOk);
    });

    reportProgress(title, QStringLiteral("Проверка файлов..."), -1.0, true);
    m_batch->start(items, parallel);
}

void MinecraftHandler::fetchText(const QString& url, int retries, bool bindToJob, FetchCb cb)
{
    auto c = std::make_shared<FetchCtx>();
    c->url = url;
    c->retries = retries;
    c->bind = bindToJob;
    c->gen = m_opGen;
    c->cb = std::move(cb);
    fetchAttempt(c);
}

void MinecraftHandler::fetchAttempt(const std::shared_ptr<FetchCtx>& c)
{
    if (c->bind && (c->gen != m_opGen || !m_installing)) return;

    QNetworkReply* reply = m_manager->get(DownloadUtil::makeRequest(QUrl(c->url)));

    auto* wd = new QTimer(reply);
    wd->setSingleShot(true);
    wd->setInterval(20000);
    connect(wd, &QTimer::timeout, reply, [reply]() { reply->abort(); });
    connect(reply, &QNetworkReply::downloadProgress, wd, [wd]() { wd->start(); });
    wd->start();

    connect(reply, &QNetworkReply::finished, this, [this, c, reply]() {
        const int st = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && st >= 200 && st < 300;
        const QByteArray data = ok ? reply->readAll() : QByteArray();
        const QString es = reply->errorString();
        reply->deleteLater();

        if (c->bind && (c->gen != m_opGen || !m_installing)) return;

        if (ok) { c->cb(true, data, QString()); return; }

        if (DownloadUtil::isRetryableHttp(st) && c->attempt < c->retries) {
            c->attempt++;
            newDebug() << "[Fetch] retry" << c->attempt << c->url << es;
            QTimer::singleShot(DownloadUtil::backoffMs(c->attempt), this, [this, c]() { fetchAttempt(c); });
            return;
        }
        c->cb(false, QByteArray(), es);
    });
}

void MinecraftHandler::stageArchive()
{
    if (m_job.archiveLink.trimmed().isEmpty()) { stagePrepare(); return; }

    m_partPaths.clear();
    m_partUrls.clear();

    const int parts = qMax(1, m_job.parts);
    if (parts <= 1) {
        m_zipPath = m_tmpDir + QStringLiteral("/modpack.zip");
        m_partPaths << m_zipPath;
        m_partUrls << m_job.archiveLink;
    } else {
        m_zipPath = m_tmpDir + QStringLiteral("/modpack.zip");
        for (int i = 1; i <= parts; ++i) {
            m_partPaths << QStringLiteral("%1/modpack.part%2").arg(m_tmpDir).arg(i);
            m_partUrls  << QStringLiteral("%1/modpack.part%2").arg(m_job.archiveLink).arg(i);
        }
        if (QFileInfo::exists(m_zipPath)) { stagePrepare(); return; }
    }

    m_partIndex = 0;
    downloadNextPart();
}

void MinecraftHandler::downloadNextPart()
{
    if (m_partIndex >= m_partUrls.size()) { mergeParts(); return; }

    const int i = m_partIndex;
    const int n = m_partUrls.size();
    const quint64 gen = m_opGen;

    m_ranged->disconnect(this);

    connect(m_ranged, &RangedDownload::progress, this, [this, i, n, gen](qint64 done, qint64 total, double speed) {
        if (gen != m_opGen || !m_installing) return;
        QString details;
        double p = -1.0;
        if (total > 0) {
            details = QStringLiteral("%1 / %2 МБ").arg(fmtMb(done), fmtMb(total));
            if (speed > 0) details += QStringLiteral("  (%1 МБ/с)").arg(fmtSpeed(speed));
            details += fmtEta(total - done, speed);
            p = double(done) / double(total);
        }
        if (n > 1) {
            details = QStringLiteral("Часть %1 из %2  •  ").arg(i + 1).arg(n) + details;
            if (p >= 0) p = (double(i) + p) / double(n);
        }
        reportProgress(QStringLiteral("Загрузка сборки"), details, p);
    });

    connect(m_ranged, &RangedDownload::finished, this, [this, gen](bool ok, const QString& err) {
        if (gen != m_opGen || !m_installing) return;
        if (!ok) {
            failJob(QStringLiteral("Ошибка загрузки сборки"),
                    QStringLiteral("Не удалось скачать архив сборки. Уже скачанная часть сохранена — "
                                   "нажмите «Повторить», загрузка продолжится с места обрыва."),
                    err, true);
            return;
        }
        m_partIndex++;
        defer(gen, [this]() { downloadNextPart(); });
    });

    reportProgress(QStringLiteral("Загрузка сборки"),
                   n > 1 ? QStringLiteral("Часть %1 из %2").arg(i + 1).arg(n) : QStringLiteral("Подключение..."),
                   -1.0, true);
    m_ranged->start(m_partUrls.at(i), m_partPaths.at(i), 16, 8);
}

void MinecraftHandler::mergeParts()
{
    if (m_partPaths.size() <= 1) { stagePrepare(); return; }

    auto st = std::make_shared<MergeState>();
    st->gen = m_opGen;
    st->out.setFileName(m_zipPath + QStringLiteral(".merge"));
    if (!st->out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        failJob(QStringLiteral("Ошибка склейки"), QStringLiteral("Не удалось создать файл для склейки частей."),
                st->out.errorString(), true);
        return;
    }
    for (const QString& p : m_partPaths) st->total += QFileInfo(p).size();

    m_merge = st;
    reportProgress(QStringLiteral("Склейка частей"), QStringLiteral("Начало..."), 0.0, true);
    QTimer::singleShot(0, this, &MinecraftHandler::mergeStep);
}

void MinecraftHandler::mergeStep()
{
    const std::shared_ptr<MergeState> st = m_merge;
    if (!st || !m_installing || st->gen != m_opGen) return;

    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 30) {
        if (!st->in.isOpen()) {
            if (st->partIdx >= m_partPaths.size()) {
                st->out.close();
                QFile::remove(m_zipPath);
                if (!QFile::rename(st->out.fileName(), m_zipPath)) {
                    failJob(QStringLiteral("Ошибка склейки"), QStringLiteral("Не удалось сохранить склеенный архив."), m_zipPath, true);
                    return;
                }
                for (const QString& p : m_partPaths) {
                    RangedDownload::removeState(p);
                    QFile::remove(p);
                }
                m_merge.reset();
                stagePrepare();
                return;
            }
            st->in.setFileName(m_partPaths.at(st->partIdx));
            if (!st->in.open(QIODevice::ReadOnly)) {
                failJob(QStringLiteral("Ошибка склейки"), QStringLiteral("Не удалось открыть часть архива."), st->in.fileName(), true);
                return;
            }
        }

        const QByteArray buf = st->in.read(4 * 1024 * 1024);
        if (buf.isEmpty()) { st->in.close(); st->partIdx++; continue; }

        if (st->out.write(buf) != buf.size()) {
            failJob(QStringLiteral("Недостаточно места на диске"),
                    QStringLiteral("Не удалось записать склеенный архив. Освободите место и повторите."),
                    st->out.errorString(), true);
            return;
        }
        st->copied += buf.size();
    }

    reportProgress(QStringLiteral("Склейка частей"),
                   QStringLiteral("%1 / %2 МБ").arg(fmtMb(st->copied), fmtMb(st->total)),
                   st->total > 0 ? double(st->copied) / double(st->total) : -1.0);
    QTimer::singleShot(0, this, &MinecraftHandler::mergeStep);
}

void MinecraftHandler::stagePrepare()
{
    if (m_jobUpdate) {
        reportProgress(QStringLiteral("Подготовка обновления"), QStringLiteral("Удаление старых файлов сборки..."), -1.0, true);
        writeInstallMarker(m_jobPath);
        prepareForUpdate(m_jobPath, m_jobSkipLoader);
    }
    defer(m_opGen, [this]() { stageLoader(); });
}

void MinecraftHandler::stageLoader()
{
    if (m_jobSkipLoader) {
        newDebug() << "[Update] Loader unchanged, skipping loader install";
        stageExtract();
        return;
    }

    const QString& l = m_job.loader;
    const Cont toExtract = [this]() { stageExtract(); };

    if (l == QLatin1String("vanilla"))        stageVanilla(toExtract);
    else if (l == QLatin1String("fabric"))    stageVanilla([this]() { stageFabric(); });
    else if (l == QLatin1String("forge"))     stageVanilla([this]() { stageForge(); });
    else if (l == QLatin1String("neoforge"))  stageNeoforge();
    else failJob(QStringLiteral("Неизвестный загрузчик"), QStringLiteral("Загрузчик «%1» не поддерживается.").arg(l), QString(), false);
}

QList<DownloadItem> MinecraftHandler::libraryItems(const QJsonObject& json, const QString& gamePath) const
{
    QList<DownloadItem> out;
    const QString libDir = gamePath + QStringLiteral("/libraries");

    for (const QJsonValue& v : json.value(QStringLiteral("libraries")).toArray()) {
        const QJsonObject lib = v.toObject();
        if (!rulesAllow(lib.value(QStringLiteral("rules")).toArray())) continue;

        DownloadItem it;
        QString rel;

        const QJsonObject art = lib.value(QStringLiteral("downloads")).toObject().value(QStringLiteral("artifact")).toObject();
        if (!art.isEmpty()) {
            rel = art.value(QStringLiteral("path")).toString();
            it.url = art.value(QStringLiteral("url")).toString();
            it.size = art.contains(QStringLiteral("size")) ? qint64(art.value(QStringLiteral("size")).toDouble()) : -1;
            it.sha1 = art.value(QStringLiteral("sha1")).toString();
        } else if (lib.contains(QStringLiteral("name")) && !lib.contains(QStringLiteral("natives"))
                   && (lib.contains(QStringLiteral("url")) || !lib.contains(QStringLiteral("downloads")))) {
            rel = mavenPathFromName(lib.value(QStringLiteral("name")).toString());
            QString base = lib.value(QStringLiteral("url")).toString();
            if (base.isEmpty()) base = QString::fromLatin1(kFabricMaven);
            if (!base.endsWith('/')) base += '/';
            if (!rel.isEmpty()) it.url = base + rel;
            it.size = lib.contains(QStringLiteral("size")) ? qint64(lib.value(QStringLiteral("size")).toDouble()) : -1;
            it.sha1 = lib.value(QStringLiteral("sha1")).toString();
        }

        QString nativeClassifier = lib.value(QStringLiteral("natives")).toObject().value(QStringLiteral("windows")).toString();
        if (!nativeClassifier.isEmpty()) {
            nativeClassifier.replace(QStringLiteral("${arch}"),
                                     QSysInfo::currentCpuArchitecture().contains(QLatin1String("64")) ? QStringLiteral("64") : QStringLiteral("32"));
            const QJsonObject cls = lib.value(QStringLiteral("downloads")).toObject().value(QStringLiteral("classifiers")).toObject()
                                        .value(nativeClassifier).toObject();
            if (!cls.value(QStringLiteral("url")).toString().isEmpty() && !cls.value(QStringLiteral("path")).toString().isEmpty()) {
                DownloadItem n;
                n.url = cls.value(QStringLiteral("url")).toString();
                n.path = QDir(libDir).absoluteFilePath(cls.value(QStringLiteral("path")).toString());
                n.size = cls.contains(QStringLiteral("size")) ? qint64(cls.value(QStringLiteral("size")).toDouble()) : -1;
                n.sha1 = cls.value(QStringLiteral("sha1")).toString();
                out << n;
            }
        }

        if (rel.isEmpty() || it.url.isEmpty()) continue;
        it.path = QDir(libDir).absoluteFilePath(rel);
        out << it;
    }
    return out;
}

void MinecraftHandler::stageVanilla(const Cont& next)
{
    const QString mc = m_job.mcVersion;
    const QString jsonPath = versionJsonFile(m_jobPath, mc);

    if (versionJsonUsable(jsonPath)) { stageVanillaFiles(next); return; }
    QFile::remove(jsonPath);

    reportProgress(QStringLiteral("Подготовка"), QStringLiteral("Получение списка версий Minecraft..."), -1.0, true);
    fetchText(QString::fromLatin1(kManifestUrl), 5, true,
              [this, mc, jsonPath, next](bool ok, const QByteArray& data, const QString& err) {
                  if (!ok) {
                      failJob(QStringLiteral("Нет связи с серверами Mojang"),
                              QStringLiteral("Не удалось получить список версий Minecraft. Проверьте подключение к интернету."), err, true);
                      return;
                  }

                  QString url, sha1;
                  for (const QJsonValue& v : QJsonDocument::fromJson(data).object().value(QStringLiteral("versions")).toArray()) {
                      const QJsonObject ver = v.toObject();
                      if (ver.value(QStringLiteral("id")).toString() == mc) {
                          url = ver.value(QStringLiteral("url")).toString();
                          sha1 = ver.value(QStringLiteral("sha1")).toString();
                          break;
                      }
                  }
                  if (url.isEmpty()) {
                      failJob(QStringLiteral("Версия Minecraft не найдена"),
                              QStringLiteral("Версии %1 нет в списке Mojang.").arg(mc), QString(), false);
                      return;
                  }

                  DownloadItem it;
                  it.url = url;
                  it.path = jsonPath;
                  it.sha1 = sha1;
                  runBatch(QStringLiteral("Описание версии Minecraft"), {it}, 1, [this, next]() { stageVanillaFiles(next); });
              });
}

void MinecraftHandler::stageVanillaFiles(const Cont& next)
{
    const QString mc = m_job.mcVersion;
    const QString jsonPath = versionJsonFile(m_jobPath, mc);
    const QJsonObject vJson = readJsonObject(jsonPath);
    if (vJson.isEmpty()) {
        QFile::remove(jsonPath);
        failJob(QStringLiteral("Ошибка установки"), QStringLiteral("Описание версии Minecraft повреждено. Повторите попытку."), jsonPath, true);
        return;
    }

    QList<DownloadItem> items;

    const QJsonObject client = vJson.value(QStringLiteral("downloads")).toObject().value(QStringLiteral("client")).toObject();
    if (!client.value(QStringLiteral("url")).toString().isEmpty()) {
        DownloadItem jar;
        jar.url = client.value(QStringLiteral("url")).toString();
        jar.path = m_jobPath + QStringLiteral("/versions/") + mc + QLatin1Char('/') + mc + QStringLiteral(".jar");
        jar.size = client.contains(QStringLiteral("size")) ? qint64(client.value(QStringLiteral("size")).toDouble()) : -1;
        jar.sha1 = client.value(QStringLiteral("sha1")).toString();
        items << jar;
    }

    items += libraryItems(vJson, m_jobPath);

    const QJsonObject ai = vJson.value(QStringLiteral("assetIndex")).toObject();
    if (!ai.value(QStringLiteral("url")).toString().isEmpty()) {
        DownloadItem idx;
        idx.url = ai.value(QStringLiteral("url")).toString();
        idx.path = m_jobPath + QStringLiteral("/assets/indexes/") + ai.value(QStringLiteral("id")).toString(mc) + QStringLiteral(".json");
        idx.size = ai.contains(QStringLiteral("size")) ? qint64(ai.value(QStringLiteral("size")).toDouble()) : -1;
        idx.sha1 = ai.value(QStringLiteral("sha1")).toString();
        items << idx;
    }

    runBatch(QStringLiteral("Загрузка Minecraft"), items, 16, [this, vJson, next]() { stageAssets(vJson, next); });
}

void MinecraftHandler::stageAssets(const QJsonObject& vJson, const Cont& next)
{
    const QString mc = m_job.mcVersion;
    const QString id = vJson.value(QStringLiteral("assetIndex")).toObject().value(QStringLiteral("id")).toString(mc);
    const QString idxPath = m_jobPath + QStringLiteral("/assets/indexes/") + id + QStringLiteral(".json");
    const QString objectsDir = m_jobPath + QStringLiteral("/assets/objects");

    const QJsonObject idx = readJsonObject(idxPath);
    if (idx.isEmpty()) {
        QFile::remove(idxPath);
        failJob(QStringLiteral("Ошибка установки ресурсов"), QStringLiteral("Индекс ресурсов не скачан или повреждён."), idxPath, true);
        return;
    }

    QList<DownloadItem> items;
    const QJsonObject objects = idx.value(QStringLiteral("objects")).toObject();
    items.reserve(objects.size());
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        const QString hash = o.value(QStringLiteral("hash")).toString();
        if (hash.size() < 2) continue;
        const QString sub = hash.left(2);

        DownloadItem d;
        d.url = QString::fromLatin1(kAssetsUrl) + sub + QLatin1Char('/') + hash;
        d.path = objectsDir + QLatin1Char('/') + sub + QLatin1Char('/') + hash;
        d.size = o.contains(QStringLiteral("size")) ? qint64(o.value(QStringLiteral("size")).toDouble()) : -1;
        d.sha1 = hash;
        items << d;
    }

    runBatch(QStringLiteral("Загрузка ресурсов"), items, 24, next);
}

void MinecraftHandler::stageFabric()
{
    const QString id = versionIdFor(m_job);
    const QString jsonPath = versionJsonFile(m_jobPath, id);

    const auto proceed = [this, jsonPath]() {
        const QJsonObject fj = readJsonObject(jsonPath);
        if (fj.isEmpty()) {
            QFile::remove(jsonPath);
            failJob(QStringLiteral("Ошибка установки Fabric"), QStringLiteral("Профиль Fabric повреждён. Повторите попытку."), jsonPath, true);
            return;
        }
        runBatch(QStringLiteral("Загрузка библиотек Fabric"), libraryItems(fj, m_jobPath), 16, [this]() { stageExtract(); });
    };

    if (!readJsonObject(jsonPath).isEmpty()) { proceed(); return; }

    const QString url = QStringLiteral("https://meta.fabricmc.net/v2/versions/loader/%1/%2/profile/json")
                            .arg(m_job.mcVersion, m_job.loaderVersion);
    reportProgress(QStringLiteral("Установка Fabric"), QStringLiteral("Получение профиля загрузчика..."), -1.0, true);

    fetchText(url, 5, true, [this, jsonPath, proceed](bool ok, const QByteArray& data, const QString& err) {
        if (!ok) {
            failJob(QStringLiteral("Нет связи с сервером Fabric"),
                    QStringLiteral("Не удалось получить профиль Fabric. Проверьте подключение к интернету."), err, true);
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject() || !doc.object().contains(QStringLiteral("libraries"))) {
            failJob(QStringLiteral("Ошибка установки Fabric"),
                    QStringLiteral("Сервер Fabric вернул некорректный профиль (такой версии загрузчика может не быть)."),
                    QString::fromUtf8(data.left(300)), false);
            return;
        }
        if (!writeFileAtomic(jsonPath, data)) {
            failJob(QStringLiteral("Ошибка записи"), QStringLiteral("Не удалось сохранить профиль Fabric."), jsonPath, false);
            return;
        }
        proceed();
    });
}

void MinecraftHandler::stageForge()
{
    const QString mc = m_job.mcVersion;
    const QString fv = m_job.loaderVersion;

    DownloadItem it;
    it.url = QStringLiteral("https://maven.minecraftforge.net/net/minecraftforge/forge/%1-%2/forge-%1-%2-installer.jar").arg(mc, fv);
    it.path = m_tmpDir + QStringLiteral("/forge-installer.jar");

    const QString installer = it.path;
    runBatch(QStringLiteral("Загрузка установщика Forge"), {it}, 1, [this, installer]() {
        runInstaller(installer, QStringLiteral("Forge"), [this]() { stageExtract(); });
    });
}

void MinecraftHandler::stageNeoforge()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/downloads");
    QDir().mkpath(dir);

    const QString v = m_job.loaderVersion;
    DownloadItem it;
    it.path = dir + QStringLiteral("/neoforge-%1-installer.jar").arg(v);
    it.url = QStringLiteral("https://maven.neoforged.net/releases/net/neoforged/neoforge/%1/neoforge-%1-installer.jar").arg(v);

    if (QFileInfo::exists(it.path)) {
        QZipReader z(it.path);
        if (!z.isReadable() || z.fileInfoList().isEmpty()) {
            z.close();
            QFile::remove(it.path);
        }
    }

    const QString installer = it.path;
    runBatch(QStringLiteral("Загрузка установщика NeoForge"), {it}, 1, [this, installer]() {
        runInstaller(installer, QStringLiteral("NeoForge"), [this]() {
            stageVanilla([this]() { stageExtract(); });
        });
    });
}

void MinecraftHandler::runInstaller(const QString& installerPath, const QString& label, const Cont& onSuccess)
{
    const QString javaPath = sm->javaPath().trimmed();
    if (javaPath.isEmpty() || !QFileInfo::exists(javaPath)) {
        failJob(QStringLiteral("Java не найдена"),
                QStringLiteral("Для установки %1 нужна Java. Укажите путь к ней в настройках лаунчера.").arg(label),
                javaPath, false);
        return;
    }
    ensureLauncherProfile(m_jobPath);

    struct Ctx {
        QString buffer;
        QStringList tail;
        int processorStep = 0;
        int downloads = 0;
        double progress = -1.0;
    };
    auto ctx = std::make_shared<Ctx>();
    const quint64 gen = m_opGen;
    const QString stageTitle = QStringLiteral("Установка %1").arg(label);

    auto* proc = new QProcess(this);
    m_installerProc = proc;
    proc->setWorkingDirectory(m_jobPath);
    proc->setProcessChannelMode(QProcess::MergedChannels);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc, ctx, gen, stageTitle]() {
        if (gen != m_opGen) { proc->readAllStandardOutput(); return; }

        ctx->buffer.append(QString::fromLocal8Bit(proc->readAllStandardOutput()));
        ctx->buffer.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        ctx->buffer.replace('\r', '\n');
        if (ctx->buffer.size() > 200000) ctx->buffer = ctx->buffer.right(4000);

        int nl;
        while ((nl = ctx->buffer.indexOf('\n')) >= 0) {
            const QString line = ctx->buffer.left(nl).trimmed();
            ctx->buffer.remove(0, nl + 1);
            if (line.isEmpty()) continue;

            ctx->tail << line;
            while (ctx->tail.size() > 40) ctx->tail.removeFirst();
            newDebug() << "+[Installer]" << line;

            QString details = line;
            double p = -1.0;

            if (line.contains(QLatin1String("Extracting json"), Qt::CaseInsensitive)) {
                details = QStringLiteral("Чтение профиля установки"); p = 0.08;
            } else if (line.contains(QLatin1String("Considering minecraft"), Qt::CaseInsensitive)) {
                details = QStringLiteral("Проверка Minecraft"); p = 0.15;
            } else if (line.contains(QLatin1String("Downloading libraries"), Qt::CaseInsensitive)) {
                details = QStringLiteral("Скачивание библиотек"); p = 0.25; ctx->downloads = 0;
            } else if (line.startsWith(QLatin1String("Downloading"), Qt::CaseInsensitive) ||
                       line.startsWith(QLatin1String("Downloaded"), Qt::CaseInsensitive)) {
                QString f = line;
                f.remove(QRegularExpression(QStringLiteral(R"(^Download(ing|ed):?\s*)")));
                if (f.length() > 80) f = f.left(30) + QStringLiteral("...") + f.right(40);
                details = f;
                ctx->downloads++;
                p = 0.25 + qMin(0.20, ctx->downloads * 0.005);
            } else if (line.contains(QLatin1String("Running processor"), Qt::CaseInsensitive)) {
                ctx->processorStep++;
                p = 0.50 + qMin(1.0, ctx->processorStep / 8.0) * 0.40;
                details = QStringLiteral("Ремаппинг (%1)").arg(ctx->processorStep);
            } else if (line.contains(QLatin1String("Building processor classpath"), Qt::CaseInsensitive)) {
                details = QStringLiteral("Финальная сборка"); p = 0.92;
            } else if (line.contains(QLatin1String("Patching minecraft"), Qt::CaseInsensitive)) {
                details = QStringLiteral("Патчинг Minecraft"); p = 0.95;
            } else if (line.contains(QLatin1String("Installation complete"), Qt::CaseInsensitive) ||
                       line.contains(QLatin1String("You can now"), Qt::CaseInsensitive) ||
                       line.contains(QLatin1String("The installation was successful"), Qt::CaseInsensitive)) {
                details = QStringLiteral("Готово"); p = 1.0;
            }

            if (p > ctx->progress) ctx->progress = p;
            reportProgress(stageTitle, details, ctx->progress);
        }
    });

    connect(proc, &QProcess::errorOccurred, this, [this, proc, gen, javaPath](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        const QString es = proc->errorString();
        if (m_installerProc == proc) m_installerProc = nullptr;
        proc->deleteLater();
        if (gen != m_opGen) return;
        failJob(QStringLiteral("Не удалось запустить установщик"),
                QStringLiteral("Java не запустилась. Проверьте путь к Java в настройках."), javaPath + QLatin1Char('\n') + es, false);
    });

    connect(proc, &QProcess::finished, this,
            [this, proc, ctx, gen, installerPath, label, onSuccess](int code, QProcess::ExitStatus status) {
                if (m_installerProc == proc) m_installerProc = nullptr;
                proc->deleteLater();
                if (gen != m_opGen || !m_installing) return;

                if (status == QProcess::NormalExit && code == 0) {
                    newDebug() << "[Installer]" << label << "installed OK";
                    defer(gen, onSuccess);
                    return;
                }

                newDebug() << "[Installer]" << label << "failed, code" << code;

                if (m_installerAttempt < 2) {
                    m_installerAttempt++;
                    reportProgress(QStringLiteral("Установка %1").arg(label),
                                   QStringLiteral("Повторная попытка (%1/2)...").arg(m_installerAttempt), -1.0, true);
                    QTimer::singleShot(3000, this, [this, gen, installerPath, label, onSuccess]() {
                        if (gen != m_opGen || !m_installing) return;
                        runInstaller(installerPath, label, onSuccess);
                    });
                    return;
                }

                failJob(QStringLiteral("Ошибка установки %1").arg(label),
                        QStringLiteral("Установщик завершился с кодом %1. Чаще всего это проблема с сетью — нажмите «Повторить».").arg(code),
                        ctx->tail.join('\n'), true);
            });

    reportProgress(QStringLiteral("Установка %1").arg(label), QStringLiteral("Запуск установщика..."), -1.0, true);
    proc->start(javaPath, QStringList() << QStringLiteral("-jar") << installerPath
                                        << QStringLiteral("--installClient") << m_jobPath);
}

void MinecraftHandler::discardArchive()
{
    if (!m_zipPath.isEmpty()) QFile::remove(m_zipPath);
    for (const QString& p : m_partPaths) {
        RangedDownload::removeState(p);
        QFile::remove(p);
    }
}

void MinecraftHandler::stageExtract()
{
    if (m_job.archiveLink.trimmed().isEmpty()) { finishInstall(); return; }
    startExtract(m_zipPath, m_jobPath);
}

void MinecraftHandler::startExtract(const QString& zipPath, const QString& targetDir)
{
    QFile probe(zipPath);
    if (!probe.open(QIODevice::ReadOnly)) {
        discardArchive();
        failJob(QStringLiteral("Ошибка распаковки"), QStringLiteral("Архив сборки не найден. Повторите загрузку."), zipPath, true);
        return;
    }
    const QByteArray magic = probe.read(4);
    probe.close();

    if (!magic.startsWith("PK")) {
        discardArchive();
        failJob(QStringLiteral("Архив повреждён"),
                QStringLiteral("Скачанный файл не является ZIP-архивом. Нажмите «Повторить» — он будет скачан заново."),
                QStringLiteral("size: %1 байт").arg(QFileInfo(zipPath).size()), true);
        return;
    }

    auto st = std::make_shared<ExtractState>();
    st->zip.reset(new QZipReader(zipPath));
    if (!st->zip->isReadable()) {
        st->zip.reset();
        discardArchive();
        failJob(QStringLiteral("Архив повреждён"), QStringLiteral("Не удалось прочитать архив. Нажмите «Повторить» — он будет скачан заново."), zipPath, true);
        return;
    }

    st->files = st->zip->fileInfoList();
    if (st->files.isEmpty()) {
        st->zip.reset();
        discardArchive();
        failJob(QStringLiteral("Архив пуст"), QStringLiteral("В архиве сборки нет файлов."), zipPath, true);
        return;
    }

    st->prefix = detectWrapperPrefix(st->files);
    st->targetDir = targetDir;
    st->targetClean = QDir::cleanPath(QDir(targetDir).absolutePath());
    st->gen = m_opGen;
    if (!st->prefix.isEmpty()) newDebug() << "[Zip] Stripping wrapper:" << st->prefix;

    m_extract = st;
    reportProgress(QStringLiteral("Распаковка сборки"), QStringLiteral("Подготовка..."), 0.0, true);
    QTimer::singleShot(0, this, &MinecraftHandler::extractStep);
}

void MinecraftHandler::extractStep()
{
    const std::shared_ptr<ExtractState> st = m_extract;
    if (!st || !m_installing || st->gen != m_opGen) return;

    const int total = st->files.size();
    QElapsedTimer t;
    t.start();

    while (st->index < total && t.elapsed() < 30) {
        const QZipReader::FileInfo& fi = st->files.at(st->index++);

        QString rel = normZipPath(fi.filePath);
        if (rel.isEmpty()) continue;
        if (!st->prefix.isEmpty()) {
            if (!rel.startsWith(st->prefix)) continue;
            rel = rel.mid(st->prefix.length());
        }
        if (rel.isEmpty()) continue;

        const QString outPath = QDir::cleanPath(QDir(st->targetDir).absoluteFilePath(rel));
        if (!outPath.startsWith(st->targetClean + QLatin1Char('/'))) {
            newDebug() << "[Zip] Skipping unsafe path:" << fi.filePath;
            continue;
        }

        if (fi.isDir) {
            if (!QDir().mkpath(outPath)) {
                st->failed++;
                if (st->firstError.isEmpty()) st->firstError = QStringLiteral("Не удалось создать папку ") + outPath;
            }
            continue;
        }

        QDir().mkpath(QFileInfo(outPath).absolutePath());
        const QByteArray data = st->zip->fileData(fi.filePath);
        if (data.isEmpty() && fi.size > 0) {
            st->failed++;
            if (st->firstError.isEmpty()) st->firstError = QStringLiteral("Не удалось прочитать из архива: ") + fi.filePath;
            continue;
        }

        QFile out(outPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(data) != data.size()) {
            st->failed++;
            if (st->firstError.isEmpty())
                st->firstError = QStringLiteral("Не удалось записать ") + outPath + QStringLiteral(" (") + out.errorString() + QLatin1Char(')');
            st->index = total;
            break;
        }
        out.close();
        st->written++;
    }

    reportProgress(QStringLiteral("Распаковка сборки"), QStringLiteral("%1 / %2").arg(st->index).arg(total),
                   total > 0 ? double(st->index) / double(total) : 1.0);

    if (st->index < total) {
        QTimer::singleShot(0, this, &MinecraftHandler::extractStep);
        return;
    }

    st->zip.reset();
    m_extract.reset();
    newDebug() << "[Zip] Done. written:" << st->written << "failed:" << st->failed;

    if (st->written == 0 || st->failed > 0) {
        discardArchive();
        failJob(QStringLiteral("Ошибка распаковки"),
                st->firstError.isEmpty() ? QStringLiteral("Не удалось распаковать сборку. Проверьте свободное место на диске.")
                                         : st->firstError,
                QString(), true);
        return;
    }
    finishInstall();
}

void MinecraftHandler::mainButtonClick()
{
    newDebug() << "DEBUG: Handling main button click";
    if (!sm || !q) return;

    if (m_minecraftProcess && m_minecraftProcess->state() != QProcess::NotRunning) {
        newDebug() << "[MC] Minecraft already running";
        return;
    }
    if (m_sel.name.isEmpty()) return;

    if (!isSafeBuildName(m_sel.name)) {
        emit q->installError(QStringLiteral("Некорректное имя сборки"),
                             QStringLiteral("Имя «%1» нельзя использовать как имя папки.").arg(m_sel.name), QString(), false);
        return;
    }

    if (m_installing) {
        if (m_job.name != m_sel.name)
            emit q->installError(QStringLiteral("Идёт установка"),
                                 QStringLiteral("Сейчас устанавливается «%1». Дождитесь завершения или отмените её.").arg(m_job.name),
                                 QString(), false);
        return;
    }

    refreshSelectedState();

    if (!m_buildExists || m_updateAvailable) {
        beginInstall(m_sel, m_buildExists && m_updateAvailable);
        return;
    }

    const QString gamePath = gamePathFor(m_sel.name);
    const QStringList command = parseVersionJson(gamePath);
    if (command.isEmpty()) return;

    newDebug() << "DEBUG: Starting minecraft on" << m_sel.loader << m_sel.loaderVersion;
    launchMinecraft(command, gamePath);
}

QByteArray MinecraftHandler::loadCache(const QString& fileName) const
{
    QFile f(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QLatin1Char('/') + fileName);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
    return f.readAll();
}

void MinecraftHandler::saveCache(const QString& fileName, const QByteArray& data) const
{
    writeFileAtomic(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QLatin1Char('/') + fileName, data);
}

void MinecraftHandler::scheduleFetchRetry()
{
    if (m_fetchRetryTimer && !m_fetchRetryTimer->isActive())
        m_fetchRetryTimer->start();
}

void MinecraftHandler::fetchBuildsList()
{
    if (m_fetchingBuilds) return;
    m_fetchingBuilds = true;

    fetchText(QString::fromLatin1(kBuildsUrl), 3, false, [this](bool ok, const QByteArray& data, const QString& err) {
        m_fetchingBuilds = false;

        const QJsonDocument doc = ok ? QJsonDocument::fromJson(data) : QJsonDocument();
        if (ok && doc.isArray()) {
            saveCache(QStringLiteral("builds_cache.json"), data);
            setOffline(false);
            emit buildsListReady(doc.array());
            return;
        }

        setOffline(true);
        scheduleFetchRetry();

        const QJsonDocument cached = QJsonDocument::fromJson(loadCache(QStringLiteral("builds_cache.json")));
        if (cached.isArray()) {
            newDebug() << "[FetchBuilds] offline, using cache";
            emit buildsListReady(cached.array());
            return;
        }

        if (!q) return;
        m_failedJobName.clear();
        m_retryFn = [this]() { fetchBuildsList(); };
        emit q->installError(QStringLiteral("Не удалось получить список сборок"),
                             ok ? QStringLiteral("Сервер вернул данные в неожиданном формате.")
                                : QStringLiteral("Проверьте подключение к интернету. Лаунчер сам повторит попытку."),
                             err, true);
    });
}

void MinecraftHandler::fetchNews()
{
    if (m_fetchingNews) return;
    m_fetchingNews = true;

    fetchText(QString::fromLatin1(kNewsUrl), 3, false, [this](bool ok, const QByteArray& data, const QString&) {
        m_fetchingNews = false;

        QJsonDocument doc = ok ? QJsonDocument::fromJson(data) : QJsonDocument();
        if (ok && doc.isArray()) {
            saveCache(QStringLiteral("news_cache.json"), data);
        } else {
            doc = QJsonDocument::fromJson(loadCache(QStringLiteral("news_cache.json")));
            if (!doc.isArray()) return;
        }

        const QJsonArray arr = doc.array();
        QJsonArray limited;
        for (int i = 0; i < arr.size() && i < 8; ++i) limited.append(arr.at(i));
        emit newsReady(limited);
    });
}

QStringList MinecraftHandler::parseVersionJson(const QString& gamePath)
{
    const QString versionId = getVersionId();
    const QString jsonPath = versionJsonFile(gamePath, versionId);

    const QJsonObject json = readJsonObject(jsonPath);
    if (json.isEmpty()) {
        newDebug() << "[MC] ERROR: cannot read" << jsonPath;
        emit q->installError(QStringLiteral("Ошибка"), QStringLiteral("Файл версии не найден или повреждён. Переустановите сборку."), jsonPath, false);
        return {};
    }

    QList<QJsonObject> chain;
    chain << json;
    int requiredJava = json.value(QStringLiteral("javaVersion")).toObject().value(QStringLiteral("majorVersion")).toInt(0);
    {
        QJsonObject cur = json;
        int guard = 0;
        while (cur.contains(QStringLiteral("inheritsFrom")) && guard++ < 8) {
            const QString parent = cur.value(QStringLiteral("inheritsFrom")).toString();
            const QJsonObject pj = readJsonObject(versionJsonFile(gamePath, parent));
            if (pj.isEmpty()) { newDebug() << "[MC] ERROR: cannot open parent json" << parent; break; }
            requiredJava = qMax(requiredJava, pj.value(QStringLiteral("javaVersion")).toObject().value(QStringLiteral("majorVersion")).toInt(0));
            chain.prepend(pj);
            cur = pj;
        }
    }

    if (requiredJava > 0) {
        QString suited;
        const QString userJava = sm->javaPath();
        if (!userJava.isEmpty() && sm->getJavaMajorVersion(userJava) == requiredJava)
            suited = userJava;
        else
            suited = sm->findJavaByMajor(requiredJava);

        const int suitedMajor = suited.isEmpty() ? -1 : sm->getJavaMajorVersion(suited);
        if (suited.isEmpty() || suitedMajor < requiredJava) {
            emit q->installError(QStringLiteral("Несовместимая версия Java"),
                                 QStringLiteral("Для Minecraft %1 требуется Java %2, %3.\nУстановите JDK %2 и повторите.")
                                     .arg(m_sel.mcVersion).arg(requiredJava)
                                     .arg(suited.isEmpty() ? QStringLiteral("но в системе её нет")
                                                           : QStringLiteral("а найдена только Java %1").arg(suitedMajor)),
                                 QString(), false);
            return {};
        }
        m_launchJavaPath = suited;
        newDebug() << "[MC] Required Java:" << requiredJava << "using Java" << suitedMajor << "at" << suited;
    } else {
        m_launchJavaPath = sm->javaPath();
    }
    int javaMajor = sm->getJavaMajorVersion(m_launchJavaPath);
    if (javaMajor <= 0) javaMajor = 21;

    extractNatives(chain, gamePath);

    const QString librariesDir = gamePath + QStringLiteral("/libraries");
    QStringList classpathList;
    QSet<QString> seen;
    auto addCp = [&](const QString& p) {
        const QString nat = QDir::toNativeSeparators(QDir::cleanPath(p));
        if (seen.contains(nat)) return;
        seen.insert(nat);
        classpathList << nat;
    };

    for (const QJsonObject& j : chain) {
        for (const QJsonValue& v : j.value(QStringLiteral("libraries")).toArray()) {
            const QJsonObject lib = v.toObject();
            if (!rulesAllow(lib.value(QStringLiteral("rules")).toArray())) continue;

            QString rel;
            const QJsonObject art = lib.value(QStringLiteral("downloads")).toObject().value(QStringLiteral("artifact")).toObject();
            if (!art.isEmpty()) rel = art.value(QStringLiteral("path")).toString();
            else if (lib.contains(QStringLiteral("name")) && !lib.contains(QStringLiteral("natives"))
                     && (lib.contains(QStringLiteral("url")) || !lib.contains(QStringLiteral("downloads"))))
                rel = mavenPathFromName(lib.value(QStringLiteral("name")).toString());
            if (rel.isEmpty()) continue;
            addCp(QDir(librariesDir).absoluteFilePath(rel));
        }
    }

    const bool usesBSL = (m_sel.loader == QLatin1String("forge") || m_sel.loader == QLatin1String("neoforge"));

    if (m_sel.loader == QLatin1String("vanilla") || m_sel.loader == QLatin1String("fabric")) {
        const QString baseVersion = json.contains(QStringLiteral("inheritsFrom"))
        ? json.value(QStringLiteral("inheritsFrom")).toString() : m_sel.mcVersion;
        const QString mcJar = gamePath + QStringLiteral("/versions/") + baseVersion + QLatin1Char('/') + baseVersion + QStringLiteral(".jar");
        if (QFile::exists(mcJar)) addCp(mcJar);
        else newDebug() << "[MC] WARNING: vanilla jar missing:" << mcJar;
    }

    const QString classPath = classpathList.join(';');

    if (usesBSL) {
        QFile cpFile(gamePath + QStringLiteral("/legacyClassPath.txt"));
        if (cpFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            for (const QString& entry : classpathList) { cpFile.write(entry.toUtf8()); cpFile.write("\n"); }
        }
    }

    QString assetIndexId = m_sel.mcVersion;
    for (int i = chain.size() - 1; i >= 0; --i) {
        const QString id = chain.at(i).value(QStringLiteral("assetIndex")).toObject().value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) { assetIndexId = id; break; }
    }
    {
        const QString idxDir = gamePath + QStringLiteral("/assets/indexes/");
        if (!QFile::exists(idxDir + assetIndexId + QStringLiteral(".json")) &&
            QFile::exists(idxDir + m_sel.mcVersion + QStringLiteral(".json")))
            QFile::copy(idxDir + m_sel.mcVersion + QStringLiteral(".json"), idxDir + assetIndexId + QStringLiteral(".json"));
    }

    QHash<QString, QString> vars;
    vars[QStringLiteral("${auth_player_name}")] = sm->username();
    vars[QStringLiteral("${auth_uuid}")] = QStringLiteral("00000000-0000-0000-0000-000000000000");
    vars[QStringLiteral("${auth_access_token}")] = QStringLiteral("0");
    vars[QStringLiteral("${auth_session}")] = QStringLiteral("0");
    vars[QStringLiteral("${user_type}")] = QStringLiteral("legacy");
    vars[QStringLiteral("${version_name}")] = versionId;
    vars[QStringLiteral("${version_type}")] = QStringLiteral("release");
    vars[QStringLiteral("${game_directory}")] = gamePath;
    vars[QStringLiteral("${assets_root}")] = gamePath + QStringLiteral("/assets");
    vars[QStringLiteral("${assets_index_name}")] = assetIndexId;
    vars[QStringLiteral("${natives_directory}")] = gamePath + QStringLiteral("/natives");
    vars[QStringLiteral("${library_directory}")] = librariesDir;
    vars[QStringLiteral("${classpath}")] = classPath;
    vars[QStringLiteral("${launcher_name}")] = QStringLiteral("SeaLauncher");
    vars[QStringLiteral("${launcher_version}")] = QStringLiteral("1.0");
    vars[QStringLiteral("${resolution_width}")] = QStringLiteral("925");
    vars[QStringLiteral("${resolution_height}")] = QStringLiteral("530");
    vars[QStringLiteral("${game_assets}")] = gamePath + QStringLiteral("/assets");
    vars[QStringLiteral("${user_properties}")] = QStringLiteral("{}");
    vars[QStringLiteral("${clientid}")] = QStringLiteral("0");
    vars[QStringLiteral("${auth_xuid}")] = QStringLiteral("0");
    vars[QStringLiteral("${classpath_separator}")] = QStringLiteral(";");
    vars[QStringLiteral("${path_separator}")] = QStringLiteral(";");
    if (usesBSL) {
        vars[QStringLiteral("${fml.neoForgeVersion}")] = m_sel.loaderVersion;
        vars[QStringLiteral("${fml.mcVersion}")] = m_sel.mcVersion;
        vars[QStringLiteral("${fml.fmlVersion}")] = QString();
    }
    if (m_sel.loader == QLatin1String("fabric"))
        vars[QStringLiteral("${fabric.gameJarPath}")] = gamePath + QStringLiteral("/versions/") + m_sel.mcVersion + QLatin1Char('/') + m_sel.mcVersion + QStringLiteral(".jar");

    auto substitute = [&vars](QString s) -> QString {
        for (auto it = vars.constBegin(); it != vars.constEnd(); ++it) s.replace(it.key(), it.value());
        return s;
    };
    auto expandArray = [&substitute](const QJsonArray& arr) -> QStringList {
        QStringList out;
        for (const QJsonValue& v : arr) {
            if (v.isString()) { out << substitute(v.toString()); continue; }
            if (!v.isObject()) continue;
            const QJsonObject obj = v.toObject();
            if (!rulesAllow(obj.value(QStringLiteral("rules")).toArray())) continue;
            const QJsonValue val = obj.value(QStringLiteral("value"));
            if (val.isString()) out << substitute(val.toString());
            else if (val.isArray())
                for (const QJsonValue& sub : val.toArray())
                    if (sub.isString()) out << substitute(sub.toString());
        }
        return out;
    };

    QStringList jvmArgs;
    jvmArgs << QStringLiteral("-Xmx") + QString::number(sm->ramMb()) + QLatin1Char('M');
    jvmArgs << QStringLiteral("-Djava.library.path=") + gamePath + QStringLiteral("/natives");
    if (usesBSL) jvmArgs << QStringLiteral("-DlegacyClassPath=") + classPath;
    else jvmArgs << QStringLiteral("-cp") << classPath;
    if (!sm->jvmArgs().trimmed().isEmpty()) jvmArgs << sm->jvmArgs().split(' ', Qt::SkipEmptyParts);

    for (const QJsonObject& jObj : chain) {
        const QJsonObject args = jObj.value(QStringLiteral("arguments")).toObject();
        if (!args.contains(QStringLiteral("jvm"))) continue;
        for (const QString& arg : expandArray(args.value(QStringLiteral("jvm")).toArray())) {
            if (!isJvmArgSupportedFor(arg, javaMajor)) { newDebug() << "[MC] Skipping JVM arg (Java" << javaMajor << "):" << arg; continue; }
            jvmArgs << arg;
        }
    }

    QStringList gameArgs;
    bool foundGameArgs = false;
    for (const QJsonObject& jObj : chain) {
        if (jObj.contains(QStringLiteral("minecraftArguments"))) {
            gameArgs << substitute(jObj.value(QStringLiteral("minecraftArguments")).toString()).split(' ', Qt::SkipEmptyParts);
            foundGameArgs = true;
            continue;
        }
        const QJsonObject args = jObj.value(QStringLiteral("arguments")).toObject();
        if (args.contains(QStringLiteral("game"))) {
            gameArgs << expandArray(args.value(QStringLiteral("game")).toArray());
            foundGameArgs = true;
        }
    }
    if (!foundGameArgs) {
        gameArgs << QStringLiteral("--username") << sm->username() << QStringLiteral("--version") << versionId
                 << QStringLiteral("--gameDir") << gamePath << QStringLiteral("--assetsDir") << gamePath + QStringLiteral("/assets")
                 << QStringLiteral("--assetIndex") << assetIndexId << QStringLiteral("--uuid") << QStringLiteral("00000000-0000-0000-0000-000000000000")
                 << QStringLiteral("--accessToken") << QStringLiteral("0") << QStringLiteral("--userType") << QStringLiteral("legacy");
    }

    QStringList filtered;
    for (int i = 0; i < gameArgs.size(); ++i) {
        const QString& a = gameArgs.at(i);
        if (a == QLatin1String("--demo")) continue;
        if (a.startsWith(QLatin1String("--quickPlay"))) {
            if (i + 1 < gameArgs.size() && !gameArgs.at(i + 1).startsWith(QLatin1String("--"))) ++i;
            continue;
        }
        filtered << a;
    }
    gameArgs = filtered;

    QString defaultMain = QStringLiteral("net.minecraft.client.main.Main");
    if (usesBSL) defaultMain = QStringLiteral("cpw.mods.bootstraplauncher.BootstrapLauncher");
    else if (m_sel.loader == QLatin1String("fabric")) defaultMain = QStringLiteral("net.fabricmc.loader.impl.launch.knot.KnotClient");
    const QString mainClass = json.value(QStringLiteral("mainClass")).toString(defaultMain);

    QStringList fullCommand;
    fullCommand << jvmArgs << mainClass << gameArgs;

    newDebug() << "[MC] loader:" << m_sel.loader << "versionId:" << versionId
               << "classpath:" << classpathList.size() << "mainClass:" << mainClass;
    return fullCommand;
}

bool MinecraftHandler::isJvmArgSupportedFor(const QString& arg, int javaMajor)
{
    struct Rule { const char* prefix; int minJava; };
    static const Rule rules[] = {
                                  { "--sun-misc-unsafe-memory-access", 23 },
                                  { "--enable-preview", 11 },
                                  { "-XX:+UseShenandoahGC", 12 },
                                  { "-XX:+UseZGC", 11 },
                                  { "-XX:+ZGenerational", 21 },
                                  { "-XX:+UseCompactObjectHeaders", 24 },
                                  { "--add-opens", 9 }, { "--add-exports", 9 }, { "--add-modules", 9 },
                                  { "--module-path", 9 }, { "-p", 9 },
                                  };
    for (const auto& r : rules)
        if (arg.startsWith(QLatin1String(r.prefix)) && javaMajor < r.minJava) return false;
    return true;
}

bool MinecraftHandler::launchMinecraft(const QStringList& command, const QString& gamePath)
{
    if (m_minecraftProcess && m_minecraftProcess->state() != QProcess::NotRunning) {
        newDebug() << "[MC] Minecraft already running, refusing to start another";
        return false;
    }
    if (command.isEmpty()) {
        emit q->installError(QStringLiteral("Ошибка"), QStringLiteral("Команда запуска пуста"), QString(), false);
        return false;
    }

    const QString javaPath = QDir::toNativeSeparators((m_launchJavaPath.isEmpty() ? sm->javaPath() : m_launchJavaPath).trimmed());
    if (javaPath.isEmpty() || !QFileInfo::exists(javaPath)) {
        emit q->installError(QStringLiteral("Java не найдена"),
                             QStringLiteral("Укажите путь к Java в настройках лаунчера."), javaPath, false);
        return false;
    }

    QByteArray buf;
    for (const QString& arg : command) {
        QString e = arg;
        e.replace('\\', QStringLiteral("\\\\"));
        e.replace('"', QStringLiteral("\\\""));
        buf += '"' + e.toUtf8() + "\"\n";
    }
    const QString argsFilePath = gamePath + QStringLiteral("/launch_args.txt");
    if (!writeFileAtomic(argsFilePath, buf)) {
        emit q->installError(QStringLiteral("Ошибка"), QStringLiteral("Не удалось записать файл аргументов запуска."), argsFilePath, false);
        return false;
    }

    auto* proc = new QProcess(this);
    m_minecraftProcess = proc;
    proc->setWorkingDirectory(gamePath);
    proc->setProcessChannelMode(QProcess::MergedChannels);

    auto tail = std::make_shared<QStringList>();
    auto lineBuf = std::make_shared<QString>();

    connect(proc, &QProcess::readyReadStandardOutput, this, [proc, tail, lineBuf]() {
        lineBuf->append(QString::fromLocal8Bit(proc->readAllStandardOutput()));
        int nl;
        while ((nl = lineBuf->indexOf('\n')) >= 0) {
            const QString line = lineBuf->left(nl).trimmed();
            lineBuf->remove(0, nl + 1);
            if (line.isEmpty()) continue;
            *tail << line;
            while (tail->size() > 60) tail->removeFirst();
            newDebug() << "[Minecraft]" << line;
        }
        if (lineBuf->size() > 100000) lineBuf->clear();
    });

    connect(proc, &QProcess::started, this, [this]() { emit minecraftStarted(); });

    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        const QString es = proc->errorString();
        if (m_minecraftProcess == proc) m_minecraftProcess = nullptr;
        proc->deleteLater();
        emit q->installError(QStringLiteral("Minecraft не запустился"), es, QString(), false);
    });

    connect(proc, &QProcess::finished, this, [this, proc, tail](int exitCode, QProcess::ExitStatus status) {
        const bool bad = (status != QProcess::NormalExit || exitCode != 0);
        newDebug() << "[Minecraft] finished, exitCode:" << exitCode;
        if (m_minecraftProcess == proc) m_minecraftProcess = nullptr;
        proc->deleteLater();

        emit minecraftStopped();

        if (bad && q)
            emit q->installError(QStringLiteral("Minecraft завершился с ошибкой"),
                                 QStringLiteral("Код выхода: %1. Последние строки журнала приведены в «Подробностях».").arg(exitCode),
                                 tail->join('\n'), false);
    });

    newDebug() << "[MC] Starting:" << javaPath << "argfile:" << argsFilePath << "cwd:" << gamePath;
    proc->start(javaPath, QStringList() << QStringLiteral("@") + QDir::toNativeSeparators(argsFilePath));
    return true;
}