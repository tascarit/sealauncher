#include "minecrafthandler.h"
#include "JsonUtilities.h"
#include "NewDebug.h"

QString getVersionsPath(){
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + "/versions";
}

MinecraftHandler::MinecraftHandler(QObject *parent) : QObject(parent)
{

}

void MinecraftHandler::Initialize(QQuickView* v){
    view = v;
}

void MinecraftHandler::reCheckBuilds(const QString& build){
    newDebug() << "Rechecking build folder: " << build.toStdString().c_str();
    QString path = getVersionsPath();
    m_buildName = build;

    QDir().mkpath(path);

    if (!QDir().exists(path + build))
        m_buildExists = false;
    else
        m_buildExists = true;

    newDebug() << "Does build exist? " << m_buildExists;

    emit buildExistsChanged();
}

void MinecraftHandler::mainButtonClick(){
    if (!m_buildExists){
        QString path = getVersionsPath();
        QNetworkAccessManager networkManager;
        QNetworkReply *reply;
        QJsonObject minecraftManifest;
        QEventLoop loop;

        QDir().mkpath(path + "/" + m_buildName);

        reply = networkManager.get(QNetworkRequest(QUrl("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json")));
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        minecraftManifest = QJsonDocument::fromJson(reply->readAll()).object();

        printJsonObject(minecraftManifest, QString("Minecraft Manifest"));
    }
}

void MinecraftHandler::setBuildExists(bool v){
    if (m_buildExists == v) return;
    m_buildExists = v;
    emit buildExistsChanged();
}


