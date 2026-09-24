#pragma once

#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H
#define VERSION 1

#endif // SETTINGSMANAGER_H


#include <QObject>
#include <QString>

class SettingsManager: public QObject {
    Q_OBJECT

    Q_PROPERTY(QString username READ username WRITE setUsername NOTIFY usernameChanged)
    Q_PROPERTY(int     ramMb    READ ramMb    WRITE setRamMb    NOTIFY ramMbChanged)
    Q_PROPERTY(QString javaPath READ javaPath WRITE setJavaPath NOTIFY javaPathChanged)
    Q_PROPERTY(QString gameDir  READ gameDir  WRITE setGameDir  NOTIFY gameDirChanged)
    Q_PROPERTY(QString jvmArgs  READ jvmArgs  WRITE setJvmArgs  NOTIFY jvmArgsChanged)
    Q_PROPERTY(int     version    READ version    WRITE setVersion    NOTIFY versionChanged)
    Q_PROPERTY(int     maxRam    READ maxRam    WRITE setMaxRam    NOTIFY maxRamChanged)

public:
    explicit SettingsManager(QObject *parent = nullptr);
    ~SettingsManager() override;

    QString username() const { return m_username; }
    int     ramMb()    const { return m_ramMb; }
    QString javaPath() const { return m_javaPath; }
    QString gameDir()  const { return m_gameDir; }
    QString jvmArgs()  const { return m_jvmArgs; }
    int version() const {return m_version;}
    int     maxRam()    const { return m_maxRam; }

    void setUsername(const QString &v);
    void setRamMb(int v);
    void setJavaPath(const QString &v);
    void setGameDir(const QString &v);
    void setJvmArgs(const QString &v);
    void setVersion(int v);
    void setMaxRam(int v);

    void CreateTemplateFile();

    Q_INVOKABLE QString configFilePath() const;
public slots:
    void load();
    void save();

signals:
    void usernameChanged();
    void ramMbChanged();
    void javaPathChanged();
    void gameDirChanged();
    void jvmArgsChanged();
    void versionChanged();
    void maxRamChanged();

private:
    QString m_username;
    int     m_ramMb    = 4096;
    QString m_javaPath;
    QString m_gameDir;
    QString m_jvmArgs;
    int m_version;
    int m_maxRam;

    bool m_loading = false;
};
