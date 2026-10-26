#include "SettingsManager.h"
#include "NewDebug.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QVariantMap>
#include <algorithm>

int SettingsManager::majorFromReleaseFile(const QString& javaExePath)
{
    QDir home(QFileInfo(javaExePath).absolutePath());
    if (!home.cdUp()) return -1;

    QFile f(home.absoluteFilePath(QStringLiteral("release")));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return -1;

    static const QRegularExpression re(QStringLiteral(R"(JAVA_VERSION\s*=\s*"?(\d+)(?:\.(\d+))?)"));
    const QRegularExpressionMatch m = re.match(QString::fromUtf8(f.readAll()));
    if (!m.hasMatch()) return -1;

    int major = m.captured(1).toInt();
    if (major == 1 && !m.captured(2).isEmpty()) major = m.captured(2).toInt();
    return major > 0 ? major : -1;
}

int SettingsManager::getJavaMajorVersion(const QString& path) const
{
    if (path.isEmpty()) return -1;
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) return -1;

    const QString key = fi.absoluteFilePath();
    const auto cached = m_majorCache.constFind(key);
    if (cached != m_majorCache.constEnd()) return cached.value();

    int major = majorFromReleaseFile(path);

    if (major <= 0) {
        QProcess p;
        p.start(path, {"-version"});
        if (!p.waitForFinished(3000)) { p.kill(); return -1; }

        const QString out = QString::fromLocal8Bit(p.readAllStandardError())
                            + QString::fromLocal8Bit(p.readAllStandardOutput());
        static const QRegularExpression re(R"(version\s+"?(\d+)(?:\.(\d+))?[\._\s"])",
                                           QRegularExpression::CaseInsensitiveOption);
        const auto m = re.match(out);
        if (!m.hasMatch()) return -1;

        major = m.captured(1).toInt();
        if (major == 1 && !m.captured(2).isEmpty()) major = m.captured(2).toInt();
    }

    if (major > 0) m_majorCache.insert(key, major);
    return major;
}

void SettingsManager::collectJavaExecutables(const QString& root, int depth, QStringList& out)
{
    const QDir dir(root);
    if (!dir.exists()) return;

    const QString exe = dir.absoluteFilePath(QStringLiteral("bin/java.exe"));
    if (QFileInfo::exists(exe)) { out << exe; return; }

    if (depth <= 0) return;
    const QStringList subs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& sub : subs)
        collectJavaExecutables(dir.absoluteFilePath(sub), depth - 1, out);
}

QList<QPair<QString, int>> SettingsManager::getAllJavaInstallations() const
{
    if (m_javaCacheValid) return m_javaCache;

    m_javaCache.clear();
    QSet<QString> seen;

    auto tryAdd = [&](const QString& path) {
        if (path.isEmpty()) return;
        const QFileInfo fi(path);
        if (!fi.exists() || !fi.isFile()) return;

        const QString canonical = fi.canonicalFilePath();
        if (seen.contains(canonical)) return;
        seen.insert(canonical);

        const int major = getJavaMajorVersion(path);
        if (major > 0) m_javaCache << qMakePair(fi.absoluteFilePath(), major);
    };

    for (const QString& var : {"JAVA_HOME", "JDK_HOME", "JRE_HOME"}) {
        const QString base = qEnvironmentVariable(var.toUtf8().constData());
        if (!base.isEmpty()) tryAdd(base + "/bin/java.exe");
    }

    const QString fromPath = QStandardPaths::findExecutable("java");
    if (!fromPath.isEmpty() && !fromPath.contains("javapath", Qt::CaseInsensitive))
        tryAdd(fromPath);

    QStringList roots = {
        "C:/Program Files/Java", "C:/Program Files/Eclipse Adoptium",
        "C:/Program Files/Microsoft", "C:/Program Files/BellSoft",
        "C:/Program Files/Amazon Corretto", "C:/Program Files/Zulu",
        "C:/Program Files/Oracle", "C:/Program Files/IBM",
        "C:/Program Files/Semeru", "C:/Program Files/Temurin",
        "C:/Program Files (x86)/Java", "C:/Program Files (x86)/Eclipse Adoptium",
        "C:/Program Files (x86)/Minecraft Launcher/runtime",
        "C:/Program Files/Minecraft Launcher/runtime"
    };
    const QString userProfile = qEnvironmentVariable("USERPROFILE");
    if (!userProfile.isEmpty())
        roots << userProfile + "/.jdks" << userProfile + "/scoop/apps";
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty())
        roots << localAppData + "/Programs/Eclipse Adoptium" << localAppData + "/Programs/Zulu";
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty())
        roots << appData + "/.minecraft/runtime";

    for (const QString& root : roots) {
        QStringList found;
        collectJavaExecutables(root, 3, found);
        for (const QString& exe : found) tryAdd(exe);
    }

    std::sort(m_javaCache.begin(), m_javaCache.end(),
              [](const QPair<QString,int>& a, const QPair<QString,int>& b) {
                  return a.second < b.second;
              });
    m_javaCacheValid = true;
    return m_javaCache;
}

