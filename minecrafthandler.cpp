#include "minecrafthandler.h"
#include "NewDebug.h"

QString getVersionsPath(){
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + "/modpacks";
}

MinecraftHandler::MinecraftHandler(QObject *parent) : QObject(parent), m_manager(new QNetworkAccessManager(this))
{
    m_progressThrottle = new QTimer(this);
    m_progressThrottle->setInterval(100);
    m_progressThrottle->setSingleShot(true);
    connect(m_progressThrottle, &QTimer::timeout, this, [this]() {
        if (!m_hasPendingUpdate) return;
        m_hasPendingUpdate = false;
        emit q->installProgress(m_pendingStage, m_pendingDetails, m_pendingProgress);
    });
}

MinecraftHandler::~MinecraftHandler(){

}

void MinecraftHandler::Initialize(QQuickView* v, QmlHandler* _q, SettingsManager* _sm, SettingsController* _sc){
    view = v;
    q = _q;
    sm = _sm;
    sc = _sc;
}

void MinecraftHandler::ensureLauncherProfile(const QString& gameDir)
{
    QDir().mkpath(gameDir);

    const QString path = gameDir + "/launcher_profiles.json";

    if (QFileInfo::exists(path)) {
        return;
    }

    QJsonObject root;
    root["profiles"] = QJsonObject();
    root["selectedProfile"] = QString();
    root["clientToken"] = QString("8aa19a0b-733b-4153-8098-6214d930881c");
    root["authenticationDatabase"] = QJsonObject();
    root["launcherVersion"] = QJsonObject{
        {"name", "SeaLauncher"},
        {"format", 21},
        {"profilesFormat", 21}
    };

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "[Installer] Не удалось создать launcher_profiles.json в" << path;
        return;
    }

    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    qDebug() << "[Installer] Создан launcher_profiles.json в" << path;
}

void MinecraftHandler::reCheckBuilds(const QString& build, const QString& version, const QString& loader, const QString& mcVer, const QString& git, const int parts){
    newDebug() << "Rechecking build folder: " << build.toStdString().c_str() << " for: " << "Minecraft v" << version << ", " << loader;
    QString path = getVersionsPath();
    m_buildName = build;
    m_buildVersion = version;
    m_buildLoader = loader;
    m_mcVersion = mcVer;
    m_buildArchiveLink = git;
    m_archiveParts = parts;

    QDir().mkpath(path);

    if (!QDir().exists(path + "/" + build))
        m_buildExists = false;
    else
        m_buildExists = true;

    newDebug() << "Does build exist? " << m_buildExists;

    emit buildExistsChanged();
}

void MinecraftHandler::mainButtonClick(){
    newDebug() << "DEBUG: Handling main button click";

    if (!m_buildExists){
        if (m_buildLoader == "neoforge"){
            QString gamePath = sm->gameDir() + "/" + m_buildName;
            newDebug() << "DEBUG: Downloading neoforge for " << m_buildVersion;

            connect(this, &MinecraftHandler::finished, this, [this, gamePath](){
                QStringList command = parseNeoforgeJson(gamePath);

                ensureNarratorDisabled(gamePath);

                sc->closeProgressPanel(view);
                launchMinecraft(command, gamePath);
            });

            downloadNeoforge(m_buildVersion);
        }
    } else {
        if (m_buildLoader == "neoforge"){
            QString gamePath = sm->gameDir() + "/" + m_buildName;
            QStringList command = parseNeoforgeJson(gamePath);

            ensureNarratorDisabled(gamePath);
            installModpack(m_buildArchiveLink, gamePath, m_archiveParts);

            newDebug() << "DEBUG: Starting minecraft on " << m_buildLoader << " " << m_buildVersion;

            launchMinecraft(command, gamePath);
        }
    }
}

QString MinecraftHandler::downloadNeoforge(const QString& version)
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
    + "/downloads";
    QDir().mkpath(dir);

    QString path = dir + QStringLiteral("/neoforge-%1-installer.jar").arg(version);
    QString url  = QStringLiteral(
                      "https://maven.neoforged.net/releases/net/neoforged/neoforge/"
                      "%1/neoforge-%1-installer.jar").arg(version);

    newDebug() << "[NeoForge] Downloading:" << url;
    newDebug() << "[NeoForge] Saving to:" << path;

    emit q->installProgress(
        QStringLiteral("Подготовка загрузки"),
        QStringLiteral("neoforge-%1-installer.jar").arg(version),
        0.0);

    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);

    QFile *file = new QFile(path);
    if (!file->open(QIODevice::WriteOnly)) {
        newDebug() << "[NeoForge] Cannot open:" << path;
        delete file;
        emit q->installError(
            QStringLiteral("Не удалось создать файл"),
            QStringLiteral("Проверьте права на запись в: ") + dir,
            path, false);
        return "";
    }

    QNetworkReply *reply = m_manager->get(req);

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, version](qint64 received, qint64 total) {
                if (total <= 0) {
                    emit q->installProgress(
                        QStringLiteral("Загрузка установщика NeoForge"),
                        QStringLiteral("neoforge-%1-installer.jar").arg(version),
                        -1.0);
                    return;
                }
                double p = double(received) / double(total);
                QString hr = QString("%1 / %2 МБ")
                                 .arg(received / 1048576.0, 0, 'f', 1)
                                 .arg(total    / 1048576.0, 0, 'f', 1);

                emit q->installProgress(
                    QStringLiteral("Загрузка установщика NeoForge"),
                    hr,
                    p);
            });

    connect(reply, &QNetworkReply::readyRead, this,
            [file, reply]() { file->write(reply->readAll()); });

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, file, version, path]() {
                file->close();
                file->deleteLater();

                if (reply->error() != QNetworkReply::NoError) {
                    qWarning() << "[NeoForge] Error:" << reply->errorString();
                    emit q->installError(
                        QStringLiteral("Ошибка скачивания установщика"),
                        reply->errorString(),
                        path, true);
                    reply->deleteLater();
                    return;
                }

                newDebug() << "[NeoForge] Downloaded OK:" << path;

                emit q->installProgress(
                    QStringLiteral("Установщик загружен"),
                    QStringLiteral("Запуск установки..."),
                    1.0);

                emit downloadFinished();
                reply->deleteLater();
                installNeoforge(path);
            });

    return path;
}

