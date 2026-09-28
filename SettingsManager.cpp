#include "SettingsManager.h"
#include "NewDebug.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

namespace fs = std::filesystem;

QList<QPair<QString, int>> SettingsManager::getAllJavaInstallations() const
{
    if (m_javaCacheValid) return m_javaCache;

    m_javaCache.clear();
    auto tryAdd = [&](const QString& path) {
        if (path.isEmpty()) return;
        QFileInfo fi(path);
        if (!fi.exists() || !fi.isFile()) return;

        const QString canonical = fi.canonicalFilePath();
        for (const auto& c : m_javaCache)
            if (QFileInfo(c.first).canonicalFilePath() == canonical) return;

        int major = getJavaMajorVersion(path);
        if (major > 0) m_javaCache << qMakePair(fi.absoluteFilePath(), major);
    };

    for (const QString& var : {"JAVA_HOME", "JDK_HOME", "JRE_HOME"}) {
        QString base = qEnvironmentVariable(var.toUtf8().constData());
        if (!base.isEmpty()) tryAdd(base + "/bin/java.exe");
    }

    QString fromPath = QStandardPaths::findExecutable("java");
    if (!fromPath.isEmpty() && !fromPath.contains("javapath", Qt::CaseInsensitive))
        tryAdd(fromPath);

    QStringList roots = {
        "C:/Program Files/Java", "C:/Program Files/Eclipse Adoptium",
        "C:/Program Files/Microsoft", "C:/Program Files/BellSoft",
        "C:/Program Files/Amazon Corretto", "C:/Program Files/Zulu",
        "C:/Program Files/Oracle", "C:/Program Files/IBM",
        "C:/Program Files (x86)/Java", "C:/Program Files (x86)/Eclipse Adoptium"
    };
    const QString userProfile = qEnvironmentVariable("USERPROFILE");
    if (!userProfile.isEmpty())
        roots << userProfile + "/.jdks" << userProfile + "/scoop/apps";
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty())
        roots << localAppData + "/Programs/Eclipse Adoptium"
              << localAppData + "/Programs/Zulu";

    for (const QString& root : roots) {
        if (!QDir(root).exists()) continue;
        QDirIterator it(root, {"java.exe"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const QString rel = QDir(root).relativeFilePath(path);
            if (rel.count('/') + rel.count('\\') > 5) continue;
            tryAdd(path);
        }
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
    if (all.isEmpty()) return "";

    for (const auto& j : all)
        if (j.second == requiredMajor)
            return j.first;

    for (const auto& j : all)
        if (j.second > requiredMajor)
            return j.first;

    return all.last().first;
}

void SettingsManager::invalidateJavaCache()
{
    m_javaCacheValid = false;
}

bool SettingsManager::isValidJavaExecutable(const QString& path)
{
    if (path.isEmpty()) return false;
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) return false;

    QProcess p;
    p.start(path, {"-version"});
    if (!p.waitForFinished(3000)) return false;
    QString out = QString::fromLocal8Bit(p.readAllStandardError())
                  + QString::fromLocal8Bit(p.readAllStandardOutput());
    return out.contains("version", Qt::CaseInsensitive);
}

QString SettingsManager::detectJavaPath() const
{
    // Раньше здесь был полный дубль сканирования из getAllJavaInstallations()
    // (те же самые директории, то же самое синхронное "java -version" на
    // каждый найденный java.exe), но БЕЗ кэша. Из-за этого при старте
    // приложения полное рекурсивное сканирование диска с блокирующим
    // запуском процессов выполнялось дважды подряд (load() -> getPaths(),
    // затем ensureLatestJava()), заметно замедляя запуск. Теперь переиспользуем
    // getAllJavaInstallations() — она кэширует результат (m_javaCacheValid),
    // так что повторный вызов почти бесплатен.
    const auto all = getAllJavaInstallations();
    if (all.isEmpty()) {
        newDebug() << "[Java] No Java installations found";
        return "";
    }

    // getAllJavaInstallations() сортирует по возрастанию major-версии,
    // поэтому последний элемент — самая новая найденная Java.
    newDebug() << "[Java] Found" << all.size() << "installation(s):";
    for (const auto& c : all)
        newDebug() << "  Java" << c.second << "→" << c.first;

    return all.last().first;
}

void SettingsManager::ensureLatestJava()
{
    newDebug() << "[Java] Scanning for latest Java...";

    const QString latest = detectJavaPath();
    if (latest.isEmpty()) {
        newDebug() << "[Java] No Java found, keeping current path:" << m_javaPath;
        return;
    }

    const int latestMajor = getJavaMajorVersion(latest);
    const int currentMajor = getJavaMajorVersion(m_javaPath);

    newDebug() << "[Java] Latest found: Java" << latestMajor << "at" << latest;
    newDebug() << "[Java] Currently set: Java" << currentMajor
               << "at" << (m_javaPath.isEmpty() ? "(empty)" : m_javaPath);

    if (latestMajor > currentMajor || currentMajor < 0) {
        newDebug() << "[Java] Auto-upgrading to Java" << latestMajor;
        m_javaPath = latest;
        save();
        emit javaPathChanged();
    } else {
        newDebug() << "[Java] Current Java is already the latest available";
    }
}

