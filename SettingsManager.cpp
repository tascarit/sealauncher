#include "SettingsManager.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

namespace fs = std::filesystem;

SettingsManager::SettingsManager(QObject *parent)
    :QObject(parent)
{
    load();
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

    QDir().mkpath(dir);

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "Debug: No config file was found, using standard values";
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

    if (m_ramMb < 2048) m_ramMb = 2048;

    qDebug() << "Debug: Settings loaded from: " << path;
    m_loading = false;

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

    QFile f(path);

    if (!f.open(QIODevice::WriteOnly)){
        qWarning() << "ERROR: Failed to open file to save settings";
        return;
    }

    f.write(QJsonDocument(o).toJson());
    qDebug() << "DEBUG: Settings file saved to: " << path;
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