#include "minecrafthandler.h"
#include "NewDebug.h"
#include <algorithm>
#include <array>

MinecraftHandler::MinecraftHandler(QObject *parent) : QObject(parent), m_manager(new QNetworkAccessManager(this))
{
    m_progressThrottle = new QTimer(this);
    m_progressThrottle->setInterval(100);
    connect(m_progressThrottle, &QTimer::timeout, this, [this]() {
        if (!m_hasPendingUpdate) return;
        m_hasPendingUpdate = false;
        emit q->installProgress(m_pendingStage, m_pendingDetails, m_pendingProgress);
    });
    m_progressThrottle->start();
}

MinecraftHandler::~MinecraftHandler(){}

void MinecraftHandler::Initialize(QQuickView* v, QmlHandler* _q, SettingsManager* _sm, SettingsController* _sc){
    view = v; q = _q; sm = _sm; sc = _sc;
    if (sm) {
        connect(sm, &SettingsManager::javaPathChanged, this, [this]() {
            m_javaMajorVersion = -1;
        });
    }
}

void MinecraftHandler::ensureLauncherProfile(const QString& gameDir)
{
    QDir().mkpath(gameDir);
    const QString path = gameDir + "/launcher_profiles.json";
    if (QFileInfo::exists(path)) return;

    QJsonObject root;
    root["profiles"] = QJsonObject();
    root["selectedProfile"] = QString();
    root["clientToken"] = QString("8aa19a0b-733b-4153-8098-6214d930881c");
    root["authenticationDatabase"] = QJsonObject();
    root["launcherVersion"] = QJsonObject{{"name", "SeaLauncher"}, {"format", 21}, {"profilesFormat", 21}};

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { qWarning() << "[Installer] Не удалось создать launcher_profiles.json в" << path; return; }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    qDebug() << "[Installer] Создан launcher_profiles.json в" << path;
}

bool MinecraftHandler::isJvmArgSupported(const QString& arg, int javaMajor)
{
    struct Rule { const char* prefix; int minJava; int maxJava; };
    static const Rule rules[] = {
        { "--sun-misc-unsafe-memory-access", 23, -1 },
        { "--enable-preview", 11, -1 },
        { "-XX:+UseShenandoahGC", 12, -1 },
        { "-XX:+UseZGC", 11, -1 },
        { "-XX:+ZGenerational", 21, -1 },
        { "-XX:+UseCompactObjectHeaders", 24, -1 },
        { "-XX:+UseStringDeduplication", 8, -1 },
        { "--add-opens", 9, -1 }, { "--add-exports", 9, -1 }, { "--add-modules", 9, -1 },
        { "--module-path", 9, -1 }, { "-p", 9, -1 },
        };
    for (const auto& r : rules) {
        if (arg.startsWith(r.prefix)) {
            if (javaMajor < r.minJava) return false;
            if (r.maxJava > 0 && javaMajor > r.maxJava) return false;
        }
    }
    return true;
}

int MinecraftHandler::detectJavaMajorVersion()
{
    if (m_javaMajorVersion > 0) return m_javaMajorVersion;
    QProcess p;
    p.start(sm->javaPath(), {"-version"});
    if (!p.waitForFinished(3000)) { m_javaMajorVersion = 21; return m_javaMajorVersion; }
    QString out = QString::fromLocal8Bit(p.readAllStandardError()) + QString::fromLocal8Bit(p.readAllStandardOutput());
    QRegularExpression re(R"((?:version\s+")?(?:1\.)?(\d+)(?:[\._"])?)");
    auto m = re.match(out);
    m_javaMajorVersion = m.hasMatch() ? m.captured(1).toInt() : 21;
    newDebug() << "[Java] Detected major version:" << m_javaMajorVersion;
    return m_javaMajorVersion;
}

void MinecraftHandler::reCheckBuilds(const QString& build, const QString& version, const QString& loader, const QString& mcVer, const QString& git, const int parts, const QString& packVer){
    newDebug() << "Rechecking build folder: " << build << " for: Minecraft v" << mcVer << ", " << loader << ", pack v" << packVer;
    m_buildName = build; m_buildVersion = version; m_buildLoader = loader;
    m_mcVersion = mcVer; m_buildArchiveLink = git; m_archiveParts = parts;
    m_packVersion = packVer;

    QString gamePath = sm->gameDir() + "/" + build;
    QDir().mkpath(sm->gameDir());
    m_buildExists = QDir().exists(gamePath);

    if (m_buildExists) {
        checkInstallation(gamePath);
        m_installedPackVersion = readInstalledPackVersion(gamePath);
        setUpdateAvailable(m_installedPackVersion != m_packVersion);
        newDebug() << "[Check] installed pack:" << m_installedPackVersion
                   << "server pack:" << m_packVersion
                   << "update:" << m_updateAvailable;
    } else {
        m_installedPackVersion.clear();
        setUpdateAvailable(false);
    }

    newDebug() << "Does build exist? " << m_buildExists;
    emit buildExistsChanged();
}

QString MinecraftHandler::readInstalledPackVersion(const QString& gamePath) const
{
    QFile f(gamePath + "/sealauncher_meta.json");
    if (!f.open(QIODevice::ReadOnly)) return "0.1";

    QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    QString ver = o.value("packVersion").toString();

    return ver.isEmpty() ? "0.1" : ver;
}

void MinecraftHandler::writeBuildMeta(const QString& gamePath)
{
    QDir().mkpath(gamePath);
    QJsonObject o;
    o["packVersion"] = m_packVersion;
    o["loaderVersion"] = m_buildVersion;
    o["loader"] = m_buildLoader;
    o["mcVersion"] = m_mcVersion;
    o["git"] = m_buildArchiveLink;
    o["parts"] = m_archiveParts;
    o["installedAt"] = QDateTime::currentSecsSinceEpoch();

    QFile f(gamePath + "/sealauncher_meta.json");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        newDebug() << "[Meta] Cannot write:" << f.fileName();
        return;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    f.close();
    newDebug() << "[Meta] Written pack version:" << m_packVersion;
}

void MinecraftHandler::prepareForUpdate(const QString& gamePath)
{
    static const QStringList keep = {
        "saves",
        "options.txt",
        "screenshots",
        "resourcepacks",
        "shaderpacks",
        "sealauncher_meta.json",
        "assets"
    };

    QDir dir(gamePath);

    const QFileInfoList entries =
        dir.entryInfoList(
            QDir::AllEntries |
            QDir::NoDotAndDotDot
            );

    for (const QFileInfo& fileInfo : entries) {
        if (keep.contains(
                fileInfo.fileName(),
                Qt::CaseInsensitive)) {
            continue;
        }

        if (fileInfo.isDir()) {
            QDir target(fileInfo.absoluteFilePath());

            if (!target.removeRecursively()) {
                newDebug()
                << "[Update] Failed to remove:"
                << fileInfo.absoluteFilePath();
            }
        } else {
            if (!QFile::remove(fileInfo.absoluteFilePath())) {
                newDebug()
                << "[Update] Failed to remove:"
                << fileInfo.absoluteFilePath();
            }
        }
    }
}

bool MinecraftHandler::loaderAlreadyInstalled(const QString& gamePath) const
{
    QFile f(gamePath + "/sealauncher_meta.json");

    if (!f.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError error;
    const QJsonDocument document =
        QJsonDocument::fromJson(f.readAll(), &error);

    if (error.error != QJsonParseError::NoError ||
        !document.isObject()) {
        return false;
    }

    const QJsonObject object = document.object();

    if (object.value("loader").toString() != m_buildLoader)
        return false;

    if (object.value("loaderVersion").toString() != m_buildVersion)
        return false;

    if (object.value("mcVersion").toString() != m_mcVersion)
        return false;

    const QString versionId = getVersionId();

    const QString versionDir =
        gamePath + "/versions/" + versionId;

    const QString versionJson =
        versionDir + "/" + versionId + ".json";

    return QFileInfo::exists(versionJson);
}

void MinecraftHandler::mainButtonClick(){
    newDebug() << "DEBUG: Handling main button click";

    if (m_minecraftProcess && m_minecraftProcess->state() != QProcess::NotRunning) {
        newDebug() << "[MC] Minecraft already running";
        return;
    }

    QString gamePath = sm->gameDir() + "/" + m_buildName;
    const bool needInstall = !m_buildExists || m_updateAvailable;

    if (m_updateAvailable) {
        newDebug() << "[Update] Updating" << m_buildName
                   << "pack from" << m_installedPackVersion << "to" << m_packVersion;
        prepareForUpdate(gamePath);
    }

    if (needInstall){
        newDebug() << "DEBUG: Downloading version for " << m_buildVersion;
        connect(this, &MinecraftHandler::finished, this, [this, gamePath](){
            ensureNarratorDisabled(gamePath);
            writeBuildMeta(gamePath);
            sc->closeProgressPanel(view);
        }, Qt::SingleShotConnection);

        const bool skipLoader = m_updateAvailable
                                && !m_buildArchiveLink.isEmpty()
                                && loaderAlreadyInstalled(gamePath);

        if (skipLoader) {
            newDebug() << "[Update] Loader unchanged, downloading modpack only";
            finalizeInstall(gamePath);
        }
        else if (m_buildLoader == "neoforge") downloadNeoforge(m_buildVersion);
        else if (m_buildLoader == "vanilla") installVanilla(m_mcVersion);
        else if (m_buildLoader == "fabric") installFabric(m_mcVersion, m_buildVersion);
        else if (m_buildLoader == "forge") installForge(m_mcVersion, m_buildVersion);
    } else {
        QStringList command = parseVersionJson(gamePath);
        ensureNarratorDisabled(gamePath);
        newDebug() << "DEBUG: Starting minecraft on " << m_buildLoader << " " << m_buildVersion;
        launchMinecraft(command, gamePath);
    }
}

void MinecraftHandler::finalizeInstall(const QString& gamePath)
{
    if (m_buildArchiveLink.trimmed().isEmpty()) {
        newDebug() << "[Install] No modpack archive, finalizing local build";
        setUpdateAvailable(false);
        setBuildExists(true);
        emit installFinished();
        emit finished();
        return;
    }
    installModpack(m_buildArchiveLink, gamePath, m_archiveParts);
}

void MinecraftHandler::installLocalBuild(const QString& buildName, const QString& mcVersion, const QString& loader, const QString& loaderVersion)
{
    m_packVersion = QStringLiteral("0.1");
    m_buildName = buildName;
    m_mcVersion = mcVersion;
    m_buildLoader = loader;
    m_buildVersion = loaderVersion;
    m_buildArchiveLink.clear();
    m_archiveParts = 1;

    QString gamePath = sm->gameDir() + "/" + buildName;
    QDir().mkpath(gamePath);

    connect(this, &MinecraftHandler::finished, this, [this, gamePath](){
        ensureNarratorDisabled(gamePath);
        writeBuildMeta(gamePath);
        sc->closeProgressPanel(view);
    }, Qt::SingleShotConnection);

    if (loader == "vanilla") installVanilla(mcVersion);
    else if (loader == "fabric") installFabric(mcVersion, loaderVersion);
    else if (loader == "forge") installForge(mcVersion, loaderVersion);
    else if (loader == "neoforge") downloadNeoforge(loaderVersion);
}

void MinecraftHandler::startInstallation(const QString& gamePath){
    QFile f(gamePath + "/installation");

    if (!f.open(QIODevice::WriteOnly)){
        newDebug() << "ERROR: Failed to open installation file";
    }

    setInstallation(true);
}

void MinecraftHandler::stopInstallation(const QString& gamePath){
    QFile f(gamePath + "/installation");

    if (!f.open(QIODevice::ReadOnly)){
        newDebug() << "ERROR: Failed to open installation file";
    }

    f.remove();

    setInstallation(false);
}

void MinecraftHandler::checkInstallation(const QString& gamePath)
{
    if (QFileInfo::exists(gamePath + "/installation")) {
        setInstallation(true);
        return;
    }

    setInstallation(false);
}

QString MinecraftHandler::downloadNeoforge(const QString& version)
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/downloads";
    QDir().mkpath(dir);
    QString path = dir + QStringLiteral("/neoforge-%1-installer.jar").arg(version);
    QString url  = QStringLiteral("https://maven.neoforged.net/releases/net/neoforged/neoforge/%1/neoforge-%1-installer.jar").arg(version);

    newDebug() << "[NeoForge] Downloading:" << url;
    emit q->installProgress(QStringLiteral("Подготовка загрузки"), QStringLiteral("neoforge-%1-installer.jar").arg(version), 0.0);

    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);

    QFile *file = new QFile(path);
    if (!file->open(QIODevice::WriteOnly)) {
        delete file;
        emit q->installError(QStringLiteral("Не удалось создать файл"), QStringLiteral("Проверьте права на запись в: ") + dir, path, false);
        return "";
    }

    QNetworkReply *reply = m_manager->get(req);

    connect(reply, &QNetworkReply::downloadProgress, this, [this, version](qint64 received, qint64 total) {
        if (total <= 0) { emit q->installProgress(QStringLiteral("Загрузка установщика NeoForge"), QStringLiteral("neoforge-%1-installer.jar").arg(version), -1.0); return; }
        double p = double(received) / double(total);
        QString hr = QString("%1 / %2 МБ").arg(received / 1048576.0, 0, 'f', 1).arg(total / 1048576.0, 0, 'f', 1);
        emit q->installProgress(QStringLiteral("Загрузка установщика NeoForge"), hr, p);
    });

    connect(reply, &QNetworkReply::readyRead, this, [file, reply]() { file->write(reply->readAll()); });

    connect(reply, &QNetworkReply::finished, this, [this, reply, file, path]() {
        file->close();
        file->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[NeoForge] Error:" << reply->errorString();
            emit q->installError(QStringLiteral("Ошибка скачивания установщика"), reply->errorString(), path, true);
            reply->deleteLater();
            return;
        }
        newDebug() << "[NeoForge] Downloaded OK:" << path;
        emit q->installProgress(QStringLiteral("Установщик загружен"), QStringLiteral("Запуск установки..."), 1.0);
        emit downloadFinished();
        reply->deleteLater();
        installNeoforge(path);
    });

    return path;
}

