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

    // Если файл уже есть — ничего не делаем
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

void MinecraftHandler::reCheckBuilds(const QString& build, const QString& version, const QString& loader, const QString& mcVer){
    newDebug() << "Rechecking build folder: " << build.toStdString().c_str() << " for: " << "Minecraft v" << version << ", " << loader;
    QString path = getVersionsPath();
    m_buildName = build;
    m_buildVersion = version;
    m_buildLoader = loader;
    m_mcVersion = mcVer;

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

            connect(this, &MinecraftHandler::installFinished, this, [this, gamePath](){
                QStringList command = parseNeoforgeJson(gamePath);

                sc->closeProgressPanel(view);
                launchMinecraft(command);
            });

            downloadNeoforge(m_buildVersion);
        }
    } else {
        if (m_buildLoader == "neoforge"){
            QString gamePath = sm->gameDir() + "/" + m_buildName;
            QStringList command = parseNeoforgeJson(gamePath);

            newDebug() << "DEBUG: Starting minecraft on " << m_buildLoader << " " << m_buildVersion;

            launchMinecraft(command);
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
            emit q->installProgress(
                QStringLiteral("Установка neoforge завершена!"),
                "",
                1.0);

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

    QStringList classpathList;
    const QString librariesDir = gamePath + "/libraries";

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
                    if (os.contains("name") && os["name"].toString() != "windows")
                        matches = false;
                    if (os.contains("arch")) {
                        const QString arch = os["arch"].toString();
                        const bool is64 = QSysInfo::currentCpuArchitecture().contains("64");
                        if ((arch == "x86" && is64) || (arch == "x86_64" && !is64))
                            matches = false;
                    }
                }
                if (matches) {
                    allowed = (action == "allow");
                }
            }
            if (!allowed) continue;
        }

        if (lib.contains("downloads")
            && lib["downloads"].toObject().contains("artifact")) {
            QString p = lib["downloads"].toObject()["artifact"]
                             .toObject()["path"].toString();
            if (!p.isEmpty())
                classpathList << QDir(librariesDir).absoluteFilePath(p);
        }
    }

    QString mcJar = gamePath + "/versions/" + m_mcVersion
                  + "/" + m_mcVersion + ".jar";
    classpathList << mcJar;

    QString classPath = classpathList.join(';');

    QString cpFilePath = gamePath + "/legacyClassPath.txt";
    {
        QFile cpFile(cpFilePath);
        if (!cpFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            newDebug() << "[MC] ERROR: cannot write classpath file:" << cpFilePath;
            emit q->installError("Ошибка", "Не удалось записать classpath", cpFilePath, false);
            return {};
        }
        cpFile.write(classPath.toUtf8());
        cpFile.close();
    }

    const QString versionName = QString("%1-neoforge-%2")
                                    .arg(m_mcVersion, m_buildVersion);

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
        if (os.contains("name") && os["name"].toString() != "windows")
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
                QString value = obj.value("value").toString();
                if (value.isEmpty() && obj.value("value").isArray()) {
                    for (const QJsonValue& sub : obj["value"].toArray())
                        out << substitute(sub.toString());
                    continue;
                }
                bool allowed = true;
                if (obj.contains("rules")) {
                    allowed = false;
                    for (const QJsonValue& rv : obj["rules"].toArray()) {
                        QJsonObject rule = rv.toObject();
                        if (ruleMatches(rule))
                            allowed = (rule.value("action").toString() == "allow");
                    }
                }
                if (allowed && !value.isEmpty())
                    out << substitute(value);
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

    QJsonObject arguments = json["arguments"].toObject();
    if (arguments.contains("jvm")) {
        jvmArgs << expandArray(arguments["jvm"].toArray());
    }

    QStringList gameArgs;
    if (arguments.contains("game")) {
        gameArgs << expandArray(arguments["game"].toArray());
    } else if (json.contains("minecraftArguments")) {
        gameArgs << substitute(json["minecraftArguments"].toString())
                        .split(' ', Qt::SkipEmptyParts);
    } else {
        gameArgs << "--username"    << sm->username();
        gameArgs << "--version"     << versionName;
        gameArgs << "--gameDir"     << gamePath;
        gameArgs << "--assetsDir"   << gamePath + "/assets";
        gameArgs << "--assetIndex"  << m_mcVersion;
        gameArgs << "--uuid"        << "00000000-0000-0000-0000-000000000000";
        gameArgs << "--accessToken" << "0";
        gameArgs << "--userType"    << "legacy";
    }

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

    return fullCommand;
}

bool MinecraftHandler::launchMinecraft(const QStringList& command){
    QString javaPath = sm->javaPath();
    QProcess* proc = new QProcess(this);

    m_minecraftProcess = proc;

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

    proc->start(javaPath, command);

    if (!proc->waitForStarted(5000)){
        newDebug() << "[Minecraft Process] ERROR: Didn't start. Probably command error";
        emit q->installError("Ошибка", "Майнкрафт не запустился, скорее всего ошибка в аргументах запуска.", "", false);
        return false;
    }

    return true;
}

void MinecraftHandler::setBuildExists(bool v){
    if (m_buildExists == v) return;
    m_buildExists = v;
    emit buildExistsChanged();
}