QString MinecraftHandler::installNeoforge(const QString& installerPath){
    QString javaPath;
    QStringList arguments;
    QString workingDir;

    javaPath = sm->javaPath();

    if (javaPath.isEmpty()){
        emit q->installError("Ошибка", "Путь к Java не найден", "", false);
        return "";
    }

    emit q->installProgress(
        QStringLiteral("Установка neoforge"),
        "Начало...",
        0.0);

    QProcess *installerProcess = new QProcess(this);

    workingDir = sm->gameDir() + "/" + m_buildName;
    arguments << "-jar" << installerPath << "--installClient" << workingDir;
    QDir().mkpath(workingDir);
    installerProcess->setWorkingDirectory(workingDir);
    installerProcess->setProcessChannelMode(QProcess::MergedChannels);

    ensureLauncherProfile(workingDir);

    auto* buffer = new QString();

    connect(installerProcess, &QProcess::readyReadStandardOutput, this, [this, installerProcess, buffer](){
        buffer->append(QString::fromLocal8Bit(installerProcess->readAllStandardOutput()));

        buffer->replace("\r\n", "\n");
        buffer->replace('\r', '\n');

        if (buffer->size() > 100000) {
            qWarning() << "[NeoForge] Buffer overflow, flushing";
            QString tail = buffer->right(2000);
            buffer->clear();
            *buffer = tail;
        }

        auto pushProgress = [this](const QString& stage, const QString& details, double p) {
            m_pendingStage   = stage;
            m_pendingDetails = details;
            m_pendingProgress = p;
            m_hasPendingUpdate = true;
            if (!m_progressThrottle->isActive())
                m_progressThrottle->start();
        };

        int processorStep = 0;
        int nl;
        while ((nl = buffer->indexOf('\n')) >= 0) {
            QString line = buffer->left(nl).trimmed();
            buffer->remove(0, nl + 1);

            if (line.isEmpty()) continue;
            newDebug() << "+[NeoforgeInstaller] " << line;

            double p = -1;
            QString stage = line;

            if (line.contains("Extracting json", Qt::CaseInsensitive)) {
                stage = "Чтение профиля установки";
                p = 0.05;
            } else if (line.contains("Considering minecraft", Qt::CaseInsensitive)) {
                stage = "Проверка Minecraft";
                p = 0.10;
            } else if (line.contains("Downloading libraries", Qt::CaseInsensitive)) {
                stage = "Скачивание библиотек";
                p = 0.25;
            } else if (line.contains("Downloading asset", Qt::CaseInsensitive)) {
                stage = "Скачивание ресурсов";
                p = 0.40;
            } else if (line.contains("Running processor", Qt::CaseInsensitive)) {
                processorStep++;
                double frac = qMin(1.0, processorStep / double(8));
                p = 0.45 + frac * 0.45;
                stage = QString("Ремаппинг (%1/%2)").arg(processorStep).arg(8);
            } else if (line.contains("Building processor classpath", Qt::CaseInsensitive)) {
                stage = "Финальная сборка";
                p = 0.92;
            } else if (line.contains("Installation complete", Qt::CaseInsensitive)
                       || line.contains("You can now", Qt::CaseInsensitive)) {
                stage = "Готово";
                p = 1.0;
            }

            if (p >= 0.0) {
                pushProgress("Установка NeoForge", stage, p);
            } else {
                pushProgress("Установка NeoForge", line, p);
            }
        }
    });

    connect(installerProcess, &QProcess::readyReadStandardError, this, [installerProcess](){
        QString error = QString::fromLocal8Bit(installerProcess->readAllStandardError());
        newDebug() << "-[NeoforgeInstaller] ERROR: " << error;
    });

    connect(installerProcess, &QProcess::finished, this, [this, installerProcess, workingDir](int exitCode, QProcess::ExitStatus exitStatus){
        if (exitStatus == QProcess::NormalExit && exitCode == 0){
            newDebug() << "[NeoforgeInstaller] Neoforge successfully installed!";

            downloadAssets(workingDir, m_mcVersion);
            downloadVanillaLibraries(workingDir);
            installModpack(m_buildArchiveLink, workingDir, m_archiveParts);

            emit installFinished();
        } else {
            newDebug() << "Ошибка установки neoforge: " << exitCode;
            emit q->installError("Ошибка", QString("Ошибка установки neoforge. Код выхода: %1").arg(exitCode), "", true);
        }

        installerProcess->deleteLater();
    });

    installerProcess->start(javaPath, arguments);

    if (!installerProcess->waitForStarted(10000)){
        emit q->installError("Ошибка запуска", "Не удалось запустить процесс установки.", "", false);
        installerProcess->deleteLater();
    }

    return workingDir;
}