QString MinecraftHandler::installNeoforge(const QString& installerPath){
    QString javaPath = sm->javaPath();
    if (javaPath.isEmpty()){ emit q->installError("Ошибка", "Путь к Java не найден", "", false); return ""; }

    emit q->installProgress(QStringLiteral("Установка neoforge"), "Начало...", 0.0);

    QString workingDir = sm->gameDir() + "/" + m_buildName;
    QDir().mkpath(workingDir);

    QProcess *installerProcess = new QProcess(this);
    installerProcess->setWorkingDirectory(workingDir);
    installerProcess->setProcessChannelMode(QProcess::MergedChannels);
    ensureLauncherProfile(workingDir);

    auto* buffer = new QString();
    auto* processorStep = new int(0);

    connect(installerProcess, &QProcess::readyReadStandardOutput, this, [this, installerProcess, buffer, processorStep](){
        buffer->append(QString::fromLocal8Bit(installerProcess->readAllStandardOutput()));
        buffer->replace("\r\n", "\n"); buffer->replace('\r', '\n');
        if (buffer->size() > 100000) *buffer = buffer->right(2000);

        int nl;
        while ((nl = buffer->indexOf('\n')) >= 0) {
            QString line = buffer->left(nl).trimmed();
            buffer->remove(0, nl + 1);
            if (line.isEmpty()) continue;
            newDebug() << "+[NeoforgeInstaller] " << line;

            double p = -1;
            QString stage = line;
            if (line.contains("Extracting json", Qt::CaseInsensitive)) { stage = "Чтение профиля установки"; p = 0.05; }
            else if (line.contains("Considering minecraft", Qt::CaseInsensitive)) { stage = "Проверка Minecraft"; p = 0.10; }
            else if (line.contains("Downloading libraries", Qt::CaseInsensitive)) { stage = "Скачивание библиотек"; p = 0.25; }
            else if (line.contains("Downloading asset", Qt::CaseInsensitive)) { stage = "Скачивание ресурсов"; p = 0.40; }
            else if (line.contains("Running processor", Qt::CaseInsensitive)) {
                (*processorStep)++;
                p = 0.45 + qMin(1.0, *processorStep / 8.0) * 0.45;
                stage = QString("Ремаппинг (%1/8)").arg(*processorStep);
            }
            else if (line.contains("Building processor classpath", Qt::CaseInsensitive)) { stage = "Финальная сборка"; p = 0.92; }
            else if (line.contains("Installation complete", Qt::CaseInsensitive) || line.contains("You can now", Qt::CaseInsensitive)) { stage = "Готово"; p = 1.0; }

            m_pendingStage = "Установка NeoForge"; m_pendingDetails = stage; m_pendingProgress = p;
            m_hasPendingUpdate = true;
            if (!m_progressThrottle->isActive()) m_progressThrottle->start();
        }
    });

    connect(installerProcess, &QProcess::readyReadStandardError, this, [installerProcess](){
        QString error = QString::fromLocal8Bit(installerProcess->readAllStandardError());
        newDebug() << "-[NeoforgeInstaller] ERROR: " << error;
    });

    connect(installerProcess, &QProcess::finished, this, [this, installerProcess, workingDir, buffer, processorStep](int exitCode, QProcess::ExitStatus exitStatus){
        delete buffer; delete processorStep;
        installerProcess->deleteLater();
        if (exitStatus == QProcess::NormalExit && exitCode == 0){
            newDebug() << "[NeoforgeInstaller] Neoforge successfully installed!";
            downloadAssets(workingDir, m_mcVersion);
            downloadVanillaLibraries(workingDir);

            finalizeInstall(workingDir);

        } else {
            newDebug() << "Ошибка установки neoforge: " << exitCode;
            emit q->installError("Ошибка", QString("Ошибка установки neoforge. Код выхода: %1").arg(exitCode), "", true);
        }
    });

    installerProcess->start(javaPath, QStringList() << "-jar" << installerPath << "--installClient" << workingDir);

    if (!installerProcess->waitForStarted(10000)){
        delete buffer; delete processorStep;
        installerProcess->deleteLater();
        emit q->installError("Ошибка запуска", "Не удалось запустить процесс установки.", "", false);
    }
    return workingDir;
}

