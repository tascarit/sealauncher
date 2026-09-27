#include "modrinthapi.h"
#include <QNetworkReply>
#include <QJsonDocument>
#include <QFile>
#include <QUrlQuery>

ModrinthApi::ModrinthApi(QObject *parent)
    : QObject(parent), m_manager(new QNetworkAccessManager(this))
{
}

QNetworkRequest ModrinthApi::createRequest(const QString& endpoint)
{
    QNetworkRequest request(QUrl(getApiBaseUrl() + endpoint));
    request.setRawHeader("User-Agent", "SeaLauncher/1.0");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

void ModrinthApi::handleResponse(QNetworkReply* reply, std::function<void(const QJsonDocument&)> callback)
{
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit error(reply->errorString());
            reply->deleteLater();
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isNull()) emit error("Invalid JSON response");
        else callback(doc);
        reply->deleteLater();
    });
}

void ModrinthApi::searchMods(const QString& query,
                             const QString& gameVersion,
                             const QString& loader)
{
    QUrlQuery params;
    params.addQueryItem("query", query);
    params.addQueryItem("limit", "50");
    params.addQueryItem("index", "relevance");

    QStringList facets;

    if (!gameVersion.isEmpty())
        facets << QString("[\"versions:%1\"]").arg(gameVersion);

    if (!loader.isEmpty())
        facets << QString("[\"categories:%1\"]").arg(loader);

    facets << "[\"project_type:mod\"]";

    params.addQueryItem("facets", "[" + facets.join(",") + "]");

    QNetworkReply* reply =
        m_manager->get(createRequest("/search?" + params.toString()));

    handleResponse(reply, [this](const QJsonDocument& doc) {
        emit searchCompleted(doc.object()["hits"].toArray());
    });
}

void ModrinthApi::getProjectVersions(const QString& projectId, const QString& gameVersion, const QString& loader)
{
    requestVersions(projectId, gameVersion, loader, [this](const QJsonArray& arr) {
        emit versionsReady(arr);
    });
}

void ModrinthApi::requestProject(const QString& projectId, std::function<void(const QJsonObject&)> cb)
{
    QNetworkReply* reply = m_manager->get(createRequest("/project/" + projectId));
    handleResponse(reply, [cb](const QJsonDocument& doc) { cb(doc.object()); });
}

void ModrinthApi::requestVersions(const QString& projectId, const QString& gameVersion, const QString& loader, std::function<void(const QJsonArray&)> cb)
{
    QUrlQuery params;
    if (!gameVersion.isEmpty()) params.addQueryItem("game_versions", QString("[\"%1\"]").arg(gameVersion));
    if (!loader.isEmpty()) params.addQueryItem("loaders", QString("[\"%1\"]").arg(loader));
    QString endpoint = "/project/" + projectId + "/version";
    if (!params.isEmpty()) endpoint += "?" + params.toString();
    QNetworkReply* reply = m_manager->get(createRequest(endpoint));
    handleResponse(reply, [cb](const QJsonDocument& doc) { cb(doc.array()); });
}

void ModrinthApi::requestVersion(const QString& versionId, std::function<void(const QJsonObject&)> cb)
{
    QNetworkReply* reply = m_manager->get(createRequest("/version/" + versionId));
    handleResponse(reply, [cb](const QJsonDocument& doc) { cb(doc.object()); });
}

void ModrinthApi::downloadFile(const QString& url, const QString& savePath)
{
    QNetworkReply* reply = m_manager->get(QNetworkRequest(QUrl(url)));

    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        emit downloadProgress(received, total);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, savePath]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit error(reply->errorString());
            reply->deleteLater();
            return;
        }
        QFile file(savePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(reply->readAll());
            file.close();
            emit downloadFinished(savePath);
        } else {
            emit error("Cannot open file: " + file.errorString());
        }
        reply->deleteLater();
    });
}