QStringList MinecraftHandler::parseNeoforgeJson(const QString& gamePath)
{
    QString jsonPath = gamePath + "/versions/neoforge-" + m_buildVersion
                       + "/neoforge-" + m_buildVersion + ".json";

    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) {
        newDebug() << "[MC] ERROR: cannot open" << jsonPath;
        emit q->installError("Ошибка", "Не найден JSON NeoForge", jsonPath, false);
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
                        if (os.contains("name") && os["name"].toString() != "windows")
                            matches = false;
                        if (os.contains("arch")) {
                            const QString arch = os["arch"].toString();
                            const bool is64 = QSysInfo::currentCpuArchitecture().contains("64");
                            if ((arch == "x86" && is64) || (arch == "x86_64" && !is64))
                                matches = false;
                        }
                    }
                    if (matches)
                        allowed = (action == "allow");
                }
                if (!allowed) continue;
            }

            QString pathStr;
            if (lib.contains("downloads") && lib["downloads"].toObject().contains("artifact"))
                pathStr = lib["downloads"].toObject()["artifact"].toObject()["path"].toString();

            if (pathStr.isEmpty()) continue;

            QString fullPath = QDir::cleanPath(QDir(librariesDir).absoluteFilePath(pathStr));
            fullPath = QDir::toNativeSeparators(fullPath);
            classpathSet.insert(fullPath);
        }
    };

    QStringList jsonPaths;
    jsonPaths << jsonPath;

    QJsonObject tempJson = json;
    while (tempJson.contains("inheritsFrom")) {
        QString parentVersion = tempJson["inheritsFrom"].toString();
        QString parentPath = gamePath + "/versions/" + parentVersion + "/" + parentVersion + ".json";
        jsonPaths.prepend(parentPath);

        QFile pf(parentPath);
        if (pf.open(QIODevice::ReadOnly)) {
            tempJson = QJsonDocument::fromJson(pf.readAll()).object();
            pf.close();
        } else {
            newDebug() << "[MC] ERROR: cannot open parent json" << parentPath;
            break;
        }
    }

    for (const QString& jPath : jsonPaths) {
        QFile jf(jPath);
        if (jf.open(QIODevice::ReadOnly)) {
            QJsonObject jObj = QJsonDocument::fromJson(jf.readAll()).object();
            processLibraries(jObj);
            jf.close();
        }
    }

    QStringList classpathList = classpathSet.values();
    QString classPath = classpathList.join(';');

    QString cpFilePath = gamePath + "/legacyClassPath.txt";
    {
        QFile cpFile(cpFilePath);
        if (!cpFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            newDebug() << "[MC] ERROR: cannot write classpath file:" << cpFilePath;
            emit q->installError("Ошибка", "Не удалось записать classpath", cpFilePath, false);
            return {};
        }

        for (const QString& entry : classpathList) {
            cpFile.write(entry.toUtf8());
            cpFile.write("\n");
        }
        cpFile.close();
    }

    const QString versionName = QString("%1-neoforge-%2").arg(m_mcVersion, m_buildVersion);

    QHash<QString, QString> vars;
    vars["${auth_player_name}"]       = sm->username();
    vars["${auth_uuid}"]              = "00000000-0000-0000-0000-000000000000";
    vars["${auth_access_token}"]      = "0";
    vars["${auth_session}"]           = "0";
    vars["${user_type}"]              = "legacy";
    vars["${version_name}"]           = versionName;
    vars["${version_type}"]           = "release";
    vars["${game_directory}"]         = gamePath;
    vars["${assets_root}"]            = gamePath + "/assets";
    vars["${assets_index_name}"]      = m_mcVersion;
    vars["${natives_directory}"]      = gamePath + "/natives";
    vars["${library_directory}"]      = librariesDir;
    vars["${classpath}"]              = classPath;
    vars["${launcher_name}"]          = "SeaLauncher";
    vars["${launcher_version}"]       = "1.0";
    vars["${resolution_width}"]       = "925";
    vars["${resolution_height}"]      = "530";
    vars["${game_assets}"]            = gamePath + "/assets";
    vars["${user_properties}"]        = "{}";
    vars["${clientid}"]               = "0";
    vars["${auth_xuid}"]              = "0";
    vars["${classpath_separator}"]    = ";";
    vars["${path_separator}"]         = ";";
    vars["${fml.neoForgeVersion}"]    = m_buildVersion;
    vars["${fml.mcVersion}"]          = m_mcVersion;
    vars["${fml.fmlVersion}"]         = "";

    auto substitute = [&vars](QString s) -> QString {
        for (auto it = vars.constBegin(); it != vars.constEnd(); ++it)
            s.replace(it.key(), it.value());
        return s;
    };

    auto ruleMatches = [](const QJsonObject& rule) -> bool {
        QJsonObject os = rule.value("os").toObject();
        bool matches = true;
        if (os.contains("name") && os["name"].toString().toLower() != "windows")
            matches = false;
        if (os.contains("arch")) {
            const QString arch = os["arch"].toString();
            const bool is64 = QSysInfo::currentCpuArchitecture().contains("64");
            if ((arch == "x86" && is64) || (arch == "x86_64" && !is64))
                matches = false;
        }
        return matches;
    };

    auto expandArray = [&substitute, &ruleMatches](const QJsonArray& arr) -> QStringList {
        QStringList out;
        for (const QJsonValue& v : arr) {
            if (v.isString()) {
                out << substitute(v.toString());
            } else if (v.isObject()) {
                QJsonObject obj = v.toObject();

                bool allowed = true;
                if (obj.contains("rules")) {
                    allowed = false;
                    for (const QJsonValue& rv : obj["rules"].toArray()) {
                        QJsonObject rule = rv.toObject();
                        if (ruleMatches(rule)) {
                            allowed = (rule.value("action").toString() == "allow");
                        }
                    }
                }

                if (allowed && obj.contains("value")) {
                    if (obj["value"].isString()) {
                        out << substitute(obj["value"].toString());
                    } else if (obj["value"].isArray()) {
                        for (const QJsonValue& sub : obj["value"].toArray()) {
                            if (sub.isString()) {
                                out << substitute(sub.toString());
                            }
                        }
                    }
                }
            }
        }
        return out;
    };

    QStringList jvmArgs;
    jvmArgs << "-Xmx" + QString::number(sm->ramMb()) + "M";
    jvmArgs << "-Djava.library.path=" + gamePath + "/natives";
    jvmArgs << "-DlegacyClassPath=" + classPath;

    if (!sm->jvmArgs().trimmed().isEmpty()) {
        jvmArgs << sm->jvmArgs().split(' ', Qt::SkipEmptyParts);
    }

    for (const QString& jPath : jsonPaths) {
        QFile jf(jPath);
        if (jf.open(QIODevice::ReadOnly)) {
            QJsonObject jObj = QJsonDocument::fromJson(jf.readAll()).object();
            QJsonObject args = jObj["arguments"].toObject();
            if (args.contains("jvm")) {
                jvmArgs << expandArray(args["jvm"].toArray());
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
            gameArgs << substitute(jObj["minecraftArguments"].toString())
            .split(' ', Qt::SkipEmptyParts);
            foundGameArgs = true;
            continue;
        }

        QJsonObject args = jObj["arguments"].toObject();
        if (args.contains("game")) {
            gameArgs << expandArray(args["game"].toArray());
            foundGameArgs = true;
        }
    }

    if (!foundGameArgs) {
        gameArgs << "--username"    << sm->username();
        gameArgs << "--version"     << versionName;
        gameArgs << "--gameDir"     << gamePath;
        gameArgs << "--assetsDir"   << gamePath + "/assets";
        gameArgs << "--assetIndex"  << m_mcVersion;
        gameArgs << "--uuid"        << "00000000-0000-0000-0000-000000000000";
        gameArgs << "--accessToken" << "0";
        gameArgs << "--userType"    << "legacy";
    }

    QStringList filteredGameArgs;
    for (const QString& arg : gameArgs) {
        if (!arg.startsWith("--quickPlay")) {
            filteredGameArgs << arg;
        }
    }
    gameArgs = filteredGameArgs;

    gameArgs.removeAll("--demo");

    QString mainClass = json["mainClass"].toString("cpw.mods.bootstraplauncher.BootstrapLauncher");

    QStringList fullCommand;
    fullCommand << jvmArgs;
    fullCommand << mainClass;
    fullCommand << gameArgs;

    newDebug() << "[MC] java:" << sm->javaPath();
    newDebug() << "[MC] classpath entries:" << classpathList.size();
    newDebug() << "[MC] classpath file:" << cpFilePath;
    newDebug() << "[MC] mainClass:" << mainClass;
    newDebug() << "[MC] argv count:" << fullCommand.size();
    newDebug() << "[MC] full command: " << fullCommand.join(' ');

    return fullCommand;
}