QStringList MinecraftHandler::parseVersionJson(const QString& gamePath)
{
    const QString versionId = getVersionId();
    const QString jsonPath  = gamePath + "/versions/" + versionId + "/" + versionId + ".json";

    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) {
        newDebug() << "[MC] ERROR: cannot open " << jsonPath;
        emit q->installError("Ошибка", "Не найден JSON версии", jsonPath, false);
        return {};
    }
    QJsonObject json = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    QSet<QString> classpathSet;
    const QString librariesDir = gamePath + "/libraries";

    auto processLibraries = [&](const QJsonObject& j) {
        for (const QJsonValue& v : j["libraries"].toArray()) {
            QJsonObject lib = v.toObject();
            if (lib.contains("rules")) {
                bool allowed = false;
                for (const QJsonValue& rv : lib["rules"].toArray()) {
                    QJsonObject rule = rv.toObject();
                    QString action = rule["action"].toString();
                    bool matches = true;
                    if (rule.contains("os")) {
                        QJsonObject os = rule["os"].toObject();
                        if (os.contains("name") && os["name"].toString() != "windows") matches = false;
                        if (os.contains("arch")) {
                            const QString arch = os["arch"].toString();
                            const bool is64 = QSysInfo::currentCpuArchitecture().contains("64");
                            if ((arch == "x86" && is64) || (arch == "x86_64" && !is64)) matches = false;
                        }
                    }
                    if (matches) allowed = (action == "allow");
                }
                if (!allowed) continue;
            }

            QString pathStr;
            if (lib.contains("downloads") && lib["downloads"].toObject().contains("artifact"))
                pathStr = lib["downloads"].toObject()["artifact"].toObject()["path"].toString();
            else if (lib.contains("name")) {
                QStringList parts = lib["name"].toString().split(':');
                if (parts.size() >= 3) {
                    QString groupPath = parts[0]; groupPath.replace('.', '/');
                    QString fileName = parts[1] + "-" + parts[2] + (parts.size() >= 4 ? "-" + parts[3] : "") + ".jar";
                    pathStr = groupPath + "/" + parts[1] + "/" + parts[2] + "/" + fileName;
                }
            }
            if (pathStr.isEmpty()) continue;
            classpathSet.insert(QDir::toNativeSeparators(QDir::cleanPath(QDir(librariesDir).absoluteFilePath(pathStr))));
        }
    };

    QStringList jsonPaths; jsonPaths << jsonPath;
    int requiredJava = json["javaVersion"].toObject()["majorVersion"].toInt(0);

    QJsonObject tempJson = json;
    while (tempJson.contains("inheritsFrom")) {
        QString parentVersion = tempJson["inheritsFrom"].toString();
        QString parentPath = gamePath + "/versions/" + parentVersion + "/" + parentVersion + ".json";
        jsonPaths.prepend(parentPath);
        QFile pf(parentPath);
        if (pf.open(QIODevice::ReadOnly)) {
            tempJson = QJsonDocument::fromJson(pf.readAll()).object();
            int v = tempJson["javaVersion"].toObject()["majorVersion"].toInt(0);
            if (v > requiredJava) requiredJava = v;
            pf.close();
        } else { newDebug() << "[MC] ERROR: cannot open parent json" << parentPath; break; }
    }

    int installedJava = detectJavaMajorVersion();

    if (requiredJava > 0) {
        QString suited = sm->findJavaByMajor(requiredJava);
        if (suited.isEmpty()) {
            emit q->installError("Несовместимая версия Java",
                                 QString("Для Minecraft %1 требуется Java %2, но в системе её нет.\n"
                                         "Установите JDK %2 и повторите.")
                                     .arg(m_mcVersion).arg(requiredJava), "", false);
            return {};
        }

        m_launchJavaPath = suited;
        const int suitedMajor = sm->getJavaMajorVersion(suited);
        newDebug() << "[MC] Required Java:" << requiredJava
                   << "using Java" << suitedMajor << "at" << suited;

        if (suitedMajor != installedJava)
            newDebug() << "[MC] NOTE: sm->javaPath() is Java" << installedJava
                       << "but this version will use Java" << suitedMajor;
    } else {
        m_launchJavaPath = sm->javaPath();
    }

    for (const QString& jPath : jsonPaths) {
        QFile jf(jPath);
        if (jf.open(QIODevice::ReadOnly)) {
            processLibraries(QJsonDocument::fromJson(jf.readAll()).object());
            jf.close();
        }
    }

    if (m_buildLoader == "vanilla" || m_buildLoader == "fabric") {
        QString baseVersion = json.contains("inheritsFrom") ? json["inheritsFrom"].toString() : m_mcVersion;
        QString mcJar = gamePath + "/versions/" + baseVersion + "/" + baseVersion + ".jar";
        if (QFile::exists(mcJar)) classpathSet.insert(QDir::toNativeSeparators(QDir::cleanPath(mcJar)));
        else newDebug() << "[MC] WARNING: vanilla jar missing:" << mcJar;
    }

    QStringList classpathList = classpathSet.values();
    QString classPath = classpathList.join(';');
    const bool usesBSL = (m_buildLoader == "forge" || m_buildLoader == "neoforge");

    if (usesBSL) {
        QFile cpFile(gamePath + "/legacyClassPath.txt");
        if (cpFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            for (const QString& entry : classpathList) { cpFile.write(entry.toUtf8()); cpFile.write("\n"); }
            cpFile.close();
        }
    }

    const QString versionName = versionId;
    QHash<QString, QString> vars;
    vars["${auth_player_name}"] = sm->username();
    vars["${auth_uuid}"] = "00000000-0000-0000-0000-000000000000";
    vars["${auth_access_token}"] = "0"; vars["${auth_session}"] = "0";
    vars["${user_type}"] = "legacy"; vars["${version_name}"] = versionName;
    vars["${version_type}"] = "release"; vars["${game_directory}"] = gamePath;
    vars["${assets_root}"] = gamePath + "/assets"; vars["${assets_index_name}"] = m_mcVersion;
    vars["${natives_directory}"] = gamePath + "/natives"; vars["${library_directory}"] = librariesDir;
    vars["${classpath}"] = classPath; vars["${launcher_name}"] = "SeaLauncher";
    vars["${launcher_version}"] = "1.0"; vars["${resolution_width}"] = "925";
    vars["${resolution_height}"] = "530"; vars["${game_assets}"] = gamePath + "/assets";
    vars["${user_properties}"] = "{}"; vars["${clientid}"] = "0"; vars["${auth_xuid}"] = "0";
    vars["${classpath_separator}"] = ";"; vars["${path_separator}"] = ";";
    if (usesBSL) { vars["${fml.neoForgeVersion}"] = m_buildVersion; vars["${fml.mcVersion}"] = m_mcVersion; vars["${fml.fmlVersion}"] = ""; }
    if (m_buildLoader == "fabric") vars["${fabric.gameJarPath}"] = gamePath + "/versions/" + m_mcVersion + "/" + m_mcVersion + ".jar";

    auto substitute = [&vars](QString s) -> QString {
        for (auto it = vars.constBegin(); it != vars.constEnd(); ++it) s.replace(it.key(), it.value());
        return s;
    };
    auto ruleMatches = [](const QJsonObject& rule) -> bool {
        QJsonObject os = rule.value("os").toObject();
        bool matches = true;
        if (os.contains("name") && os["name"].toString().toLower() != "windows") matches = false;
        if (os.contains("arch")) {
            const QString arch = os["arch"].toString();
            const bool is64 = QSysInfo::currentCpuArchitecture().contains("64");
            if ((arch == "x86" && is64) || (arch == "x86_64" && !is64)) matches = false;
        }
        return matches;
    };
    auto expandArray = [&substitute, &ruleMatches](const QJsonArray& arr) -> QStringList {
        QStringList out;
        for (const QJsonValue& v : arr) {
            if (v.isString()) out << substitute(v.toString());
            else if (v.isObject()) {
                QJsonObject obj = v.toObject();
                bool allowed = true;
                if (obj.contains("rules")) {
                    allowed = false;
                    for (const QJsonValue& rv : obj["rules"].toArray()) {
                        QJsonObject rule = rv.toObject();
                        if (ruleMatches(rule)) allowed = (rule.value("action").toString() == "allow");
                    }
                }
                if (allowed && obj.contains("value")) {
                    if (obj["value"].isString()) out << substitute(obj["value"].toString());
                    else if (obj["value"].isArray())
                        for (const QJsonValue& sub : obj["value"].toArray())
                            if (sub.isString()) out << substitute(sub.toString());
                }
            }
        }
        return out;
    };

    QStringList jvmArgs;
    jvmArgs << "-Xmx" + QString::number(sm->ramMb()) + "M";
    jvmArgs << "-Djava.library.path=" + gamePath + "/natives";
    if (usesBSL) jvmArgs << "-DlegacyClassPath=" + classPath;
    else jvmArgs << "-cp" << classPath;
    if (!sm->jvmArgs().trimmed().isEmpty()) jvmArgs << sm->jvmArgs().split(' ', Qt::SkipEmptyParts);

    const int javaMajor = detectJavaMajorVersion();
    for (const QString& jPath : jsonPaths) {
        QFile jf(jPath);
        if (jf.open(QIODevice::ReadOnly)) {
            QJsonObject jObj = QJsonDocument::fromJson(jf.readAll()).object();
            QJsonObject args = jObj["arguments"].toObject();
            if (args.contains("jvm"))
                for (const QString& arg : expandArray(args["jvm"].toArray())) {
                    if (!isJvmArgSupported(arg, javaMajor)) { newDebug() << "[MC] Skipping JVM arg (Java" << javaMajor << "):" << arg; continue; }
                    jvmArgs << arg;
                }
            jf.close();
        }
    }

    QStringList gameArgs;
    bool foundGameArgs = false;
    for (const QString& jPath : jsonPaths) {
        QFile jf(jPath);
        if (!jf.open(QIODevice::ReadOnly)) continue;
        QJsonObject jObj = QJsonDocument::fromJson(jf.readAll()).object();
        jf.close();
        if (jObj.contains("minecraftArguments")) {
            gameArgs << substitute(jObj["minecraftArguments"].toString()).split(' ', Qt::SkipEmptyParts);
            foundGameArgs = true; continue;
        }
        QJsonObject args = jObj["arguments"].toObject();
        if (args.contains("game")) { gameArgs << expandArray(args["game"].toArray()); foundGameArgs = true; }
    }
    if (!foundGameArgs) {
        gameArgs << "--username" << sm->username() << "--version" << versionName
                 << "--gameDir" << gamePath << "--assetsDir" << gamePath + "/assets"
                 << "--assetIndex" << m_mcVersion << "--uuid" << "00000000-0000-0000-0000-000000000000"
                 << "--accessToken" << "0" << "--userType" << "legacy";
    }
    QStringList filteredGameArgs;
    for (const QString& arg : gameArgs) if (!arg.startsWith("--quickPlay")) filteredGameArgs << arg;
    gameArgs = filteredGameArgs;
    gameArgs.removeAll("--demo");

    QString defaultMain = "net.minecraft.client.main.Main";
    if (m_buildLoader == "neoforge" || m_buildLoader == "forge") defaultMain = "cpw.mods.bootstraplauncher.BootstrapLauncher";
    else if (m_buildLoader == "fabric") defaultMain = "net.fabricmc.loader.impl.launch.knot.KnotClient";
    QString mainClass = json["mainClass"].toString(defaultMain);

    QStringList fullCommand;
    fullCommand << jvmArgs << mainClass << gameArgs;

    newDebug() << "[MC] loader:" << m_buildLoader << "versionId:" << versionId
               << "classpath:" << classpathList.size() << "mainClass:" << mainClass;

    return fullCommand;
}

