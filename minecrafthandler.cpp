#include "minecrafthandler.h"
#include "JsonUtilities.h"
#include "NewDebug.h"

#include <QProcess>

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

void MinecraftHandler::Initialize(QQuickView* v, QmlHandler* _q, SettingsManager* _sm){
    view = v;
    q = _q;
    sm = _sm;
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

void MinecraftHandler::reCheckBuilds(const QString& build, const QString& version, const QString& loader){
    newDebug() << "Rechecking build folder: " << build.toStdString().c_str() << " for: " << "Minecraft v" << version << ", " << loader;
    QString path = getVersionsPath();
    m_buildName = build;
    m_buildVersion = version;
    m_buildLoader = loader;

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
            newDebug() << "DEBUG: Downloading neoforge for " << m_buildVersion;

            downloadNeoforge(m_buildVersion);
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
            parseNeoforgeJson(workingDir);
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

QString MinecraftHandler::parseNeoforgeJson(const QString& gamePath){

}

void MinecraftHandler::setBuildExists(bool v){
    if (m_buildExists == v) return;
    m_buildExists = v;
    emit buildExistsChanged();
}