void MinecraftHandler::downloadVanillaLibraries(const QString& gamePath)
{
    const QString jsonPath = gamePath + "/versions/" + m_mcVersion + "/" + m_mcVersion + ".json";
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) {
        newDebug() << "[Vanilla] Cannot open" << jsonPath;
        return;
    }

    QJsonObject json = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    const QString librariesDir = gamePath + "/libraries";
    int total = 0;
    int downloaded = 0;

    QNetworkAccessManager manager;
    QEventLoop loop;
    QList<QPair<QString, QString>> queue;

    for (const QJsonValue& v : json["libraries"].toArray()) {
        QJsonObject lib = v.toObject();
        if (!lib.contains("downloads")) continue;
        QJsonObject downloads = lib["downloads"].toObject();
        if (!downloads.contains("artifact")) continue;

        QJsonObject artifact = downloads["artifact"].toObject();
        QString url  = artifact["url"].toString();
        QString path = artifact["path"].toString();
        if (url.isEmpty() || path.isEmpty()) continue;

        QString fullPath = QDir(librariesDir).absoluteFilePath(path);
        if (QFile::exists(fullPath)) continue;

        queue << qMakePair(url, fullPath);
    }

    total = queue.size();
    if (total == 0) {
        newDebug() << "[Vanilla] All libraries already present";
        return;
    }

    newDebug() << "[Vanilla] Downloading " << total << " libraries";

    for (const auto& item : queue) {
        const QString& url = item.first;
        const QString& fullPath = item.second;

        QDir().mkpath(QFileInfo(fullPath).absolutePath());

        QNetworkRequest req{QUrl(url)};
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

        QNetworkReply* reply = manager.get(req);
        QFile* file = new QFile(fullPath);
        if (!file->open(QIODevice::WriteOnly)) {
            delete file;
            reply->deleteLater();
            continue;
        }

        QObject::connect(reply, &QNetworkReply::readyRead, [reply, file]() {
            file->write(reply->readAll());
        });

        QObject::connect(reply, &QNetworkReply::finished, &loop, [&, reply, file]() {
            file->close();
            file->deleteLater();
            reply->deleteLater();

            downloaded++;
            double p = double(downloaded) / double(total) * 0.3;

            emit q->installProgress(
                "Скачивание библиотек Minecraft",
                QString("%1 / %2").arg(downloaded).arg(total),
                p);

            loop.quit();
        });

        loop.exec();
    }

    newDebug() << "[Vanilla] Done, downloaded" << downloaded << "of" << total;
}