void MinecraftHandler::downloadLibrariesFromJson(const QJsonObject& json, const QString& gamePath)
{
    const QString librariesDir = gamePath + "/libraries";
    QList<QPair<QString, QString>> queue;
    int skipped = 0;

    for (const QJsonValue& v : json["libraries"].toArray()) {
        QJsonObject lib = v.toObject();
        if (lib.contains("rules")) {
            bool allowed = false;
            for (const QJsonValue& rv : lib["rules"].toArray()) {
                QJsonObject rule = rv.toObject();
                QString action = rule["action"].toString();
                bool matches = true;
                if (rule.contains("os")) {
                    QJsonObject os = rule["os"].toObject();
                    if (os.contains("name") && os["name"].toString() != "windows") matches = false;
                    if (os.contains("arch")) {
                        const QString arch = os["arch"].toString();
                        const bool is64 = QSysInfo::currentCpuArchitecture().contains("64");
                        if ((arch == "x86" && is64) || (arch == "x86_64" && !is64)) matches = false;
                    }
                }
                if (matches) allowed = (action == "allow");
            }
            if (!allowed) continue;
        }
        QString pathStr, urlStr;
        if (lib.contains("downloads") && lib["downloads"].toObject().contains("artifact")) {
            QJsonObject art = lib["downloads"].toObject()["artifact"].toObject();
            pathStr = art["path"].toString(); urlStr = art["url"].toString();
        } else if (lib.contains("name")) {
            QStringList parts = lib["name"].toString().split(':');
            if (parts.size() >= 3) {
                QString groupPath = parts[0]; groupPath.replace('.', '/');
                QString fileName = parts[1] + "-" + parts[2] + (parts.size() >= 4 ? "-" + parts[3] : "") + ".jar";
                pathStr = groupPath + "/" + parts[1] + "/" + parts[2] + "/" + fileName;
                QString base = lib["url"].toString();
                if (base.isEmpty()) base = "https://maven.fabricmc.net/";
                if (!base.endsWith('/')) base += '/';
                urlStr = base + pathStr;
            }
        }
        if (pathStr.isEmpty() || urlStr.isEmpty()) continue;
        QString fullPath = QDir(librariesDir).absoluteFilePath(pathStr);
        if (QFile::exists(fullPath)) { skipped++; continue; }
        queue << qMakePair(urlStr, fullPath);
    }

    if (queue.isEmpty()) { newDebug() << "[Libs] All present (" << skipped << " skipped)"; return; }
    newDebug() << "[Libs] Downloading" << queue.size() << "libraries";

    std::array<QNetworkAccessManager, 4> managers;
    QEventLoop loop;
    constexpr int PARALLEL = 24;

    struct State { int total = 0; int index = 0; int active = 0; int done = 0; int failed = 0; int nextMgr = 0; };
    auto state = std::make_shared<State>();
    state->total = queue.size();

    auto pump = std::make_shared<std::function<void()>>();
    *pump = [this, &loop, &managers, &queue, state, pump]() {
        while (state->active < PARALLEL && state->index < state->total) {
            const auto item = queue[state->index++];
            state->active++;
            QDir().mkpath(QFileInfo(item.second).absolutePath());

            QNetworkRequest req{QUrl(item.first)};
            req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            QNetworkReply* reply = managers[state->nextMgr++ % 4].get(req);
            QFile* file = new QFile(item.second);
            if (!file->open(QIODevice::WriteOnly)) {
                delete file; reply->deleteLater();
                state->active--; state->done++; state->failed++;
                if (state->done >= state->total) loop.quit(); else (*pump)();
                continue;
            }

            auto* timeout = new QTimer(reply);
            timeout->setSingleShot(true);
            timeout->setInterval(30000);
            QObject::connect(timeout, &QTimer::timeout, reply, [reply]() { reply->abort(); });
            timeout->start();

            QObject::connect(reply, &QNetworkReply::readyRead, this, [reply, file, timeout]() {
                timeout->start();
                file->write(reply->readAll());
            });
            QObject::connect(reply, &QNetworkReply::finished, this, [this, &loop, state, pump, reply, file]() {
                const int err = reply->error();
                file->close(); file->deleteLater(); reply->deleteLater();
                if (err != QNetworkReply::NoError) {
                    state->failed++;
                    QFile::remove(file->fileName());
                }
                state->active--; state->done++;
                if (state->done % 5 == 0 || state->done == state->total)
                    emit q->installProgress("Скачивание библиотек", QString("%1 / %2").arg(state->done).arg(state->total), double(state->done) / state->total);
                if (state->done >= state->total) { loop.quit(); return; }
                (*pump)();
            });
        }
    };

    (*pump)();
    loop.exec();
    newDebug() << "[Libs] Done. Failed:" << state->failed;
}

void MinecraftHandler::downloadVanillaLibraries(const QString& gamePath)
{
    const QString jsonPath = gamePath + "/versions/" + m_mcVersion + "/" + m_mcVersion + ".json";
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) { newDebug() << "[Vanilla] Cannot open" << jsonPath; return; }
    QJsonObject json = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    const QString librariesDir = gamePath + "/libraries";
    QList<QPair<QString, QString>> queue;

    for (const QJsonValue& v : json["libraries"].toArray()) {
        QJsonObject lib = v.toObject();
        if (!lib.contains("downloads")) continue;
        QJsonObject downloads = lib["downloads"].toObject();
        if (!downloads.contains("artifact")) continue;
        QJsonObject artifact = downloads["artifact"].toObject();
        QString url = artifact["url"].toString(), path = artifact["path"].toString();
        if (url.isEmpty() || path.isEmpty()) continue;
        QString fullPath = QDir(librariesDir).absoluteFilePath(path);
        if (QFile::exists(fullPath)) continue;
        queue << qMakePair(url, fullPath);
    }

    if (queue.isEmpty()) { newDebug() << "[Vanilla] All present"; return; }
    newDebug() << "[Vanilla] Downloading" << queue.size() << "libraries";

    std::array<QNetworkAccessManager, 4> managers;
    QEventLoop loop;
    constexpr int PARALLEL = 24;

    struct State { int total = 0; int index = 0; int active = 0; int done = 0; int failed = 0; int nextMgr = 0; };
    auto state = std::make_shared<State>();
    state->total = queue.size();

    auto pump = std::make_shared<std::function<void()>>();
    *pump = [this, &loop, &managers, &queue, state, pump]() {
        while (state->active < PARALLEL && state->index < state->total) {
            const auto item = queue[state->index++];
            state->active++;
            QDir().mkpath(QFileInfo(item.second).absolutePath());

            QNetworkRequest req{QUrl(item.first)};
            req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            QNetworkReply* reply = managers[state->nextMgr++ % 4].get(req);
            QFile* file = new QFile(item.second);
            if (!file->open(QIODevice::WriteOnly)) {
                delete file; reply->deleteLater();
                state->active--; state->done++; state->failed++;
                if (state->done >= state->total) loop.quit(); else (*pump)();
                continue;
            }
            auto* timeout = new QTimer(reply);
            timeout->setSingleShot(true);
            timeout->setInterval(30000);
            QObject::connect(timeout, &QTimer::timeout, reply, [reply]() { reply->abort(); });
            timeout->start();

            QObject::connect(reply, &QNetworkReply::readyRead, this, [reply, file, timeout]() {
                timeout->start();
                file->write(reply->readAll());
            });
            QObject::connect(reply, &QNetworkReply::finished, this, [this, &loop, state, pump, reply, file]() {
                const int err = reply->error();
                file->close(); file->deleteLater(); reply->deleteLater();
                if (err != QNetworkReply::NoError) { state->failed++; QFile::remove(file->fileName()); }
                state->active--; state->done++;
                if (state->done % 5 == 0 || state->done == state->total)
                    emit q->installProgress("Скачивание библиотек Minecraft", QString("%1 / %2").arg(state->done).arg(state->total), double(state->done) / state->total);
                if (state->done >= state->total) { loop.quit(); return; }
                (*pump)();
            });
        }
    };

    (*pump)();
    loop.exec();
    newDebug() << "[Vanilla] Done. Failed:" << state->failed;
}

QString MinecraftHandler::fetchUrl(const QString& url, int timeoutMs, int retries)
{
    for (int attempt = 0; attempt <= retries; ++attempt) {
        newDebug() << "[Fetch] attempt" << attempt << "url:" << url;

        QNetworkRequest req{QUrl(url)};
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::RedirectPolicy::NoLessSafeRedirectPolicy);
        QNetworkReply* reply = m_manager->get(req);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        timer.setInterval(timeoutMs);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        timer.start();
        loop.exec();

        const bool timedOut = !timer.isActive();
        if (timedOut) {
            newDebug() << "[Fetch] TIMEOUT url:" << url;
            reply->abort();
            reply->deleteLater();
            continue;
        }
        timer.stop();

        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray data = reply->readAll();
            reply->deleteLater();
            newDebug() << "[Fetch] OK bytes:" << data.size() << "url:" << url;
            return QString::fromUtf8(data);
        }

        newDebug() << "[Fetch] err:" << reply->errorString() << "url:" << url;
        reply->deleteLater();
    }
    return QString();
}