int SettingsManager::getJavaMajorVersion(const QString& path) const
{
    if (path.isEmpty()) return -1;
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) return -1;

    QProcess p;
    p.start(path, {"-version"});
    if (!p.waitForFinished(3000)) return -1;

    QString out = QString::fromLocal8Bit(p.readAllStandardError())
                  + QString::fromLocal8Bit(p.readAllStandardOutput());

    QRegularExpression re(R"(version\s+"?(\d+)(?:\.(\d+))?[\._\s"])",
                          QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(out);
    if (!m.hasMatch()) return -1;

    int major = m.captured(1).toInt();
    if (major == 1 && !m.captured(2).isEmpty())
        major = m.captured(2).toInt();
    return major;
}

void SettingsManager::CreateTemplateFile(){
    const QString path = configFilePath();
    const QString dir = QFileInfo(path).absolutePath();

    QDir().mkpath(dir);

    QJsonObject o;
    o["version"] = 1;
    o["username"] = "";
    o["ramMb"] = 4096;
    o["javaPath"] = "";
    o["gameDir"] = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/modpacks";
    o["jvmArgs"] = "";
    o["maxRam"] = 4096;
    o["buildsServerUrl"] = "";
    o["newsServerUrl"] = "";

    QFile f(path);

    if (!f.open(QIODevice::WriteOnly)){
        qWarning() << "ERROR: Failed to open file to save settings";
        return;
    }

    f.write(QJsonDocument(o).toJson());
}

SettingsManager::SettingsManager(QObject *parent)
    :QObject(parent)
{
    load();
    ensureLatestJava();
}

SettingsManager::~SettingsManager(){
    save();
}

QString SettingsManager::configFilePath() const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + "/settings.json";
}

void SettingsManager::load(){
    m_loading = true;
    const QString path = configFilePath();
    const QString dir = QFileInfo(path).absolutePath();

    auto getPaths = [this](){
        if (m_javaPath == "")
            m_javaPath = detectJavaPath();
        if (m_gameDir == "")
            m_gameDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/modpacks";
    };

    QDir().mkpath(dir);

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        CreateTemplateFile();
        f.open(QIODevice::ReadOnly);
        newDebug() << "Debug: No config file was found, using standard values";
        getPaths();
        m_loading = false;
        return;
    }

    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();

    m_username = o.value("username").toString(m_username);
    m_ramMb    = o.value("ramMb").toInt(m_ramMb);
    m_javaPath = o.value("javaPath").toString(m_javaPath);
    m_gameDir  = o.value("gameDir").toString(m_gameDir);
    m_jvmArgs  = o.value("jvmArgs").toString(m_jvmArgs);
    m_version = o.value("version").toInt(m_version);
    m_maxRam = o.value("maxRam").toInt(m_maxRam);

    if (m_ramMb < 2048) m_ramMb = 2048;

    newDebug() << "Debug: Settings loaded from: " << path;
    m_loading = false;

    getPaths();

    emit maxRamChanged();
    emit usernameChanged();
    emit ramMbChanged();
    emit javaPathChanged();
    emit gameDirChanged();
    emit jvmArgsChanged();
    emit versionChanged();
}

void SettingsManager::save(){
    if (m_loading)
        return;

    const QString path = configFilePath();
    const QString dir = QFileInfo(path).absolutePath();

    QDir().mkpath(dir);

    QJsonObject o;
    o["version"] = VERSION;
    o["username"] = m_username;
    o["ramMb"] = m_ramMb;
    o["javaPath"] = m_javaPath;
    o["gameDir"] = m_gameDir;
    o["jvmArgs"] = m_jvmArgs;
    o["maxRam"] = m_maxRam;

    QFile f(path);

    if (!f.open(QIODevice::WriteOnly)){
        qWarning() << "ERROR: Failed to open file to save settings";
        return;
    }

    f.write(QJsonDocument(o).toJson());
}

void SettingsManager::setUsername(const QString &v)
{
    if (m_username == v) return;
    m_username = v;
    emit usernameChanged();
    save();
}

void SettingsManager::setVersion(int v)
{
    if (m_version == v) return;
    m_version = v;
    emit versionChanged();
    save();
}

void SettingsManager::setRamMb(int v)
{
    if (m_ramMb == v) return;
    m_ramMb = v;
    emit ramMbChanged();
    save();
}

void SettingsManager::setMaxRam(int v)
{
    if (m_maxRam == v) return;
    m_maxRam = v;
    emit maxRamChanged();
    save();
}

void SettingsManager::setJavaPath(const QString &v)
{
    if (m_javaPath == v) return;
    m_javaPath = v;
    emit javaPathChanged();
    save();
}

void SettingsManager::setGameDir(const QString &v)
{
    if (m_gameDir == v) return;
    m_gameDir = v;
    emit gameDirChanged();
    save();
}

void SettingsManager::setJvmArgs(const QString &v)
{
    if (m_jvmArgs == v) return;
    m_jvmArgs = v;
    emit jvmArgsChanged();
    save();
}