void MinecraftHandler::downloadAssets(const QString& gamePath, const QString& assetIndexId)
{
    const QString indexesDir = gamePath + "/assets/indexes";
    const QString objectsDir = gamePath + "/assets/objects";
    QDir().mkpath(indexesDir);
    QDir().mkpath(objectsDir);

    const QString versionJson = gamePath + "/versions/" + m_mcVersion + "/" + m_mcVersion + ".json";
    QFile vf(versionJson);
    if (!vf.open(QIODevice::ReadOnly)) {
        newDebug() << "[Assets] Cannot open" << versionJson;
        return;
    }
    QJsonObject vJson = QJsonDocument::fromJson(vf.readAll()).object();
    vf.close();

    QJsonObject assetIndex = vJson["assetIndex"].toObject();
    const QString idxUrl  = assetIndex["url"].toString();
    const QString idxPath = indexesDir + "/" + assetIndexId + ".json";

    if (!QFile::exists(idxPath)) {
        QNetworkAccessManager mgr;
        QEventLoop loop;
        QNetworkRequest req{QUrl(idxUrl)};
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply* reply = mgr.get(req);
        QFile* file = new QFile(idxPath);
        file->open(QIODevice::WriteOnly);
        QObject::connect(reply, &QNetworkReply::readyRead,
                         [reply, file]() { file->write(reply->readAll()); });
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        file->close();
        file->deleteLater();
        reply->deleteLater();
    }

    QFile inf(idxPath);
    if (!inf.open(QIODevice::ReadOnly)) {
        newDebug() << "[Assets] Cannot read index" << idxPath;
        return;
    }
    QJsonObject index = QJsonDocument::fromJson(inf.readAll()).object();
    inf.close();

    struct Asset { QString url; QString path; };
    QList<Asset> queue;

    QJsonObject objects = index["objects"].toObject();
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QJsonObject obj = it.value().toObject();
        const QString hash = obj["hash"].toString();
        const QString sub  = hash.left(2);
        const QString path = objectsDir + "/" + sub + "/" + hash;

        if (QFile::exists(path) && QFileInfo(path).size() > 0)
            continue;

        queue << Asset{
            "https://resources.download.minecraft.net/" + sub + "/" + hash,
            path
        };
    }

    const int total = queue.size();
    if (total == 0) {
        newDebug() << "[Assets] All present";
        return;
    }

    newDebug() << "[Assets] Downloading" << total << "files in parallel";

    QNetworkAccessManager mgr;
    QEventLoop loop;

    const int PARALLEL = 32;
    int nextIndex = 0;
    int active = 0;
    int done = 0;
    int failed = 0;

    std::function<void()> pump;

    pump = [&]() {
        while (active < PARALLEL && nextIndex < total) {
            const Asset a = queue[nextIndex++];
            active++;

            QDir().mkpath(QFileInfo(a.path).absolutePath());

            QNetworkRequest req{QUrl(a.url)};
            req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);

            QNetworkReply* reply = mgr.get(req);
            QFile* file = new QFile(a.path);
            file->open(QIODevice::WriteOnly);

            QObject::connect(reply, &QNetworkReply::readyRead,
                             [reply, file]() { file->write(reply->readAll()); });

            QObject::connect(reply, &QNetworkReply::finished, &loop,
                             [&, reply, file]() {
                                 file->close();
                                 file->deleteLater();

                                 if (reply->error() != QNetworkReply::NoError) {
                                     failed++;
                                     QFile::remove(file->fileName());
                                 }
                                 reply->deleteLater();

                                 active--;
                                 done++;

                                 if (done == 1 || done % 25 == 0 || done == total) {
                                     emit q->installProgress(
                                         "Загрузка ассетов",
                                         QString("%1 / %2").arg(done).arg(total),
                                         double(done) / total);
                                 }

                                 if (done >= total) {
                                     loop.quit();
                                     return;
                                 }

                                 pump();
                             });
        }
    };

    pump();
    loop.exec();

    newDebug() << "[Assets] Done. Failed:" << failed;
    if (failed > 0) {
        emit q->installError(
            "Ошибка загрузки ассетов",
            QString("Не удалось скачать %1 файлов").arg(failed),
            "", true);
    }
}

void MinecraftHandler::ensureNarratorDisabled(const QString& gamePath)
{
    const QString optionsPath = gamePath + "/options.txt";
    QFile file(optionsPath);

    if (file.exists()) {
        if (file.open(QIODevice::ReadWrite | QIODevice::Text)) {
            QString content = QString::fromUtf8(file.readAll());
            if (!content.contains("narrator:")) {
                content += "\nnarrator:0";
                file.resize(0);
                file.write(content.toUtf8());
            }
            file.close();
        }
        return;
    }

    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write("narrator:0");
        file.close();
    }
}