void MinecraftHandler::downloadAssets(const QString& gamePath, const QString& assetIndexId)
{
    const QString indexesDir = gamePath + "/assets/indexes";
    const QString objectsDir = gamePath + "/assets/objects";
    QDir().mkpath(indexesDir); QDir().mkpath(objectsDir);

    const QString versionJson = gamePath + "/versions/" + m_mcVersion + "/" + m_mcVersion + ".json";
    QFile vf(versionJson);
    if (!vf.open(QIODevice::ReadOnly)) { newDebug() << "[Assets] Cannot open" << versionJson; return; }
    QJsonObject vJson = QJsonDocument::fromJson(vf.readAll()).object();
    vf.close();

    QJsonObject assetIndex = vJson["assetIndex"].toObject();
    const QString idxUrl = assetIndex["url"].toString();
    const QString idxPath = indexesDir + "/" + assetIndexId + ".json";

    if (!QFile::exists(idxPath) || QFileInfo(idxPath).size() == 0) {
        newDebug() << "[Assets] Fetching index:" << idxUrl;
        QString idxData = fetchUrl(idxUrl, 30000, 3);
        if (idxData.isEmpty()) {
            newDebug() << "[Assets] Index fetch failed";
            emit q->installError("Ошибка установки ассетов",
                                 "Не удалось скачать индекс ассетов",
                                 "", true);
            return;
        }
        QFile f(idxPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { newDebug() << "[Assets] Cannot write index" << idxPath; return; }
        f.write(idxData.toUtf8());
        f.close();
    }

    QFile inf(idxPath);
    if (!inf.open(QIODevice::ReadOnly)) { newDebug() << "[Assets] Cannot read index" << idxPath; return; }
    QJsonObject index = QJsonDocument::fromJson(inf.readAll()).object();
    inf.close();

    struct Asset { QString url; QString path; qint64 size = 0; };
    QList<Asset> queue;
    QJsonObject objects = index["objects"].toObject();
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QJsonObject obj = it.value().toObject();
        const QString hash = obj["hash"].toString();
        const QString sub = hash.left(2);
        const QString path = objectsDir + "/" + sub + "/" + hash;
        if (QFile::exists(path) && QFileInfo(path).size() > 0) continue;
        Asset a;
        a.url = "https://resources.download.minecraft.net/" + sub + "/" + hash;
        a.path = path;
        a.size = (qint64)obj["size"].toDouble();
        queue << a;
    }

    const int total = queue.size();
    if (total == 0) { newDebug() << "[Assets] All present"; return; }

    std::array<QNetworkAccessManager, 4> managers;
    QEventLoop loop;
    constexpr int PARALLEL = 24;
    constexpr int NO_DATA_TIMEOUT_MS = 30000;

    struct State {
        int total = 0;
        qint64 totalBytes = 0;
        qint64 doneBytes = 0;
        int index = 0;
        int active = 0;
        int done = 0;
        int failed = 0;
        int nextMgr = 0;
    };
    auto state = std::make_shared<State>();
    state->total = total;
    for (const Asset& a : queue) state->totalBytes += a.size;
    newDebug() << "[Assets] Downloading" << state->total << "files," << state->totalBytes << "bytes";

    auto pump = std::make_shared<std::function<void()>>();
    *pump = [this, &loop, &managers, &queue, state, pump]() {
        while (state->active < PARALLEL && state->index < state->total) {
            const Asset a = queue[state->index++];
            state->active++;
            QDir().mkpath(QFileInfo(a.path).absolutePath());

            QNetworkRequest req{QUrl(a.url)};
            req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            QNetworkReply* reply = managers[state->nextMgr++ % 4].get(req);
            QFile* file = new QFile(a.path);
            file->open(QIODevice::WriteOnly);

            auto* timeout = new QTimer(reply);
            timeout->setSingleShot(true);
            timeout->setInterval(NO_DATA_TIMEOUT_MS);
            QObject::connect(timeout, &QTimer::timeout, reply, [reply]() { reply->abort(); });
            timeout->start();

            QObject::connect(reply, &QNetworkReply::readyRead, this, [reply, file, timeout]() {
                timeout->start();
                file->write(reply->readAll());
            });
            QObject::connect(reply, &QNetworkReply::finished, this, [this, &loop, state, pump, reply, file, a]() {
                const int err = reply->error();
                file->close(); file->deleteLater(); reply->deleteLater();
                if (err != QNetworkReply::NoError) {
                    state->failed++;
                    QFile::remove(file->fileName());
                    newDebug() << "[Assets] file failed:" << a.path << "err:" << err;
                } else {
                    state->doneBytes += a.size;
                }
                state->active--; state->done++;
                if (state->done == 1 || state->done % 20 == 0 || state->done == state->total)
                    emit q->installProgress("Загрузка ассетов",
                                            QString("%1 / %2 МБ").arg(state->doneBytes / 1048576.0, 0, 'f', 1).arg(state->totalBytes / 1048576.0, 0, 'f', 1),
                                            state->totalBytes > 0 ? double(state->doneBytes) / double(state->totalBytes) : 0.0);
                if (state->done >= state->total) { loop.quit(); return; }
                (*pump)();
            });
        }
    };

    (*pump)();
    loop.exec();
    newDebug() << "[Assets] Done. Failed:" << state->failed;
    if (state->failed > 0) emit q->installError("Ошибка загрузки ассетов", QString("Не удалось скачать %1 файлов").arg(state->failed), "", true);
}

void MinecraftHandler::deleteCurrentBuild()
{
    if (!q || !sm) return;
    QString path = sm->gameDir() + "/" + m_buildName;
    QDir dir(path);
    if (!dir.exists()) return;
    if (dir.removeRecursively()) {
        newDebug() << "[Delete] Removed:" << path;
        setBuildExists(false);
        setUpdateAvailable(false);
    } else {
        emit q->installError("Ошибка", "Не удалось удалить сборку", path, false);
    }
}

void MinecraftHandler::openLauncherDir()
{
    QString path = sm->gameDir();
    QDir().mkpath(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MinecraftHandler::fetchBuildsList()
{
    QString url = "https://raw.githubusercontent.com/tascarit/MinecraftSborki/refs/heads/main/builds.json";
    if (url.isEmpty()) {
        newDebug() << "[FetchBuilds] URL is empty";
        return;
    }
    newDebug() << "[FetchBuilds] Fetching from:" << url;
    QString data = fetchUrl(url, 10000, 3);
    if (data.isEmpty()) {
        emit q->installError("Ошибка", "Не удалось получить список сборок", url, true);
        return;
    }
    newDebug() << "[FetchBuilds] data: " << data.toStdString().c_str();
    QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
    if (!doc.isArray()) {
        emit q->installError("Ошибка", "Некорректный формат списка сборок", url, false);
        return;
    }
    emit buildsListReady(doc.array());
}

void MinecraftHandler::fetchNews()
{
    QString url = "https://raw.githubusercontent.com/tascarit/MinecraftSborki/refs/heads/main/news.json";
    if (url.isEmpty()) {
        newDebug() << "[FetchNews] URL is empty";
        return;
    }
    newDebug() << "[FetchNews] Fetching from:" << url;
    QString data = fetchUrl(url, 10000, 3);
    if (data.isEmpty()) {
        newDebug() << "[FetchNews] Empty response";
        return;
    }
    QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
    if (!doc.isArray()) {
        newDebug() << "[FetchNews] Not an array";
        return;
    }
    QJsonArray arr = doc.array();
    if (arr.size() > 8) {
        QJsonArray limited;
        for (int i = 0; i < 8; ++i) limited.append(arr.at(i));
        emit newsReady(limited);
    } else {
        emit newsReady(arr);
    }
}

void MinecraftHandler::ensureNarratorDisabled(const QString& gamePath)
{
    QString optionsPath = gamePath + "/options.txt";
    QFile file(optionsPath);

    QString content;
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            newDebug() << "[Options] Cannot read options.txt";
            return;
        }
        content = file.readAll();
        file.close();
    }

    bool needWrite = false;
    if (!content.contains("narrator:")) {
        content += "narrator:0\n";
        needWrite = true;
    }
    if (!content.contains("tutorialStep:")) {
        content += "tutorialStep:none\n";
        needWrite = true;
    }
    if (!content.contains("skipMultiplayerWarning:")) {
        content += "skipMultiplayerWarning:true\n";
        needWrite = true;
    }

    if (needWrite) {
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            file.write(content.toUtf8());
            file.close();
            newDebug() << "[Options] Created/updated options.txt";
        } else {
            newDebug() << "[Options] Cannot write options.txt:" << file.errorString();
        }
    }
}

void MinecraftHandler::downloadFileParallel(const QString& url, const QString& savePath, int chunkSizeMB, int maxParallel)
{
    QNetworkAccessManager* mgr = new QNetworkAccessManager(this);
    QNetworkRequest probeReq{QUrl(url)};
    probeReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    probeReq.setRawHeader("Range", "bytes=0-0");
    QNetworkReply* probeReply = mgr->get(probeReq);

    connect(probeReply, &QNetworkReply::finished, this, [=, this]() {
        const int probeErr = probeReply->error();
        const QString probeErrStr = probeReply->errorString();
        const int httpStatus = probeReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray rangeHeader = probeReply->rawHeader("Content-Range");
        probeReply->deleteLater();

        newDebug() << "[DL] probe http:" << httpStatus << "err:" << probeErr << probeErrStr;
        newDebug() << "[DL] Content-Range:" << rangeHeader;

        if (probeErr != QNetworkReply::NoError) {
            emit q->installError("Ошибка", "Не удалось получить файл: " + probeErrStr, url, false);
            mgr->deleteLater(); emit downloadFinished(); return;
        }
        if (rangeHeader.isEmpty()) { newDebug() << "[DL] No Range support, single-stream"; downloadSingleStream(url, savePath, mgr); return; }

        int slashPos = rangeHeader.indexOf('/');
        if (slashPos < 0) { emit q->installError("Ошибка", "Некорректный Content-Range", url, false); mgr->deleteLater(); emit downloadFinished(); return; }

        qint64 fileSize = rangeHeader.mid(slashPos + 1).toLongLong();
        if (fileSize <= 0) { emit q->installError("Ошибка", "Размер файла 0", url, false); mgr->deleteLater(); emit downloadFinished(); return; }

        newDebug() << "[DL] size:" << fileSize;

        QFile file(savePath);
        if (!file.open(QIODevice::ReadWrite)) {
            emit q->installError("Ошибка", "Не удалось создать файл: " + savePath + " (" + file.errorString() + ")", savePath, false);
            mgr->deleteLater(); emit downloadFinished(); return;
        }
        file.resize(fileSize);
        file.close();

        const qint64 chunkSize = qint64(chunkSizeMB) * 1024 * 1024;
        QList<Chunk> chunks;
        for (qint64 pos = 0; pos < fileSize; pos += chunkSize)
            chunks << Chunk{pos, qMin(pos + chunkSize - 1, fileSize - 1)};

        newDebug() << "[DL] chunks:" << chunks.size() << "parallel:" << maxParallel;
        downloadChunks(mgr, url, savePath, chunks, maxParallel);
    });
}

void MinecraftHandler::downloadSingleStream(const QString& url, const QString& savePath, QNetworkAccessManager* mgr)
{
    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = mgr->get(req);
    QFile* file = new QFile(savePath);
    file->open(QIODevice::WriteOnly);

    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    auto lastBytes = std::make_shared<qint64>(0);

    connect(reply, &QNetworkReply::readyRead, this, [reply, file]() { file->write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, timer, lastBytes](qint64 recv, qint64 total) {
        if (timer->elapsed() < 200) return;
        double speed = (recv - *lastBytes) / 1048576.0 / (timer->elapsed() / 1000.0);
        *lastBytes = recv; timer->restart();
        if (total > 0)
            emit q->installProgress("Загрузка сборки",
                                    QString("%1 / %2 МБ (%3 МБ/с)").arg(recv / 1048576.0, 0, 'f', 1).arg(total / 1048576.0, 0, 'f', 1).arg(speed, 0, 'f', 1),
                                    double(recv) / double(total));
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, file, mgr]() {
        const int err = reply->error();
        const QString errStr = reply->errorString();
        file->close(); file->deleteLater(); reply->deleteLater(); mgr->deleteLater();
        if (err != QNetworkReply::NoError) emit q->installError("Ошибка загрузки", errStr, "", true);
        emit downloadFinished();
    });
}

