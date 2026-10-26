#pragma once

#ifndef MODRINTHAPI_H
#define MODRINTHAPI_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <functional>
#include <memory>

class QNetworkReply;

class ModrinthApi : public QObject
{
    Q_OBJECT

public:
    using ErrorCb = std::function<void(const QString&)>;

    explicit ModrinthApi(QObject *parent = nullptr);

    Q_INVOKABLE void searchMods(const QString& query, const QString& gameVersion = QString(), const QString& loader = QString());
    Q_INVOKABLE void getProjectVersions(const QString& projectId, const QString& gameVersion = QString(), const QString& loader = QString());
    Q_INVOKABLE void downloadFile(const QString& url, const QString& savePath);

    void requestProject(const QString& projectId, std::function<void(const QJsonObject&)> cb,
                        ErrorCb onError = nullptr, QObject* context = nullptr);
    void requestVersions(const QString& projectId, const QString& gameVersion, const QString& loader,
                         std::function<void(const QJsonArray&)> cb, ErrorCb onError = nullptr, QObject* context = nullptr);
    void requestVersion(const QString& versionId, std::function<void(const QJsonObject&)> cb,
                        ErrorCb onError = nullptr, QObject* context = nullptr);

    static QString getApiBaseUrl() { return QStringLiteral("https://api.modrinth.com/v2"); }

signals:
    void searchCompleted(const QJsonArray& projects);
    void searchFailed(const QString& message);
    void versionsReady(const QJsonArray& versions);
    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(const QString& path);
    void error(const QString& message);

private:
    struct Req {
        QString endpoint;
        int attempt = 0;
        QPointer<QObject> context;
        bool hasContext = false;
        std::function<void(const QJsonDocument&)> onOk;
        ErrorCb onError;
    };

    QNetworkRequest createRequest(const QString& endpoint) const;
    void sendGet(const std::shared_ptr<Req>& req);
    void getJson(const QString& endpoint, QObject* context,
                 std::function<void(const QJsonDocument&)> onOk, ErrorCb onError);

    QNetworkAccessManager* m_manager;
    quint64 m_searchSeq = 0;
};

#endif