void MinecraftHandler::downloadFileParallel(const QString& url,
                                            const QString& savePath,
                                            int chunkSizeMB,
                                            int maxParallel)
{
    QNetworkAccessManager* mgr = new QNetworkAccessManager(this);

    QNetworkRequest probeReq{QUrl(url)};
    probeReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                          QNetworkRequest::NoLessSafeRedirectPolicy);
    probeReq.setRawHeader("Range", "bytes=0-0");

    QNetworkReply* probeReply = mgr->get(probeReq);

    connect(probeReply, &QNetworkReply::finished, this,
            [=, this]() {
                probeReply->deleteLater();

                newDebug() << "[DL] probe error:" << probeReply->error()
                           << probeReply->errorString();
                newDebug() << "[DL] HTTP status:"
                           << probeReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                newDebug() << "[DL] Content-Range:"
                           << probeReply->rawHeader("Content-Range");
                newDebug() << "[DL] final URL:" << probeReply->url().toString();

                if (probeReply->error() != QNetworkReply::NoError) {
                    emit q->installError("Ошибка", "Не удалось получить файл: "
                                                       + probeReply->errorString(), url, false);
                    mgr->deleteLater();
                    emit downloadFinished();
                    return;
                }

                QByteArray rangeHeader = probeReply->rawHeader("Content-Range");
                if (rangeHeader.isEmpty()) {
                    newDebug() << "[DL] No Content-Range, fallback to single-stream";
                    downloadSingleStream(url, savePath, mgr);
                    return;
                }

                int slashPos = rangeHeader.indexOf('/');
                if (slashPos < 0) {
                    emit q->installError("Ошибка",
                                         "Странный Content-Range: " + rangeHeader,
                                         url, false);
                    mgr->deleteLater();
                    emit downloadFinished();
                    return;
                }

                qint64 fileSize = rangeHeader.mid(slashPos + 1).toLongLong();
                if (fileSize <= 0) {
                    emit q->installError("Ошибка", "Размер файла 0 или невалидный",
                                         url, false);
                    mgr->deleteLater();
                    emit downloadFinished();
                    return;
                }

                newDebug() << "[DL] size:" << fileSize << "accept-ranges: yes";

                const qint64 chunkSize = qint64(chunkSizeMB) * 1024 * 1024;

                QFile file(savePath);
                if (!file.open(QIODevice::ReadWrite)) {
                    emit q->installError("Ошибка",
                                         "Не удалось создать файл: " + savePath
                                             + " (" + file.errorString() + ")",
                                         savePath, false);
                    mgr->deleteLater();
                    emit downloadFinished();
                    return;
                }
                file.resize(fileSize);
                file.close();

                QList<Chunk> chunks;
                for (qint64 pos = 0; pos < fileSize; pos += chunkSize) {
                    qint64 end = qMin(pos + chunkSize - 1, fileSize - 1);
                    chunks << Chunk{pos, end};
                }

                newDebug() << "[DL] chunks:" << chunks.size()
                           << "parallel:" << maxParallel;

                downloadChunks(mgr, url, savePath, chunks, maxParallel);
            });
}

void MinecraftHandler::downloadSingleStream(const QString& url,
                                            const QString& savePath,
                                            QNetworkAccessManager* mgr)
{
    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = mgr->get(req);
    QFile* file = new QFile(savePath);
    file->open(QIODevice::WriteOnly);

    QElapsedTimer timer;
    timer.start();
    qint64 lastBytes = 0;

    connect(reply, &QNetworkReply::readyRead, this,
            [reply, file]() { file->write(reply->readAll()); });

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, &timer, &lastBytes](qint64 recv, qint64 total) {
                if (timer.elapsed() < 200) return;
                double speed = (recv - lastBytes) / 1048576.0 /
                               (timer.elapsed() / 1000.0);
                lastBytes = recv;
                timer.restart();

                if (total > 0) {
                    emit q->installProgress(
                        "Загрузка сборки",
                        QString("%1 / %2 МБ (%3 МБ/с)")
                            .arg(recv / 1048576.0, 0, 'f', 1)
                            .arg(total / 1048576.0, 0, 'f', 1)
                            .arg(speed, 0, 'f', 1),
                        double(recv) / double(total));
                }
            });

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, file, mgr]() {
                file->close();
                file->deleteLater();
                reply->deleteLater();
                mgr->deleteLater();

                if (reply->error() != QNetworkReply::NoError) {
                    emit q->installError("Ошибка загрузки",
                                         reply->errorString(), "", true);
                }
                emit downloadFinished();
            });
}