void MinecraftHandler::downloadChunks(QNetworkAccessManager* mgr, const QString& url, const QString& savePath, const QList<Chunk>& chunks, int maxParallel)
{
    auto* file = new QFile(savePath);
    if (!file->open(QIODevice::ReadWrite)) {
        newDebug() << "[DL] Cannot open:" << savePath << file->errorString();
        delete file; mgr->deleteLater();
        emit q->installError("Ошибка", "Не удалось открыть файл: " + savePath, savePath, true);
        emit downloadFailed("cannot open file");
        return;
    }

    QNetworkAccessManager m2, m3, m4;
    QNetworkAccessManager* managers[4] = { mgr, &m2, &m3, &m4 };

    struct Task {
        Chunk c;
        int attempt = 0;
        int idx = 0;
        std::shared_ptr<QByteArray> buf;
    };
    struct State {
        int total = 0;
        qint64 totalBytes = 0;
        qint64 downloadedBytes = 0;
        qint64 lastBytes = 0;
        int active = 0;
        int completed = 0;
        int failed = 0;
        int nextMgr = 0;
        QElapsedTimer timer;
        QList<Task> pending;
    };

    auto state = std::make_shared<State>();
    state->total = chunks.size();
    for (const Chunk& c : chunks) state->totalBytes += (c.end - c.start + 1);
    for (int i = 0; i < chunks.size(); i++) {
        Task t;
        t.c = chunks[i];
        t.idx = i;
        t.buf = std::make_shared<QByteArray>();
        state->pending.append(t);
    }
    state->timer.start();

    constexpr int MAX_ATTEMPTS = 5;
    constexpr int NO_DATA_TIMEOUT_MS = 30000;

    newDebug() << "[DL] start, chunks:" << state->total << "parallel:" << maxParallel;

    QEventLoop loop;

    auto pump = std::make_shared<std::function<void()>>();
    *pump = [this, &loop, &managers, maxParallel, state, pump, file, url, mgr, savePath]() {
        while (state->active < maxParallel && !state->pending.isEmpty()) {
            Task task = state->pending.takeFirst();
            state->active++;

            const qint64 off = task.buf->size();
            QNetworkRequest req{QUrl(url)};
            req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            req.setRawHeader("Range", QString("bytes=%1-%2").arg(task.c.start + off).arg(task.c.end).toUtf8());

            QNetworkReply* reply = managers[state->nextMgr++ % 4]->get(req);

            auto* timeout = new QTimer(reply);
            timeout->setSingleShot(true);
            timeout->setInterval(NO_DATA_TIMEOUT_MS);
            QObject::connect(timeout, &QTimer::timeout, reply, [reply]() { reply->abort(); });
            timeout->start();

            QObject::connect(reply, &QNetworkReply::readyRead, this, [this, reply, task, state, timeout]() {
                const QByteArray data = reply->readAll();
                *task.buf += data;
                state->downloadedBytes += data.size();

                timeout->start();

                if (state->timer.elapsed() > 200) {
                    double speed = (state->downloadedBytes - state->lastBytes) / 1048576.0 / (state->timer.elapsed() / 1000.0);
                    state->lastBytes = state->downloadedBytes;
                    state->timer.restart();
                    emit q->installProgress("Загрузка сборки",
                                            QString("%1 / %2 МБ (%3 МБ/с)").arg(state->downloadedBytes / 1048576.0, 0, 'f', 1).arg(state->totalBytes / 1048576.0, 0, 'f', 1).arg(speed, 0, 'f', 1),
                                            double(state->downloadedBytes) / double(state->totalBytes));
                }
            });

            QObject::connect(reply, &QNetworkReply::finished, this, [this, &loop, state, pump, reply, task, file, off, timeout, mgr, savePath]() {
                const int errCode = reply->error();
                const QString errStr = reply->errorString();
                const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const int bufSize = task.buf->size();
                const int expected = int(task.c.end - task.c.start + 1);

                timeout->stop();
                reply->deleteLater();

                bool ok = (errCode == QNetworkReply::NoError) && (bufSize == expected);
                if (ok && off > 0 && httpStatus != 206) {
                    task.buf->clear();
                    ok = false;
                }
                if (ok) {
                    file->seek(task.c.start);
                    if (file->write(*task.buf) != expected) ok = false;
                }

                state->active--;

                if (ok) {
                    state->completed++;
                } else if (task.attempt + 1 < MAX_ATTEMPTS) {
                    newDebug() << "[DL] chunk retry" << task.idx << "attempt" << (task.attempt + 1)
                    << "http:" << httpStatus << "err:" << errCode << errStr
                    << "got:" << bufSize << "expected:" << expected;
                    Task retryTask = task;
                    retryTask.attempt++;
                    state->pending.append(retryTask);
                } else {
                    state->completed++;
                    state->failed++;
                    newDebug() << "[DL] chunk FAILED" << task.idx << "http:" << httpStatus << "err:" << errCode << errStr;
                }

                if (state->completed >= state->total) {
                    file->close();
                    delete file;
                    mgr->deleteLater();
                    const int failCount = state->failed;
                    const bool okFinal = (failCount == 0);
                    loop.quit();
                    QMetaObject::invokeMethod(this, [this, failCount, okFinal, savePath]() {
                        if (!okFinal) {
                            emit q->installError("Ошибка загрузки",
                                                 QString("Не удалось скачать %1 чанков").arg(failCount), savePath, true);
                            emit downloadFailed(QString("failed %1").arg(failCount));
                        } else {
                            emit downloadFinished();
                        }
                    }, Qt::QueuedConnection);
                    return;
                }
                (*pump)();
            });
        }
    };

    (*pump)();
    loop.exec();
}

bool MinecraftHandler::mergeFiles(const QStringList& parts, const QString& outputPath)
{
    QFile out(outputPath);
    if (!out.open(QIODevice::WriteOnly)) { newDebug() << "[Merge] Cannot open:" << outputPath; return false; }
    for (const QString& part : parts) {
        QFile in(part);
        if (!in.open(QIODevice::ReadOnly)) { newDebug() << "[Merge] Cannot open part:" << part; out.close(); return false; }
        const qint64 totalSize = in.size();
        qint64 copied = 0;
        const qint64 bufSize = 4 * 1024 * 1024;
        while (!in.atEnd()) {
            QByteArray buf = in.read(bufSize);
            out.write(buf);
            copied += buf.size();
            emit q->installProgress("Склейка частей",
                                    QString("%1: %2 / %3 МБ").arg(QFileInfo(part).fileName()).arg(copied / 1048576.0, 0, 'f', 1).arg(totalSize / 1048576.0, 0, 'f', 1),
                                    double(copied) / double(totalSize));
        }
        in.close();
    }
    out.close();
    return true;
}

bool MinecraftHandler::extractZip(const QString& zipPath, const QString& targetDir)
{
    QFile probe(zipPath);
    if (!probe.open(QIODevice::ReadOnly)) {
        newDebug() << "[Zip] Cannot open:" << zipPath;
        return false;
    }

    const QByteArray magic = probe.read(4);
    probe.close();

    if (!magic.startsWith("PK")) {
        newDebug() << "[Zip] Not a ZIP, magic:" << magic.toHex() << "size:" << QFileInfo(zipPath).size();
        return false;
    }

    QZipReader zip(zipPath);
    if (!zip.isReadable()) {
        newDebug() << "[Zip] Not readable:" << zipPath;
        return false;
    }

    const auto files = zip.fileInfoList();
    if (files.isEmpty()) {
        newDebug() << "[Zip] Empty archive";
        zip.close();
        return false;
    }

    QString prefix;

    while (true) {
        QSet<QString> topLevel;

        for (const QZipReader::FileInfo& fi : files) {
            QString path = fi.filePath;
            path.replace('\\', '/');

            while (path.startsWith('/'))
                path.remove(0, 1);

            while (path.endsWith('/'))
                path.chop(1);

            if (!path.startsWith(prefix))
                continue;

            path = path.mid(prefix.length());

            if (path.isEmpty())
                continue;

            const int slash = path.indexOf('/');
            topLevel.insert(slash < 0 ? path : path.left(slash));
        }

        if (topLevel.size() != 1)
            break;

        const QString candidate = *topLevel.constBegin();
        bool validWrapper = true;
        bool hasContent = false;

        for (const QZipReader::FileInfo& fi : files) {
            QString path = fi.filePath;
            path.replace('\\', '/');

            while (path.startsWith('/'))
                path.remove(0, 1);

            while (path.endsWith('/'))
                path.chop(1);

            if (!path.startsWith(prefix))
                continue;

            path = path.mid(prefix.length());

            if (path.isEmpty() || path == candidate)
                continue;

            if (!path.startsWith(candidate + "/")) {
                validWrapper = false;
                break;
            }

            hasContent = true;
        }

        if (!validWrapper || !hasContent)
            break;

        prefix += candidate + "/";
        newDebug() << "[Zip] Stripping wrapper:" << prefix;
    }

    if (prefix.isEmpty())
        newDebug() << "[Zip] No wrapper folder, extracting normally";

    QDir().mkpath(targetDir);

    int done = 0;
    int written = 0;
    int failed = 0;
    const int total = files.size();

    for (const QZipReader::FileInfo& fi : files) {
        QString rel = fi.filePath;
        rel.replace('\\', '/');

        while (rel.startsWith('/'))
            rel.remove(0, 1);

        while (rel.endsWith('/'))
            rel.chop(1);

        if (rel.isEmpty()) {
            done++;
            continue;
        }

        if (!prefix.isEmpty()) {
            if (!rel.startsWith(prefix)) {
                done++;
                continue;
            }

            rel = rel.mid(prefix.length());
        }

        if (rel.isEmpty()) {
            done++;
            continue;
        }

        const QString outPath = QDir(targetDir).absoluteFilePath(rel);

        if (fi.isDir) {
            if (!QDir().mkpath(outPath)) {
                newDebug() << "[Zip] Cannot create directory:" << outPath;
                failed++;
            }
        } else {
            QDir().mkpath(QFileInfo(outPath).absolutePath());

            QFile out(outPath);

            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                newDebug() << "[Zip] Cannot write:" << outPath << out.errorString();
                failed++;
            } else {
                const QByteArray data = zip.fileData(fi.filePath);

                if (data.isEmpty() && fi.size > 0) {
                    newDebug() << "[Zip] Cannot read:" << fi.filePath;
                    failed++;
                } else if (out.write(data) != data.size()) {
                    newDebug() << "[Zip] Failed to write:" << outPath;
                    failed++;
                } else {
                    written++;
                }

                out.close();
            }
        }

        done++;

        if (done % 50 == 0 || done == total)
            emit q->installProgress(
                "Распаковка сборки",
                QString("%1 / %2").arg(done).arg(total),
                total > 0 ? double(done) / double(total) : 1.0
                );
    }

    zip.close();

    newDebug() << "[Zip] Done."
               << "written:" << written
               << "failed:" << failed
               << "wrapper:" << (prefix.isEmpty() ? "<none>" : prefix);

    return written > 0 && failed == 0;
}