QString SettingsManager::findJavaByMajor(int requiredMajor) const
{
    const auto all = getAllJavaInstallations();
    if (all.isEmpty()) return QString();

    for (const auto& j : all)
        if (j.second == requiredMajor) return j.first;
    for (const auto& j : all)
        if (j.second > requiredMajor) return j.first;

    return all.last().first;
}

void SettingsManager::invalidateJavaCache()
{
    m_javaCacheValid = false;
    m_majorCache.clear();
}

void SettingsManager::rescanJava()
{
    invalidateJavaCache();
    getAllJavaInstallations();
    emit javaListChanged();
}

int SettingsManager::javaMajorOf(const QString& path) const
{
    return getJavaMajorVersion(path);
}

QVariantList SettingsManager::detectedJavas() const
{
    QVariantList list;
    const auto all = getAllJavaInstallations();
    for (int i = all.size() - 1; i >= 0; --i) {
        QVariantMap m;
        m["path"] = all.at(i).first;
        m["major"] = all.at(i).second;
        list << m;
    }
    return list;
}

bool SettingsManager::isValidJavaExecutable(const QString& path)
{
    if (path.isEmpty()) return false;
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) return false;

    QProcess p;
    p.start(path, {"-version"});
    if (!p.waitForFinished(3000)) { p.kill(); return false; }
    const QString out = QString::fromLocal8Bit(p.readAllStandardError())
                        + QString::fromLocal8Bit(p.readAllStandardOutput());
    return out.contains("version", Qt::CaseInsensitive);
}

QString SettingsManager::detectJavaPath() const
{
    const auto all = getAllJavaInstallations();
    if (all.isEmpty()) {
        newDebug() << "[Java] No Java installations found";
        return QString();
    }

    newDebug() << "[Java] Found" << all.size() << "installation(s):";
    for (const auto& c : all)
        newDebug() << "  Java" << c.second << "→" << c.first;

    return all.last().first;
}

void SettingsManager::ensureLatestJava()
{
    const QString latest = detectJavaPath();
    if (latest.isEmpty()) {
        newDebug() << "[Java] No Java found, keeping current path:" << m_javaPath;
        return;
    }

    const int latestMajor = getJavaMajorVersion(latest);
    const int currentMajor = getJavaMajorVersion(m_javaPath);

    if (latestMajor > currentMajor || currentMajor < 0) {
        newDebug() << "[Java] Auto-upgrading to Java" << latestMajor << "at" << latest;
        m_javaPath = latest;
        emit javaPathChanged();
        scheduleSave();
    }
}

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &SettingsManager::save);

    load();
    ensureLatestJava();
}

SettingsManager::~SettingsManager()
{
    save();
}

QString SettingsManager::configFilePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/settings.json";
}

void SettingsManager::scheduleSave()
{
    if (m_loading) return;
    m_saveTimer.start();
}

void SettingsManager::writeTemplateFile()
{
    QJsonObject o;
    o["version"] = VERSION;
    o["username"] = "";
    o["ramMb"] = 4096;
    o["javaPath"] = "";
    o["gameDir"] = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/modpacks";
    o["jvmArgs"] = "";
    o["maxRam"] = 4096;

    QSaveFile f(configFilePath());
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "ERROR: Failed to open file to save settings";
        return;
    }
    f.write(QJsonDocument(o).toJson());
    f.commit();
}