void MinecraftHandler::downloadChunks(QNetworkAccessManager* mgr,
                                      const QString& url,
                                      const QString& savePath,
                                      const QList<Chunk>& chunks,
                                      int maxParallel)
{
    auto* file = new QFile(savePath);
    file->open(QIODevice::ReadWrite);

    auto* totalBytes = new qint64(0);
    for (const Chunk& c : chunks)
        *totalBytes += (c.end - c.start + 1);

    auto* downloadedBytes = new qint64(0);
    auto* nextIndex = new int(0);
    auto* active = new int(0);
    auto* done = new int(0);
    auto* failed = new int(0);
    auto* loop = new QEventLoop(this);
    auto* timer = new QElapsedTimer();
    timer->start();

    qint64* lastBytes = new qint64(0);

    auto pump = std::make_shared<std::function<void()>>();

    *pump = [=, this]() {
        while (*active < maxParallel && *nextIndex < chunks.size()) {
            const Chunk c = chunks[(*nextIndex)++];
            (*active)++;

            QNetworkRequest req{QUrl(url)};
            req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
            req.setRawHeader("Range",
                             QString("bytes=%1-%2").arg(c.start).arg(c.end).toUtf8());

            QNetworkReply* reply = mgr->get(req);
            auto* buf = new QByteArray();
            buf->reserve(int(c.end - c.start + 1));

            QObject::connect(reply, &QNetworkReply::readyRead, this,
                             [reply, buf, downloadedBytes, timer, lastBytes,
                              totalBytes, this]() {
                                 QByteArray data = reply->readAll();
                                 *buf += data;
                                 *downloadedBytes += data.size();

                                 if (timer->elapsed() > 200) {
                                     double speed = (*downloadedBytes - *lastBytes) / 1048576.0 /
                                                    (timer->elapsed() / 1000.0);
                                     *lastBytes = *downloadedBytes;
                                     timer->restart();

                                     emit q->installProgress(
                                         "Загрузка сборки",
                                         QString("%1 / %2 МБ (%3 МБ/с)")
                                             .arg(*downloadedBytes / 1048576.0, 0, 'f', 1)
                                             .arg(*totalBytes / 1048576.0, 0, 'f', 1)
                                             .arg(speed, 0, 'f', 1),
                                         double(*downloadedBytes) / double(*totalBytes));
                                 }
                             });

            QObject::connect(reply, &QNetworkReply::finished, this,
                             [=, this]() {
                                 reply->deleteLater();

                                 bool ok = reply->error() == QNetworkReply::NoError
                                           && buf->size() == (c.end - c.start + 1);

                                 if (ok) {
                                     file->seek(c.start);
                                     file->write(*buf);
                                 } else {
                                     (*failed)++;
                                     newDebug() << "[Download] chunk failed:"
                                                << c.start << "-" << c.end
                                                << "err:" << reply->errorString();
                                 }

                                 delete buf;
                                 (*active)--;
                                 (*done)++;

                                 if (*done >= chunks.size()) {
                                     file->close();
                                     delete file;
                                     delete totalBytes;
                                     delete downloadedBytes;
                                     delete nextIndex;
                                     delete active;
                                     delete done;
                                     int failCount = *failed;
                                     delete failed;
                                     delete timer;
                                     delete lastBytes;
                                     loop->quit();
                                     loop->deleteLater();

                                     mgr->deleteLater();

                                     if (failCount > 0) {
                                         emit q->installError("Ошибка загрузки",
                                                              QString("Не удалось скачать %1 чанков")
                                                                  .arg(failCount), "", true);
                                     }
                                     emit downloadFinished();
                                     return;
                                 }

                                 (*pump)();
                             });
        }
    };

    (*pump)();
    loop->exec();
}

bool MinecraftHandler::mergeFiles(const QStringList& parts,
                                  const QString& outputPath)
{
    QFile out(outputPath);
    if (!out.open(QIODevice::WriteOnly)) {
        newDebug() << "[Merge] Cannot open output:" << outputPath;
        return false;
    }

    for (const QString& part : parts) {
        QFile in(part);
        if (!in.open(QIODevice::ReadOnly)) {
            newDebug() << "[Merge] Cannot open part:" << part;
            out.close();
            return false;
        }

        const qint64 totalSize = in.size();
        qint64 copied = 0;
        const qint64 bufSize = 4 * 1024 * 1024;

        while (!in.atEnd()) {
            QByteArray buf = in.read(bufSize);
            out.write(buf);
            copied += buf.size();

            emit q->installProgress(
                "Склейка частей",
                QString("%1: %2 / %3 МБ")
                    .arg(QFileInfo(part).fileName())
                    .arg(copied / 1048576.0, 0, 'f', 1)
                    .arg(totalSize / 1048576.0, 0, 'f', 1),
                double(copied) / double(totalSize));
        }
        in.close();
    }

    out.close();
    return true;
}

bool MinecraftHandler::extractZip(const QString& zipPath,
                                  const QString& targetDir)
{
    QZipReader zip(zipPath);
    if (!zip.isReadable()) {
        newDebug() << "[Zip] Not readable:" << zipPath;
        return false;
    }

    QDir().mkpath(targetDir);

    const auto files = zip.fileInfoList();
    const int total = files.size();
    int done = 0;

    for (const QZipReader::FileInfo& fi : files) {
        const QString outPath = targetDir + "/" + fi.filePath;

        if (fi.isDir) {
            QDir().mkpath(outPath);
        } else {
            QDir().mkpath(QFileInfo(outPath).absolutePath());
            QFile out(outPath);
            if (out.open(QIODevice::WriteOnly)) {
                out.write(zip.fileData(fi.filePath));
                out.close();
            }
        }

        done++;
        if (done % 50 == 0 || done == total) {
            emit q->installProgress(
                "Распаковка сборки",
                QString("%1 / %2").arg(done).arg(total),
                double(done) / double(total));
        }
    }

    zip.close();
    return true;
}