void MinecraftHandler::installModpack(const QString& baseUrl, const QString& gamePath, int partCount)
{
    QDir().mkpath(gamePath);
    QStringList partPaths, partUrls;
    if (partCount <= 1) { partPaths << gamePath + "/modpack.zip"; partUrls << baseUrl; }
    else for (int i = 1; i <= partCount; i++) {
            partPaths << QString("%1/modpack.part%2").arg(gamePath).arg(i);
            partUrls << QString("%1/modpack.part%2").arg(baseUrl).arg(i);
        }

    struct State {
        int index = 0;
        bool aborted = false;
    };
    auto state = std::make_shared<State>();

    auto downloadNext = std::make_shared<std::function<void()>>();
    *downloadNext = [this, state, partUrls, partPaths, gamePath, partCount, downloadNext]() {
        if (state->aborted) return;
        if (state->index >= partUrls.size()) {
            onAllPartsDownloaded(partPaths, gamePath, partCount);
            return;
        }

        int i = state->index++;
        newDebug() << "[Modpack] Downloading part" << (i + 1) << "of" << partUrls.size();

        QMetaObject::Connection* okConn = new QMetaObject::Connection();
        QMetaObject::Connection* failConn = new QMetaObject::Connection();

        *okConn = connect(this, &MinecraftHandler::downloadFinished, this, [this, downloadNext, okConn, failConn]() {
            disconnect(*okConn); disconnect(*failConn);
            delete okConn; delete failConn;
            QMetaObject::invokeMethod(this, [downloadNext]() { (*downloadNext)(); }, Qt::QueuedConnection);
        });
        *failConn = connect(this, &MinecraftHandler::downloadFailed, this, [state, okConn, failConn](const QString&) {
            disconnect(*okConn); disconnect(*failConn);
            delete okConn; delete failConn;
            state->aborted = true;
            newDebug() << "[Modpack] aborted on part" << state->index;
        });

        emit q->installProgress("Загрузка сборки", QString("Часть %1 из %2").arg(i + 1).arg(partUrls.size()), 0.0);
        downloadFileParallel(partUrls[i], partPaths[i], 4, 16);
    };
    (*downloadNext)();
}

void MinecraftHandler::onAllPartsDownloaded(const QStringList& partPaths, const QString& gamePath, int partCount)
{
    QString finalZip = gamePath + "/modpack.zip";
    newDebug() << "[Modpack] gamePath:" << gamePath << "parts:" << partCount;
    for (const QString& p : partPaths) {
        QFileInfo fi(p);
        newDebug() << "[Modpack] part:" << p << "exists:" << fi.exists() << "size:" << fi.size();
    }

    // тут баг с распаковкой был, extractZip возвращает false даже если все успешно

    auto doExtract = [=, this]() {
        emit q->installProgress("Распаковка сборки", "Подготовка...", 0.0);
        extractZip(finalZip, gamePath);
        QFile::remove(finalZip);
        for (const QString& p : partPaths) QFile::remove(p);
        setBuildExists(true);
        emit q->installProgress("Готово", "Сборка установлена", 1.0);
        emit installFinished();
        emit finished();
    };

    if (partCount > 1) {
        emit q->installProgress("Склейка частей", "Начало...", 0.0);
        if (!mergeFiles(partPaths, finalZip)) { emit q->installError("Ошибка", "Не удалось склеить части", "", true); return; }
        doExtract();
    } else doExtract();
}

bool MinecraftHandler::downloadFile(const QString& url, const QString& savePath,
                                    int timeoutMs, int retries)
{
    for (int attempt = 0; attempt <= retries; attempt++) {
        newDebug() << "[Download] attempt" << attempt << "url:" << url;

        QDir().mkpath(QFileInfo(savePath).absolutePath());

        QNetworkAccessManager manager;
        QEventLoop loop;
        QTimer watchdog;
        watchdog.setSingleShot(true);
        watchdog.setInterval(timeoutMs);

        QNetworkRequest req{QUrl(url)};
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

        QNetworkReply* reply = manager.get(req);
        QFile* file = new QFile(savePath);

        if (!file->open(QIODevice::WriteOnly)) {
            newDebug() << "[Download] Cannot open:" << savePath << file->errorString();
            delete file;
            reply->deleteLater();
            return false;
        }

        QObject::connect(reply, &QNetworkReply::downloadProgress, this,
                         [this, &watchdog, url](qint64 recv, qint64 total) {
                             watchdog.start();
                             if (total > 0) {
                                 emit q->installProgress("Загрузка Minecraft",
                                                         QString("%1 / %2 МБ")
                                                             .arg(recv / 1048576.0, 0, 'f', 1)
                                                             .arg(total / 1048576.0, 0, 'f', 1),
                                                         double(recv) / double(total));
                             }
                         });

        QObject::connect(reply, &QNetworkReply::readyRead,
                         [reply, file]() { file->write(reply->readAll()); });
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&watchdog, &QTimer::timeout, &loop, [&]() {
            newDebug() << "[Download] TIMEOUT" << timeoutMs << "ms:" << url;
            reply->abort();
            loop.quit();
        });

        watchdog.start();
        loop.exec();

        const int err = reply->error();
        const QString errStr = reply->errorString();
        file->close();
        file->deleteLater();
        reply->deleteLater();

        const qint64 written = QFileInfo(savePath).size();
        newDebug() << "[Download] result err:" << err << errStr << "written:" << written;

        if (err == QNetworkReply::NoError && written > 0)
            return true;

        QFile::remove(savePath);
        if (attempt < retries) {
            newDebug() << "[Download] retrying in 500 ms";
            QThread::msleep(500);
        }
    }

    newDebug() << "[Download] ALL ATTEMPTS FAILED:" << url;
    return false;
}

void MinecraftHandler::downloadVanillaFiles(const QString& gamePath, const QString& mcVersion)
{
    const QString vanillaJson = gamePath + "/versions/" + mcVersion + "/" + mcVersion + ".json";
    const QString vanillaJar  = gamePath + "/versions/" + mcVersion + "/" + mcVersion + ".jar";

    if (!QFile::exists(vanillaJson)) {
        newDebug() << "[Vanilla] Downloading JSON";
        QString manifestStr = fetchUrl("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json");
        if (manifestStr.isEmpty()) return;
        QJsonObject manifest = QJsonDocument::fromJson(manifestStr.toUtf8()).object();
        QString vanillaUrl;
        for (const QJsonValue& v : manifest["versions"].toArray()) {
            QJsonObject ver = v.toObject();
            if (ver["id"].toString() == mcVersion) { vanillaUrl = ver["url"].toString(); break; }
        }
        if (vanillaUrl.isEmpty()) return;
        QDir().mkpath(gamePath + "/versions/" + mcVersion);
        downloadFile(vanillaUrl, vanillaJson);
    }

    QFile vf(vanillaJson);
    if (!vf.open(QIODevice::ReadOnly)) return;
    QJsonObject vJson = QJsonDocument::fromJson(vf.readAll()).object();
    vf.close();

    QString clientUrl = vJson["downloads"].toObject()["client"].toObject()["url"].toString();
    if (!clientUrl.isEmpty() && !QFile::exists(vanillaJar)) {
        emit q->installProgress("Скачивание Minecraft", "client.jar", 0.0);
        downloadFile(clientUrl, vanillaJar);
    }

    downloadVanillaLibraries(gamePath);
    QString assetIndexId = vJson["assetIndex"].toObject()["id"].toString();
    if (assetIndexId.isEmpty()) assetIndexId = mcVersion;
    downloadAssets(gamePath, assetIndexId);
}

void MinecraftHandler::installVanilla(const QString& mcVersion)
{
    newDebug() << "[Vanilla] Installing" << mcVersion;
    QString gamePath = sm->gameDir() + "/" + m_buildName;
    QDir().mkpath(gamePath);
    downloadVanillaFiles(gamePath, mcVersion);
    setBuildExists(true);
    sc->closeProgressPanel(view);
    emit installFinished();
    emit finished();
}

void MinecraftHandler::installFabric(const QString& mcVersion, const QString& loaderVersion)
{
    newDebug() << "[Fabric] Installing loader" << loaderVersion << "for MC" << mcVersion;
    QString gamePath = sm->gameDir() + "/" + m_buildName;
    QDir().mkpath(gamePath);

    downloadVanillaFiles(gamePath, mcVersion);

    QString metaUrl = QString("https://meta.fabricmc.net/v2/versions/loader/%1/%2/profile/json").arg(mcVersion, loaderVersion);
    newDebug() << "[Fabric] Fetching profile:" << metaUrl;
    QString profileJson = fetchUrl(metaUrl);
    if (profileJson.isEmpty()) { emit q->installError("Ошибка", "Не удалось получить профиль Fabric", metaUrl, true); return; }

    const QString fabricVersionId = QString("fabric-loader-%1-%2").arg(loaderVersion, mcVersion);
    const QString versionDir = gamePath + "/versions/" + fabricVersionId;
    QDir().mkpath(versionDir);

    QFile jsonFile(versionDir + "/" + fabricVersionId + ".json");
    if (!jsonFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) { emit q->installError("Ошибка", "Не удалось сохранить JSON Fabric", versionDir, false); return; }
    jsonFile.write(profileJson.toUtf8());
    jsonFile.close();

    QJsonObject fabricJson = QJsonDocument::fromJson(profileJson.toUtf8()).object();
    downloadLibrariesFromJson(fabricJson, gamePath);

    finalizeInstall(gamePath);
}

