#include "modrinthapi.h"
#include "downloadmanager.h"

#include <QNetworkReply>
#include <QJsonDocument>
#include <QUrlQuery>
#include <QTimer>

namespace {
constexpr int kMaxAttempts = 4;
constexpr int kTimeoutMs = 20000;
}

ModrinthApi::ModrinthApi(QObject *parent)
    : QObject(parent), m_manager(new QNetworkAccessManager(this))
{
}

QNetworkRequest ModrinthApi::createRequest(const QString& endpoint) const
{
    QNetworkRequest request = DownloadUtil::makeRequest(QUrl(getApiBaseUrl() + endpoint));
    request.setRawHeader("Accept", "application/json");
    return request;
}

void ModrinthApi::getJson(const QString& endpoint, QObject* context,
                          std::function<void(const QJsonDocument&)> onOk, ErrorCb onError)
{
    auto r = std::make_shared<Req>();
    r->endpoint = endpoint;
    r->context = context;
    r->hasContext = (context != nullptr);
    r->onOk = std::move(onOk);
    r->onError = std::move(onError);
    sendGet(r);
}

void ModrinthApi::sendGet(const std::shared_ptr<Req>& r)
{
    if (r->hasContext && r->context.isNull()) return;
    QObject* ctx = r->hasContext ? r->context.data() : static_cast<QObject*>(this);

    QNetworkReply* reply = m_manager->get(createRequest(r->endpoint));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);

    auto* wd = new QTimer(reply);
    wd->setSingleShot(true);
    wd->setInterval(kTimeoutMs);
    connect(wd, &QTimer::timeout, reply, [reply]() { reply->abort(); });
    connect(reply, &QNetworkReply::downloadProgress, wd, [wd]() { wd->start(); });
    wd->start();

    connect(reply, &QNetworkReply::finished, ctx, [this, r, reply]() {
        const int st = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && st >= 200 && st < 300;

        if (ok) {
            QJsonParseError pe;
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &pe);
            if (pe.error == QJsonParseError::NoError) { r->onOk(doc); return; }
            if (r->onError) r->onError(QStringLiteral("Modrinth вернул некорректный ответ"));
            return;
        }

        const QString es = reply->errorString();
        if (DownloadUtil::isRetryableHttp(st) && r->attempt < kMaxAttempts) {
            r->attempt++;
            QObject* timerCtx = r->hasContext ? r->context.data() : static_cast<QObject*>(this);
            if (!timerCtx) return;
            QTimer::singleShot(DownloadUtil::backoffMs(r->attempt) * (st == 429 ? 3 : 1), timerCtx,
                               [this, r]() { sendGet(r); });
            return;
        }
        if (r->onError) r->onError(es);
    });
}

void ModrinthApi::searchMods(const QString& query, const QString& gameVersion, const QString& loader)
{
    QUrlQuery params;
    params.addQueryItem("query", query);
    params.addQueryItem("limit", "50");
    params.addQueryItem("index", "relevance");

    QJsonArray facets;
    if (!gameVersion.isEmpty()) facets.append(QJsonArray{"versions:" + gameVersion});
    if (!loader.isEmpty())      facets.append(QJsonArray{"categories:" + loader});
    facets.append(QJsonArray{"project_type:mod"});
    params.addQueryItem("facets", QString::fromUtf8(QJsonDocument(facets).toJson(QJsonDocument::Compact)));

    const quint64 seq = ++m_searchSeq;
    getJson("/search?" + params.toString(QUrl::FullyEncoded), this,
            [this, seq](const QJsonDocument& doc) {
                if (seq != m_searchSeq) return;
                emit searchCompleted(doc.object().value("hits").toArray());
            },
            [this, seq](const QString& err) {
                if (seq != m_searchSeq) return;
                emit searchFailed(err);
            });
}

void ModrinthApi::getProjectVersions(const QString& projectId, const QString& gameVersion, const QString& loader)
{
    requestVersions(projectId, gameVersion, loader,
                    [this](const QJsonArray& arr) { emit versionsReady(arr); },
                    [this](const QString& err) { emit error(err); }, this);
}

void ModrinthApi::requestProject(const QString& projectId, std::function<void(const QJsonObject&)> cb,
                                 ErrorCb onError, QObject* context)
{
    getJson("/project/" + projectId, context,
            [cb](const QJsonDocument& doc) { cb(doc.object()); }, std::move(onError));
}

void ModrinthApi::requestVersions(const QString& projectId, const QString& gameVersion, const QString& loader,
                                  std::function<void(const QJsonArray&)> cb, ErrorCb onError, QObject* context)
{
    QUrlQuery params;
    if (!gameVersion.isEmpty())
        params.addQueryItem("game_versions", QString::fromUtf8(QJsonDocument(QJsonArray{gameVersion}).toJson(QJsonDocument::Compact)));
    if (!loader.isEmpty())
        params.addQueryItem("loaders", QString::fromUtf8(QJsonDocument(QJsonArray{loader}).toJson(QJsonDocument::Compact)));

    QString endpoint = "/project/" + projectId + "/version";
    if (!params.isEmpty()) endpoint += "?" + params.toString(QUrl::FullyEncoded);

    getJson(endpoint, context,
            [cb](const QJsonDocument& doc) { cb(doc.array()); }, std::move(onError));
}

void ModrinthApi::requestVersion(const QString& versionId, std::function<void(const QJsonObject&)> cb,
                                 ErrorCb onError, QObject* context)
{
    getJson("/version/" + versionId, context,
            [cb](const QJsonDocument& doc) { cb(doc.object()); }, std::move(onError));
}

void ModrinthApi::downloadFile(const QString& url, const QString& savePath)
{
    auto* batch = new DownloadBatch(this);
    connect(batch, &DownloadBatch::progress, this,
            [this](qint64 done, qint64 total, int, int, double) { emit downloadProgress(done, total); });
    connect(batch, &DownloadBatch::finished, this, [this, batch, savePath](bool ok, const QString& err) {
        if (ok) emit downloadFinished(savePath);
        else    emit error(err);
        batch->deleteLater();
    });

    DownloadItem it;
    it.url = url;
    it.path = savePath;
    batch->start({it}, 1);
}