void MinecraftHandler::installModpack(const QString& baseUrl,
                                      const QString& gamePath,
                                      int partCount)
{
    QDir().mkpath(gamePath);

    QStringList partPaths;
    QStringList partUrls;

    if (partCount <= 1) {
        partPaths << gamePath + "/modpack.zip";
        partUrls  << baseUrl;
    } else {
        for (int i = 1; i <= partCount; i++) {
            partPaths << QString("%1/modpack.part%2").arg(gamePath).arg(i);
            partUrls  << QString("%1/modpack.part%2").arg(baseUrl).arg(i);
        }
    }

    auto* index = new int(0);

    auto downloadNext = std::make_shared<std::function<void()>>();
    *downloadNext = [=, this]() {
        if (*index >= partUrls.size()) {
            delete index;
            onAllPartsDownloaded(partPaths, gamePath, partCount);
            return;
        }

        int i = (*index)++;
        newDebug() << "[Modpack] Downloading part" << (i + 1)
                   << "of" << partUrls.size();

        connect(this, &MinecraftHandler::downloadFinished, this,
                [=]() {
                    QMetaObject::invokeMethod(this, [=]() {
                        disconnect(this, &MinecraftHandler::downloadFinished,
                                   nullptr, nullptr);
                        (*downloadNext)();
                    }, Qt::QueuedConnection);
                },
                Qt::SingleShotConnection);

        emit q->installProgress(
            "Загрузка сборки",
            QString("Часть %1 из %2").arg(i + 1).arg(partUrls.size()),
            0.0);

        downloadFileParallel(partUrls[i], partPaths[i]);
    };

    (*downloadNext)();
}

void MinecraftHandler::onAllPartsDownloaded(const QStringList& partPaths,
                                            const QString& gamePath,
                                            int partCount)
{
    QString finalZip = gamePath + "/modpack.zip";

    newDebug() << "[Modpack] gamePath:" << gamePath;
    newDebug() << "[Modpack] partCount:" << partCount;

    for (const QString& p : partPaths) {
        QFileInfo fi(p);
        newDebug() << "[Modpack] part:" << p
                   << "exists:" << fi.exists()
                   << "size:" << fi.size();
    }

    auto doExtract = [=, this]() {
        emit q->installProgress("Распаковка сборки", "Подготовка...", 0.0);

        if (extractZip(finalZip, gamePath)) {
            QFile::remove(finalZip);
            for (const QString& p : partPaths)
                QFile::remove(p);
            emit q->installProgress("Готово", "Сборка установлена", 1.0);
            emit installFinished();
            emit finished();
        } else {
            emit q->installError("Ошибка",
                                 "Не удалось распаковать архив",
                                 finalZip, true);
        }
    };

    if (partCount > 1) {
        emit q->installProgress("Склейка частей", "Начало...", 0.0);

        if (!mergeFiles(partPaths, finalZip)) {
            emit q->installError("Ошибка",
                                 "Не удалось склеить части архива",
                                 "", true);
            return;
        }
        doExtract();
    } else {
        doExtract();
    }
}

bool MinecraftHandler::launchMinecraft(const QStringList& command, const QString& gamePath){
    QString javaPath = sm->javaPath().trimmed();
    QProcess* proc = new QProcess(this);
    QString argsFilePath = gamePath + "/launch_args.txt";
    QFile argsFile(argsFilePath);

    if (!argsFile.open(QIODevice::WriteOnly)){
        newDebug() << "ERROR: Ошибка открытия файла аргументов запуска jvm";
        emit q->installError("Ошибка", "Ошибка открытия файла аргументов запуска jvm", "", false);
        return 0;
    }

    QTextStream out(&argsFile);
    out.setEncoding(QStringConverter::Utf8);

    for (const QString& arg : command) {
        if (arg.contains(' ')) {
            QString escaped = arg;
            escaped.replace("\"", "\\\"");
            out << "\"" << escaped << "\"\n";
        } else {
            out << arg << "\n";
        }
    }
    argsFile.close();

    javaPath = QDir::toNativeSeparators(javaPath);
    m_minecraftProcess = proc;

    proc->setWorkingDirectory(gamePath);

    connect(proc, &QProcess::readyReadStandardOutput, this, [proc](){
        QString output = QString::fromLocal8Bit(proc->readAllStandardOutput());

        newDebug() << "[Minecraft Process] DEBUG: " << output;
    });

    connect(proc, &QProcess::readyReadStandardError, this, [proc](){
        QString error = QString::fromLocal8Bit(proc->readAllStandardError());

        newDebug() << "[Minecraft Process] ERROR: " << error;
    });

    connect(proc, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus){
       if (exitStatus == QProcess::NormalExit && exitCode == 0){
           newDebug() << "[Minecraft Process] Process finished normally";
       } else {
           newDebug() << QString("[Minecraft Process] ERROR: Process finished with error, exitCode: %1").arg(exitCode);

           emit q->installError("Ошибка", QString("Майнкрафт завершился с ошибкой: %1").arg(exitCode), "", false);
       }
    });

    QStringList shortCommand;
    shortCommand << "@" + QDir::toNativeSeparators(argsFilePath);

    proc->start(javaPath, shortCommand);

    newDebug() << "[Minecraft Process] Starting Java: " << javaPath;
    newDebug() << "[Minecraft Process] Using argfile: " << argsFilePath;
    newDebug() << "[Minecraft Process] Working Dir: " << gamePath;
    newDebug() << "[Minecraft Process] Short Command: " << shortCommand.join(' ');

    if (!proc->waitForStarted(5000)){
        newDebug() << "[Minecraft Process] ERROR: Didn't start. Probably command error: " << proc->errorString();
        emit q->installError("Ошибка", "Майнкрафт не запустился, скорее всего ошибка в аргументах запуска.", proc->errorString(), false);
        return false;
    }

    return true;
}

void MinecraftHandler::setBuildExists(bool v){
    if (m_buildExists == v) return;
    m_buildExists = v;
    emit buildExistsChanged();
}