void MinecraftHandler::installForge(const QString& mcVersion, const QString& forgeVersion)
{
    newDebug() << "[Forge] Installing" << forgeVersion << "for MC" << mcVersion;
    QString gamePath = sm->gameDir() + "/" + m_buildName;
    QDir().mkpath(gamePath);

    QString installerUrl = QString("https://maven.minecraftforge.net/net/minecraftforge/forge/%1-%2/forge-%1-%2-installer.jar").arg(mcVersion, forgeVersion);
    QString installerPath = gamePath + "/forge-installer.jar";

    emit q->installProgress("Установка Forge", "Подготовка...", 0.0);
    downloadVanillaFiles(gamePath, mcVersion);
    ensureLauncherProfile(gamePath);

    QNetworkAccessManager mgr; QEventLoop loop;
    QNetworkRequest req{QUrl(installerUrl)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = mgr.get(req);
    QFile* file = new QFile(installerPath);
    if (!file->open(QIODevice::WriteOnly)) {
        delete file; reply->deleteLater();
        emit q->installError("Ошибка", "Не удалось создать файл установщика", installerPath, false);
        return;
    }
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 recv, qint64 total) {
        if (total > 0)
            emit q->installProgress("Установка Forge",
                                    QString("Загрузка установщика: %1 / %2 МБ").arg(recv / 1048576.0, 0, 'f', 1).arg(total / 1048576.0, 0, 'f', 1),
                                    double(recv) / double(total) * 0.1);
    });
    connect(reply, &QNetworkReply::readyRead, [reply, file]() { file->write(reply->readAll()); });
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    file->close(); file->deleteLater();

    const int dlErr = reply->error();
    const QString dlErrStr = reply->errorString();
    reply->deleteLater();
    if (dlErr != QNetworkReply::NoError) { emit q->installError("Ошибка", "Не удалось скачать установщик Forge: " + dlErrStr, installerUrl, true); return; }

    QProcess* installer = new QProcess(this);
    installer->setWorkingDirectory(gamePath);
    installer->setProcessChannelMode(QProcess::MergedChannels);

    auto* buffer = new QString();
    auto* processorStep = new int(0);

    connect(installer, &QProcess::readyReadStandardOutput, this, [this, installer, buffer, processorStep]() {
        buffer->append(QString::fromLocal8Bit(installer->readAllStandardOutput()));
        buffer->replace("\r\n", "\n"); buffer->replace('\r', '\n');
        if (buffer->size() > 200000) *buffer = buffer->right(4000);

        static int downloadCount = 0;
        int nl;
        while ((nl = buffer->indexOf('\n')) >= 0) {
            QString line = buffer->left(nl).trimmed();
            buffer->remove(0, nl + 1);
            if (line.isEmpty()) continue;
            newDebug() << "+[ForgeInstaller]" << line;

            double p = -1.0;
            QString stage = "Установка Forge";
            QString details = line;

            if (line.contains("Extracting json", Qt::CaseInsensitive)) {
                stage = "Чтение профиля"; p = 0.12;
            } else if (line.contains("Considering minecraft", Qt::CaseInsensitive)) {
                stage = "Проверка Minecraft"; p = 0.18;
            } else if (line.contains("Downloading libraries", Qt::CaseInsensitive)) {
                stage = "Скачивание библиотек";
                details = "Подготовка...";
                p = 0.25;
                downloadCount = 0;
            } else if (line.startsWith("Downloading", Qt::CaseInsensitive) ||
                       line.startsWith("Downloaded", Qt::CaseInsensitive)) {
                QString file = line;
                file.remove(QRegularExpression(R"(^Download(ing|ed):?\s*)"));
                if (file.length() > 80) file = file.left(30) + "..." + file.right(40);
                stage = "Скачивание библиотек";
                details = file;
                downloadCount++;
                p = 0.25 + qMin(0.20, downloadCount * 0.005);
            } else if (line.contains("Downloading asset", Qt::CaseInsensitive)) {
                stage = "Скачивание ресурсов";
                details = "Ассеты Minecraft";
                p = 0.45;
            } else if (line.contains("Running processor", Qt::CaseInsensitive)) {
                (*processorStep)++;
                p = 0.50 + qMin(1.0, *processorStep / 8.0) * 0.40;
                stage = QString("Ремаппинг (%1)").arg(*processorStep);
            } else if (line.contains("Building processor classpath", Qt::CaseInsensitive)) {
                stage = "Финальная сборка"; p = 0.92;
            } else if (line.contains("Patching minecraft", Qt::CaseInsensitive)) {
                stage = "Патчинг Minecraft"; p = 0.95;
            } else if (line.contains("Installation complete", Qt::CaseInsensitive) ||
                       line.contains("You can now play", Qt::CaseInsensitive) ||
                       line.contains("The installation was successful", Qt::CaseInsensitive)) {
                stage = "Готово"; p = 1.0;
            }

            m_pendingStage = "Установка Forge";
            m_pendingDetails = details;
            if (p >= 0.0) m_pendingProgress = p;
            m_hasPendingUpdate = true;
            if (!m_progressThrottle->isActive()) m_progressThrottle->start();
        }
    });

    connect(installer, &QProcess::readyReadStandardError, this, [installer]() {
        QString err = QString::fromLocal8Bit(installer->readAllStandardError());
        if (!err.trimmed().isEmpty()) newDebug() << "-[ForgeInstaller ERROR]" << err.trimmed();
    });

    connect(installer, &QProcess::finished, this, [this, installer, gamePath, installerPath, buffer, processorStep](int exitCode, QProcess::ExitStatus status) {
        delete buffer; delete processorStep;
        installer->deleteLater();

        if (status != QProcess::NormalExit || exitCode != 0) {
            newDebug() << "[Forge] Installer failed, exitCode:" << exitCode;
            emit q->installError("Ошибка установки Forge", QString("Установщик завершился с кодом %1").arg(exitCode), "", true);
            return;
        }

        newDebug() << "[Forge] Installation successful";
        QFile::remove(installerPath);
        finalizeInstall(gamePath);
    });

    installer->start(sm->javaPath(), QStringList() << "-jar" << installerPath << "--installClient" << gamePath);
    if (!installer->waitForStarted(10000)) {
        delete buffer; delete processorStep;
        installer->deleteLater();
        emit q->installError("Ошибка запуска", "Не удалось запустить установщик Forge: " + installer->errorString(), "", false);
    }
}

QString MinecraftHandler::getVersionId() const
{
    if (m_buildLoader == "vanilla") return m_mcVersion;
    if (m_buildLoader == "fabric") return QString("fabric-loader-%1-%2").arg(m_buildVersion, m_mcVersion);
    if (m_buildLoader == "forge") return QString("%1-forge-%2").arg(m_mcVersion, m_buildVersion);
    if (m_buildLoader == "neoforge") return QString("neoforge-%1").arg(m_buildVersion);
    return m_mcVersion;
}

QString MinecraftHandler::getVersionJsonPath(const QString& gamePath) const
{
    const QString id = getVersionId();
    return gamePath + "/versions/" + id + "/" + id + ".json";
}

bool MinecraftHandler::launchMinecraft(const QStringList& command, const QString& gamePath){
    if (m_minecraftProcess && m_minecraftProcess->state() != QProcess::NotRunning) {
        newDebug() << "[MC] Minecraft already running (pid:"
                   << m_minecraftProcess->processId() << "), refusing to start another";
        return false;
    }

    if (command.isEmpty()) {
        newDebug() << "[MC] Empty command, aborting";
        emit q->installError("Ошибка", "Команда запуска пуста", "", false);
        return false;
    }

    QString javaPath = m_launchJavaPath.isEmpty()
                           ? sm->javaPath().trimmed()
                           : m_launchJavaPath.trimmed();
    newDebug() << "[MC] Launching with Java:" << javaPath;
    QProcess* proc = new QProcess(this);
    QString argsFilePath = gamePath + "/launch_args.txt";
    QFile argsFile(argsFilePath);

    if (!argsFile.open(QIODevice::WriteOnly)){
        newDebug() << "ERROR: Ошибка открытия файла аргументов";
        emit q->installError("Ошибка", "Ошибка открытия файла аргументов запуска jvm", "", false);
        return false;
    }

    QByteArray buf;
    for (const QString& arg : command) {
        if (arg.contains(' ')) {
            QString escaped = arg;
            escaped.replace("\"", "\\\"");
            buf += '"' + escaped.toUtf8() + "\"\n";
        } else {
            buf += arg.toUtf8() + "\n";
        }
    }
    argsFile.write(buf);
    argsFile.close();

    javaPath = QDir::toNativeSeparators(javaPath);
    m_minecraftProcess = proc;
    proc->setWorkingDirectory(gamePath);

    connect(proc, &QProcess::readyReadStandardOutput, this, [proc](){
        newDebug() << "[Minecraft Process] DEBUG: " << QString::fromLocal8Bit(proc->readAllStandardOutput());
    });
    connect(proc, &QProcess::readyReadStandardError, this, [proc](){
        newDebug() << "[Minecraft Process] ERROR: " << QString::fromLocal8Bit(proc->readAllStandardError());
    });
    connect(proc, &QProcess::finished, this, [this, proc](int exitCode, QProcess::ExitStatus exitStatus){
        if (exitStatus == QProcess::NormalExit && exitCode == 0)
            newDebug() << "[Minecraft Process] finished normally";
        else
            newDebug() << "[Minecraft Process] exitCode:" << exitCode;
        if (m_minecraftProcess == proc) m_minecraftProcess = nullptr;
        proc->deleteLater();
        emit minecraftStopped();
    });

    QStringList shortCommand;
    shortCommand << "@" + QDir::toNativeSeparators(argsFilePath);
    proc->start(javaPath, shortCommand);

    newDebug() << "[MC] Starting:" << javaPath << "argfile:" << argsFilePath << "cwd:" << gamePath;

    if (!proc->waitForStarted(5000)){
        newDebug() << "[MC] Didn't start:" << proc->errorString();
        emit q->installError("Ошибка", "Майнкрафт не запустился: " + proc->errorString(), "", false);
        return false;
    }

    emit minecraftStarted();
    return true;
}

void MinecraftHandler::setBuildExists(bool v){
    if (m_buildExists == v) return;
    m_buildExists = v;
    emit buildExistsChanged();
}

void MinecraftHandler::setUpdateAvailable(bool v){
    if (m_updateAvailable == v) return;
    m_updateAvailable = v;
    emit updateAvailableChanged();
}

void MinecraftHandler::setInstallation(bool v){
    if (m_installation == v) return;
    m_installation = v;
    emit installationChanged();
}