void SettingsManager::load()
{
    m_loading = true;
    const QString path = configFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    auto fillDefaults = [this]() {
        if (m_javaPath.isEmpty()) m_javaPath = detectJavaPath();
        if (m_gameDir.isEmpty())
            m_gameDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/modpacks";
    };

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        writeTemplateFile();
        newDebug() << "Debug: No config file was found, using standard values";
        fillDefaults();
        m_loading = false;
        return;
    }

    const QByteArray raw = f.readAll();
    f.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        newDebug() << "[Settings] settings.json is corrupt:" << err.errorString() << "- backing up";
        QFile::remove(path + ".corrupt");
        QFile::copy(path, path + ".corrupt");
    }
    const QJsonObject o = doc.object();

    m_username = o.value("username").toString(m_username);
    m_ramMb    = o.value("ramMb").toInt(m_ramMb);
    m_javaPath = o.value("javaPath").toString(m_javaPath);
    m_gameDir  = o.value("gameDir").toString(m_gameDir);
    m_jvmArgs  = o.value("jvmArgs").toString(m_jvmArgs);
    m_version  = o.value("version").toInt(m_version);
    m_maxRam   = o.value("maxRam").toInt(m_maxRam);
    m_lastBuild = o.value("lastBuild").toString(m_lastBuild);

    if (m_ramMb < 2048) m_ramMb = 2048;
    if (m_maxRam >= 2048 && m_ramMb > m_maxRam) m_ramMb = m_maxRam;

    newDebug() << "Debug: Settings loaded from: " << path;

    fillDefaults();
    m_loading = false;

    emit maxRamChanged();
    emit usernameChanged();
    emit ramMbChanged();
    emit javaPathChanged();
    emit gameDirChanged();
    emit jvmArgsChanged();
    emit versionChanged();
}

void SettingsManager::save()
{
    m_saveTimer.stop();
    if (m_loading) return;

    QJsonObject o;
    o["version"] = VERSION;
    o["username"] = m_username;
    o["ramMb"] = m_ramMb;
    o["javaPath"] = m_javaPath;
    o["gameDir"] = m_gameDir;
    o["jvmArgs"] = m_jvmArgs;
    o["maxRam"] = m_maxRam;
    o["lastBuild"] = m_lastBuild;

    QDir().mkpath(QFileInfo(configFilePath()).absolutePath());

    QSaveFile f(configFilePath());
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "ERROR: Failed to open file to save settings";
        return;
    }
    f.write(QJsonDocument(o).toJson());
    if (!f.commit()) qWarning() << "ERROR: Failed to commit settings file";
}

void SettingsManager::setLastBuild(const QString &v)
{
    if (m_lastBuild == v) return;
    m_lastBuild = v;
    emit lastBuildChanged();
    scheduleSave();
}

void SettingsManager::setUsername(const QString &v)
{
    if (m_username == v) return;
    m_username = v;
    emit usernameChanged();
    scheduleSave();
}

void SettingsManager::setVersion(int v)
{
    if (m_version == v) return;
    m_version = v;
    emit versionChanged();
    scheduleSave();
}

void SettingsManager::setRamMb(int v)
{
    if (m_ramMb == v) return;
    m_ramMb = v;
    emit ramMbChanged();
    scheduleSave();
}

void SettingsManager::setMaxRam(int v)
{
    if (m_maxRam == v) return;
    m_maxRam = v;
    emit maxRamChanged();
    if (v >= 2048 && m_ramMb > v) setRamMb(v);
    scheduleSave();
}

void SettingsManager::setJavaPath(const QString &v)
{
    if (m_javaPath == v) return;
    m_javaPath = v;
    emit javaPathChanged();
    scheduleSave();
}

void SettingsManager::setGameDir(const QString &v)
{
    if (m_gameDir == v) return;
    m_gameDir = v;
    emit gameDirChanged();
    scheduleSave();
}

void SettingsManager::setJvmArgs(const QString &v)
{
    if (m_jvmArgs == v) return;
    m_jvmArgs = v;
    emit jvmArgsChanged();
    scheduleSave();
}