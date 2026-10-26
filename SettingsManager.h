#pragma once

#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#define VERSION 1

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QPair>
#include <QHash>
#include <QTimer>
#include <QVariantList>
#include <QDirIterator>

class SettingsManager: public QObject {
    Q_OBJECT

    Q_PROPERTY(QString username READ username WRITE setUsername NOTIFY usernameChanged)
    Q_PROPERTY(int     ramMb    READ ramMb    WRITE setRamMb    NOTIFY ramMbChanged)
    Q_PROPERTY(QString javaPath READ javaPath WRITE setJavaPath NOTIFY javaPathChanged)
    Q_PROPERTY(QString gameDir  READ gameDir  WRITE setGameDir  NOTIFY gameDirChanged)
    Q_PROPERTY(QString jvmArgs  READ jvmArgs  WRITE setJvmArgs  NOTIFY jvmArgsChanged)
    Q_PROPERTY(int     version    READ version    WRITE setVersion    NOTIFY versionChanged)
    Q_PROPERTY(int     maxRam    READ maxRam    WRITE setMaxRam    NOTIFY maxRamChanged)
    Q_PROPERTY(QString lastBuild READ lastBuild WRITE setLastBuild NOTIFY lastBuildChanged)

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
    QString lastBuild() const { return m_lastBuild; }

    void setLastBuild(const QString &v);
    void setUsername(const QString &v);
    void setRamMb(int v);
    void setJavaPath(const QString &v);
    void setGameDir(const QString &v);
    void setJvmArgs(const QString &v);
    void setVersion(int v);
    void setMaxRam(int v);

    void ensureLatestJava();
    QList<QPair<QString, int>> getAllJavaInstallations() const;
    QString findJavaByMajor(int major) const;
    static bool isValidJavaExecutable(const QString& path);
    int getJavaMajorVersion(const QString& path) const;
    void invalidateJavaCache();

    Q_INVOKABLE QString configFilePath() const;
    Q_INVOKABLE int javaMajorOf(const QString& path) const;
    Q_INVOKABLE QVariantList detectedJavas() const;
    Q_INVOKABLE void rescanJava();

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
    void lastBuildChanged();
    void javaListChanged();

private:
    void scheduleSave();
    void writeTemplateFile();
    QString detectJavaPath() const;
    static int majorFromReleaseFile(const QString& javaExePath);
    static void collectJavaExecutables(const QString& root, int depth, QStringList& out);

    QString m_username;
    int     m_ramMb    = 4096;
    QString m_javaPath;
    QString m_gameDir;
    QString m_jvmArgs = "-XX:+UseG1GC -XX:+UnlockExperimentalVMOptions -XX:MaxGCPauseMillis=100 -XX:+PerfDisableSharedMem -XX:G1ReservePercent=20 -XX:G1HeapRegionSize=16M";
    int m_version = 1;
    int m_maxRam = 4096;
    QString m_lastBuild;

    bool m_loading = false;
    QTimer m_saveTimer;

    mutable QList<QPair<QString, int>> m_javaCache;
    mutable bool m_javaCacheValid = false;
    mutable QHash<QString, int> m_majorCache;
};

#endif