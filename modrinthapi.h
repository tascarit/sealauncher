#pragma once

#ifndef MODRINTHAPI_H
#define MODRINTHAPI_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>

class ModrinthApi : public QObject
{
    Q_OBJECT

public:
    explicit ModrinthApi(QObject *parent = nullptr);

    Q_INVOKABLE void searchMods(const QString& query, const QString& gameVersion = QString(), const QString& loader = QString());
    Q_INVOKABLE void getProjectVersions(const QString& projectId, const QString& gameVersion = QString(), const QString& loader = QString());
    Q_INVOKABLE void downloadFile(const QString& url, const QString& savePath);

    void requestProject(const QString& projectId, std::function<void(const QJsonObject&)> cb, QObject* context = nullptr);
    void requestVersions(const QString& projectId, const QString& gameVersion, const QString& loader, std::function<void(const QJsonArray&)> cb, QObject* context = nullptr);
    void requestVersion(const QString& versionId, std::function<void(const QJsonObject&)> cb, QObject* context = nullptr);

    static QString getApiBaseUrl() { return QStringLiteral("https://api.modrinth.com/v2"); }

signals:
    void searchCompleted(const QJsonArray& projects);
    void versionsReady(const QJsonArray& versions);
    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(const QString& path);
    void error(const QString& message);

private:
    QNetworkAccessManager* m_manager;

    QNetworkRequest createRequest(const QString& endpoint);
    void handleResponse(QNetworkReply* reply, QObject* context, std::function<void(const QJsonDocument&)> callback);
};

#endif
