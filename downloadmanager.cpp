#include "downloadmanager.h"

#include <QNetworkReply>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTimer>
#include <QSet>
#include <QUrl>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QStorageInfo>

namespace {
constexpr int kMaxAttempts        = 6;
constexpr int kChunkMaxAttempts   = 8;
constexpr int kProbeMaxAttempts   = 5;
constexpr int kNoDataTimeoutMs    = 30000;
constexpr int kProbeTimeoutMs     = 15000;
constexpr int kProgressIntervalMs = 150;

QString statePath(const QString& path) { return path + QStringLiteral(".dlstate"); }
}

namespace DownloadUtil {

QNetworkRequest makeRequest(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "SeaLauncher/1.0");
    return req;
}

bool isRetryableHttp(int status)
{
    return status == 0 || (status >= 200 && status < 300)
    || status == 408 || status == 425 || status == 429 || status >= 500;
}

int backoffMs(int attempt)
{
    const int step = qBound(0, attempt - 1, 4);
    const int base = qMin(8000, 500 * (1 << step));
    return base + int(QRandomGenerator::global()->bounded(250));
}

bool isFileComplete(const DownloadItem& item)
{
    const QFileInfo fi(item.path);
    if (!fi.exists() || !fi.isFile()) return false;
    if (item.size > 0) return fi.size() == item.size;
    return fi.size() > 0;
}

QString sha1OfFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash h(QCryptographicHash::Sha1);
    if (!h.addData(&f)) return QString();
    return QString::fromLatin1(h.result().toHex());
}

}

struct DownloadBatch::Active {
    Pending p;
    QNetworkReply* reply = nullptr;
    QFile* file = nullptr;
    QTimer* watchdog = nullptr;
    qint64 resumeFrom = 0;
    qint64 counted = 0;
    qint64 gotNow = 0;
    bool resumed = false;
    bool statusChecked = false;
    bool diskError = false;
    bool timedOut = false;
};

DownloadBatch::DownloadBatch(QObject* parent) : QObject(parent) {}

DownloadBatch::~DownloadBatch()
{
    cancel();
}

QNetworkAccessManager* DownloadBatch::nextManager()
{
    const int i = (m_nextMgr++) % int(m_mgrs.size());
    if (!m_mgrs[i]) m_mgrs[i] = new QNetworkAccessManager(this);
    return m_mgrs[i];
}

void DownloadBatch::start(const QList<DownloadItem>& items, int parallel)
{
    cancel();

    m_parallel = qBound(1, parallel, 64);
    m_running = true;
    m_totalBytes = m_doneBytes = 0;
    m_totalFiles = m_doneFiles = m_failed = 0;
    m_lastError.clear();

    QSet<QString> seen;
    for (const DownloadItem& it : items) {
        if (it.url.isEmpty() || it.path.isEmpty()) continue;
        if (seen.contains(it.path)) continue;
        seen.insert(it.path);

        m_totalFiles++;
        if (it.size > 0) m_totalBytes += it.size;

        if (DownloadUtil::isFileComplete(it)) {
            m_doneFiles++;
            if (it.size > 0) m_doneBytes += it.size;
            continue;
        }
        Pending p;
        p.item = it;
        m_queue.append(p);
    }

    m_speed.reset();
    m_emitClock.start();

    const quint64 gen = m_gen;
    QTimer::singleShot(0, this, [this, gen]() {
        if (gen != m_gen || !m_running) return;
        emitProgress(true);
        pump();
    });
}

void DownloadBatch::cancel()
{
    ++m_gen;
    m_running = false;
    m_queue.clear();
    m_retrying = 0;

    const QList<Active*> act = m_active;
    m_active.clear();
    for (Active* a : act) teardown(a);
}

void DownloadBatch::pump()
{
    while (m_running && m_active.size() < m_parallel && !m_queue.isEmpty())
        launch(m_queue.takeFirst());
    checkDone();
}

void DownloadBatch::checkDone()
{
    if (!m_running) return;
    if (!m_queue.isEmpty() || !m_active.isEmpty() || m_retrying > 0) return;

    m_running = false;
    emitProgress(true);
    emit finished(m_failed == 0, m_lastError);
}

void DownloadBatch::emitProgress(bool force)
{
    if (!force && m_emitClock.isValid() && m_emitClock.elapsed() < kProgressIntervalMs)
        return;
    m_emitClock.restart();
    const double speed = m_speed.update(m_doneBytes);
    emit progress(m_doneBytes, m_totalBytes, m_doneFiles, m_totalFiles, speed);
}

bool DownloadBatch::finalizeFile(const DownloadItem& item, QString* err)
{
    const QString part = item.path + QStringLiteral(".part");
    const QFileInfo pi(part);

    auto bad = [err](const QString& msg) { if (err) *err = msg; return false; };

    if (!pi.exists())
        return bad(QStringLiteral("Временный файл не найден"));
    if (item.size > 0 && pi.size() != item.size)
        return bad(QStringLiteral("Получено %1 из %2 байт").arg(pi.size()).arg(item.size));
    if (item.size <= 0 && pi.size() == 0)
        return bad(QStringLiteral("Получен пустой файл"));
    if (!item.sha1.isEmpty()) {
        const QString h = DownloadUtil::sha1OfFile(part);
        if (h.compare(item.sha1, Qt::CaseInsensitive) != 0)
            return bad(QStringLiteral("Контрольная сумма не совпала"));
    }

    QFile::remove(item.path);
    if (!QFile::rename(part, item.path))
        return bad(QStringLiteral("Не удалось переименовать временный файл"));
    return true;
}

void DownloadBatch::launch(const Pending& p)
{
    const QString part = p.item.path + QStringLiteral(".part");
    QDir().mkpath(QFileInfo(p.item.path).absolutePath());

    qint64 have = 0;
    {
        const QFileInfo pi(part);
        if (pi.exists()) have = pi.size();
    }
    if (p.item.size > 0 && have > p.item.size) {
        QFile::remove(part);
        have = 0;
    }

    if (p.item.size > 0 && have == p.item.size) {
        QString err;
        if (finalizeFile(p.item, &err)) {
            m_doneFiles++;
            m_doneBytes += p.item.size;
            return;
        }
        QFile::remove(part);
        have = 0;
    }

    auto* file = new QFile(part);
    const QIODevice::OpenMode mode = have > 0
                                         ? (QIODevice::WriteOnly | QIODevice::Append)
                                         : (QIODevice::WriteOnly | QIODevice::Truncate);
    if (!file->open(mode)) {
        m_failed++;
        m_lastError = QFileInfo(p.item.path).fileName() + QStringLiteral(": не удалось открыть файл для записи (")
                      + file->errorString() + QLatin1Char(')');
        delete file;
        return;
    }

    auto* a = new Active;
    a->p = p;
    a->file = file;
    a->resumeFrom = have;
    a->counted = have;
    m_doneBytes += have;

    QNetworkRequest req = DownloadUtil::makeRequest(QUrl(p.item.url));
    if (p.item.size >= 8LL * 1024 * 1024)
        req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    if (have > 0)
        req.setRawHeader("Range", "bytes=" + QByteArray::number(have) + "-");

    QNetworkReply* reply = nextManager()->get(req);
    a->reply = reply;

    a->watchdog = new QTimer(reply);
    a->watchdog->setSingleShot(true);
    a->watchdog->setInterval(kNoDataTimeoutMs);
    connect(a->watchdog, &QTimer::timeout, reply, [reply, a]() {
        a->timedOut = true;
        reply->abort();
    });
    a->watchdog->start();

    m_active.append(a);
    connect(reply, &QNetworkReply::readyRead, this, [this, a]() { onReadyRead(a); });
    connect(reply, &QNetworkReply::finished,  this, [this, a]() { onReplyFinished(a); });
}

void DownloadBatch::onReadyRead(Active* a)
{
    const int st = a->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (st < 200 || st >= 300) {
        a->reply->readAll();
        return;
    }

    if (!a->statusChecked) {
        a->statusChecked = true;
        if (a->resumeFrom > 0) {
            if (st == 206) {
                a->resumed = true;
            } else {
                m_doneBytes -= a->counted;
                a->counted = 0;
                a->resumeFrom = 0;
                a->file->resize(0);
            }
        }
    }

    const QByteArray data = a->reply->readAll();
    if (data.isEmpty()) return;

    if (a->file->write(data) != data.size()) {
        a->diskError = true;
        a->reply->abort();
        return;
    }

    m_doneBytes += data.size();
    a->counted += data.size();
    a->gotNow += data.size();
    a->watchdog->start();
    emitProgress(false);
}

void DownloadBatch::onReplyFinished(Active* a)
{
    QNetworkReply* r = a->reply;
    const int st = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError err = r->error();
    QString es = r->errorString();
    if (a->timedOut) es = QStringLiteral("Превышено время ожидания данных");

    const bool httpOk = (err == QNetworkReply::NoError && st >= 200 && st < 300);
    if (httpOk) onReadyRead(a);

    if (a->file) {
        a->file->flush();
        a->file->close();
    }

    if (a->diskError) {
        failItem(a, QStringLiteral("Ошибка записи на диск: ") + a->file->errorString(), false, false);
    } else if (httpOk) {
        QString verr;
        const DownloadItem item = a->p.item;
        if (finalizeFile(item, &verr)) {
            m_doneFiles++;
            teardown(a);
        } else {
            failItem(a, verr, true, true);
        }
    } else {
        const bool dropPart = (st == 416) || (st >= 400 && st < 500 && st != 408 && st != 429);
        const bool retryable = DownloadUtil::isRetryableHttp(st) || st == 416;
        failItem(a, es.isEmpty() ? QStringLiteral("HTTP %1").arg(st) : es, retryable, dropPart);
    }

    pump();
}

void DownloadBatch::failItem(Active* a, const QString& err, bool retryable, bool dropPart)
{
    Pending p = a->p;
    const QString part = p.item.path + QStringLiteral(".part");

    m_doneBytes -= a->counted;
    if (m_doneBytes < 0) m_doneBytes = 0;
    const bool progressed = a->resumed && a->gotNow > 0;

    teardown(a);
    if (dropPart) QFile::remove(part);

    if (progressed) p.attempt = 0;

    if (retryable && m_running && p.attempt < kMaxAttempts) {
        p.attempt++;
        m_retrying++;
        const quint64 gen = m_gen;
        qWarning() << "[DL] retry" << p.attempt << "of" << kMaxAttempts << p.item.url << "-" << err;
        QTimer::singleShot(DownloadUtil::backoffMs(p.attempt), this, [this, p, gen]() {
            if (gen != m_gen) return;
            m_retrying--;
            m_queue.prepend(p);
            pump();
        });
        return;
    }

    m_failed++;
    m_lastError = QFileInfo(p.item.path).fileName() + QStringLiteral(": ") + err;
    qWarning() << "[DL] FAILED" << p.item.url << "-" << err;
}

void DownloadBatch::teardown(Active* a)
{
    m_active.removeOne(a);
    if (a->watchdog) a->watchdog->stop();
    if (a->reply) {
        a->reply->disconnect(this);
        if (a->reply->isRunning()) a->reply->abort();
        a->reply->deleteLater();
    }
    if (a->file) {
        if (a->file->isOpen()) a->file->close();
        delete a->file;
    }
    delete a;
}

struct RangedDownload::ActiveChunk {
    int idx = 0;
    qint64 startGot = 0;
    QNetworkReply* reply = nullptr;
    QTimer* watchdog = nullptr;
    qint64 gotNow = 0;
    bool bad = false;
    bool timedOut = false;
    bool diskError = false;
    bool usedFinal = false;
    bool urlCaptured = false;
    bool recycled = false;
};

RangedDownload::RangedDownload(QObject* parent) : QObject(parent) {}

RangedDownload::~RangedDownload()
{
    cancel();
}

QNetworkAccessManager* RangedDownload::nextManager()
{
    const int i = (m_nextMgr++) % int(m_mgrs.size());
    if (!m_mgrs[i]) m_mgrs[i] = new QNetworkAccessManager(this);
    return m_mgrs[i];
}

void RangedDownload::removeState(const QString& path)
{
    QFile::remove(statePath(path));
    QFile::remove(path + QStringLiteral(".part"));
}

bool RangedDownload::isAlreadyComplete() const
{
    QFile f(statePath(m_path));
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (!o.value(QStringLiteral("complete")).toBool()) return false;
    if (o.value(QStringLiteral("url")).toString() != m_url) return false;
    const qint64 size = qint64(o.value(QStringLiteral("size")).toDouble());
    return size > 0 && QFileInfo(m_path).size() == size;
}

void RangedDownload::start(const QString& url, const QString& path, int chunkMb, int parallel)
{
    cancel();

    m_url = url;
    m_path = path;
    m_chunkBytes = qint64(qMax(1, chunkMb)) * 1024 * 1024;
    m_parallel = qBound(1, parallel, 16);
    m_running = true;
    m_probeAttempt = 0;
    m_probeKind = 0;
    m_probeTotal = 0;
    m_finalUrl.clear();
    m_finalAge.invalidate();
    m_total = 0;
    m_doneBytes = 0;
    m_retrying = 0;
    m_chunks.clear();
    m_todo.clear();
    m_speed.reset();
    m_emitClock.start();

    QDir().mkpath(QFileInfo(path).absolutePath());

    if (isAlreadyComplete()) {
        const quint64 gen = m_gen;
        QTimer::singleShot(0, this, [this, gen]() {
            if (gen != m_gen || !m_running) return;
            m_running = false;
            m_total = QFileInfo(m_path).size();
            m_doneBytes = m_total;
            emit progress(m_doneBytes, m_total, 0.0);
            emit finished(true, QString());
        });
        return;
    }

    probe();
}

void RangedDownload::cancel()
{
    const bool wasRunning = m_running;
    ++m_gen;
    m_running = false;

    bool allDone = !m_chunks.isEmpty();
    for (const Chunk& c : m_chunks) if (!c.done) { allDone = false; break; }
    if (wasRunning && !m_chunks.isEmpty() && !allDone)
        saveState(false);

    abortAll();
}

void RangedDownload::abortAll()
{
    if (m_stallTimer) m_stallTimer->stop();
    if (m_probeReply) {
        m_probeReply->disconnect(this);
        m_probeReply->abort();
        m_probeReply->deleteLater();
        m_probeReply = nullptr;
    }
    if (m_fallback) m_fallback->cancel();

    const QList<ActiveChunk*> act = m_active;
    m_active.clear();
    for (ActiveChunk* ac : act) teardownChunk(ac);

    m_todo.clear();
    m_retrying = 0;

    if (m_file) {
        if (m_file->isOpen()) m_file->close();
        delete m_file;
        m_file = nullptr;
    }
}

void RangedDownload::emitProgress(bool force)
{
    if (!force && m_emitClock.isValid() && m_emitClock.elapsed() < kProgressIntervalMs)
        return;
    m_emitClock.restart();
    emit progress(m_doneBytes, m_total, m_speed.update(m_doneBytes));
}

void RangedDownload::probe()
{
    m_probeKind = 0;
    m_probeTotal = 0;

    QNetworkRequest req = DownloadUtil::makeRequest(QUrl(m_url));
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setRawHeader("Range", "bytes=0-0");
    QNetworkReply* reply = nextManager()->get(req);
    m_probeReply = reply;

    auto* wd = new QTimer(reply);
    wd->setSingleShot(true);
    wd->setInterval(kProbeTimeoutMs);
    connect(wd, &QTimer::timeout, reply, [reply]() { reply->abort(); });
    wd->start();

    const quint64 gen = m_gen;
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply, gen]() {
        if (gen != m_gen) return;
        onProbeHeaders(reply);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
        if (gen != m_gen) { reply->deleteLater(); return; }
        onProbeFinished(reply);
    });
}

void RangedDownload::onProbeHeaders(QNetworkReply* reply)
{
    if (m_probeKind != 0) return;
    const int st = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (st == 0 || (st >= 300 && st < 400)) return;

    if (st == 206) {
        const QByteArray cr = reply->rawHeader("Content-Range");
        const int slash = cr.lastIndexOf('/');
        const qint64 total = slash >= 0 ? cr.mid(slash + 1).toLongLong() : 0;
        if (total > 0) { m_probeKind = 1; m_probeTotal = total; }
        else           { m_probeKind = 2; m_probeTotal = -1; }
    } else if (st == 200) {
        m_probeKind = 2;
        m_probeTotal = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if (m_probeTotal <= 0) m_probeTotal = -1;
    } else {
        return;
    }

    reply->abort();
}

void RangedDownload::onProbeFinished(QNetworkReply* reply)
{
    if (m_probeReply == reply) m_probeReply = nullptr;

    const int st = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString es = reply->errorString();
    const QString probeUrl = reply->url().toString();
    reply->deleteLater();

    if (m_probeKind == 1 && !probeUrl.isEmpty() && probeUrl != m_url) {
        m_finalUrl = probeUrl;
        m_finalAge.restart();
    }

    if (m_probeKind == 1) { beginChunks(m_probeTotal); return; }
    if (m_probeKind == 2) { startFallback(m_probeTotal); return; }

    const bool retryable = DownloadUtil::isRetryableHttp(st);
    if (retryable && m_probeAttempt < kProbeMaxAttempts) {
        m_probeAttempt++;
        const quint64 gen = m_gen;
        QTimer::singleShot(DownloadUtil::backoffMs(m_probeAttempt), this, [this, gen]() {
            if (gen != m_gen || !m_running) return;
            probe();
        });
        return;
    }
    probeFailed(es.isEmpty() ? QStringLiteral("HTTP %1").arg(st) : es);
}

void RangedDownload::probeFailed(const QString& err)
{
    fail(QStringLiteral("Не удалось получить информацию о файле: ") + err);
}

void RangedDownload::startFallback(qint64 sizeHint)
{
    if (!m_fallback) {
        m_fallback = new DownloadBatch(this);
        connect(m_fallback, &DownloadBatch::progress, this,
                [this](qint64 done, qint64 total, int, int, double speed) {
                    if (!m_running) return;
                    m_total = total;
                    m_doneBytes = done;
                    emit progress(done, total, speed);
                });
        connect(m_fallback, &DownloadBatch::finished, this, [this](bool ok, const QString& err) {
            if (!m_running) return;
            m_running = false;
            if (ok) {
                m_total = QFileInfo(m_path).size();
                saveState(true);
            }
            emit finished(ok, err);
        });
    }

    QFile::remove(m_path);

    DownloadItem it;
    it.url = m_url;
    it.path = m_path;
    it.size = sizeHint > 0 ? sizeHint : -1;
    m_total = it.size > 0 ? it.size : 0;
    m_fallback->start({it}, 1);
}

void RangedDownload::beginChunks(qint64 total)
{
    m_total = total;
    m_chunks.clear();
    for (qint64 pos = 0; pos < total; pos += m_chunkBytes) {
        Chunk c;
        c.start = pos;
        c.end = qMin(pos + m_chunkBytes - 1, total - 1);
        m_chunks.append(c);
    }

    const bool reuse = loadState(total);

    {
        qint64 remaining = 0;
        for (const Chunk& c : m_chunks)
            if (!reuse || !c.done) remaining += c.end - c.start + 1;
        const QStorageInfo storage(QFileInfo(m_path).absolutePath());
        if (storage.isValid() && storage.bytesAvailable() >= 0 &&
            storage.bytesAvailable() < remaining + 200LL * 1024 * 1024) {
            fail(QStringLiteral("Недостаточно места на диске: нужно ещё ~%1 МБ, свободно %2 МБ.")
                     .arg((remaining + 200LL * 1024 * 1024) / 1048576).arg(storage.bytesAvailable() / 1048576));
            return;
        }
    }

    m_file = new QFile(m_path);
    if (!m_file->open(QIODevice::ReadWrite)) {
        const QString e = m_file->errorString();
        delete m_file;
        m_file = nullptr;
        fail(QStringLiteral("Не удалось открыть файл «%1»: %2").arg(m_path, e));
        return;
    }
    if (!reuse || m_file->size() != total) {
        for (Chunk& c : m_chunks) { c.done = false; c.got = 0; }
        m_file->resize(0);
        if (!m_file->resize(total)) {
            fail(QStringLiteral("Не удалось выделить %1 МБ на диске. Проверьте свободное место.")
                     .arg(total / 1048576));
            return;
        }
    }

    m_doneBytes = 0;
    m_todo.clear();
    for (int i = 0; i < m_chunks.size(); ++i) {
        if (m_chunks[i].done) m_doneBytes += m_chunks[i].end - m_chunks[i].start + 1;
        else m_todo.append(i);
    }

    saveState(false);
    emitProgress(true);

    if (!m_stallTimer) {
        m_stallTimer = new QTimer(this);
        m_stallTimer->setInterval(1000);
        connect(m_stallTimer, &QTimer::timeout, this, &RangedDownload::onStallTick);
    }
    m_avgSpeed = 0.0;
    m_peakSpeed = 0.0;
    m_stallLastBytes = m_doneBytes;
    m_stallLowTicks = 0;
    m_stallTicks = 0;
    m_recycleClock.start();
    m_stallTimer->start();

    pumpChunks();
}

void RangedDownload::onStallTick()
{
    if (!m_running || m_active.isEmpty()) {
        m_stallLastBytes = m_doneBytes;
        return;
    }

    const double inst = double(m_doneBytes - m_stallLastBytes);
    m_stallLastBytes = m_doneBytes;
    m_stallTicks++;

    m_avgSpeed = m_avgSpeed <= 0.0 ? inst : (m_avgSpeed * 0.6 + inst * 0.4);
    m_peakSpeed = qMax(m_peakSpeed * 0.98, m_avgSpeed);

    if (m_stallTicks >= 5 && m_peakSpeed > 1.5e6 && m_avgSpeed < m_peakSpeed * 0.25)
        m_stallLowTicks++;
    else
        m_stallLowTicks = 0;

    if (m_stallLowTicks >= 4 && m_recycleClock.elapsed() > 10000) {
        m_stallLowTicks = 0;
        m_recycleClock.restart();
        recycleConnections();
    }
}

void RangedDownload::recycleConnections()
{
    qWarning() << "[DL] speed collapsed, recycling connections";

    for (QNetworkAccessManager* mgr : m_mgrs)
        if (mgr) mgr->clearConnectionCache();

    const QList<ActiveChunk*> act = m_active;
    for (ActiveChunk* ac : act) {
        if (!m_running) break;
        if (!m_active.contains(ac)) continue;
        ac->recycled = true;
        ac->reply->abort();
    }
}

void RangedDownload::pumpChunks()
{
    while (m_running && m_active.size() < m_parallel && !m_todo.isEmpty())
        launchChunk(m_todo.takeFirst());

    if (m_running && m_todo.isEmpty() && m_active.isEmpty() && m_retrying == 0)
        complete();
}

void RangedDownload::launchChunk(int idx)
{
    const Chunk& c = m_chunks.at(idx);

    auto* ac = new ActiveChunk;
    ac->idx = idx;
    ac->startGot = c.got;

    const bool useFinal = !m_finalUrl.isEmpty() && m_finalAge.isValid() && m_finalAge.elapsed() < 120000;
    ac->usedFinal = useFinal;

    QNetworkRequest req = DownloadUtil::makeRequest(QUrl(useFinal ? m_finalUrl : m_url));
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setRawHeader("Range", "bytes=" + QByteArray::number(c.start + c.got) + "-" + QByteArray::number(c.end));

    QNetworkReply* reply = nextManager()->get(req);
    ac->reply = reply;

    ac->watchdog = new QTimer(reply);
    ac->watchdog->setSingleShot(true);
    ac->watchdog->setInterval(kNoDataTimeoutMs);
    connect(ac->watchdog, &QTimer::timeout, reply, [reply, ac]() {
        ac->timedOut = true;
        reply->abort();
    });
    ac->watchdog->start();

    m_active.append(ac);
    connect(reply, &QNetworkReply::readyRead, this, [this, ac]() { onChunkReadyRead(ac); });
    connect(reply, &QNetworkReply::finished,  this, [this, ac]() { onChunkFinished(ac); });
}

void RangedDownload::onChunkReadyRead(ActiveChunk* ac)
{
    const int st = ac->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (st < 200 || st >= 300) {
        ac->reply->readAll();
        return;
    }

    if (st != 206 && !(st == 200 && m_chunks.size() == 1 && ac->startGot == 0)) {
        ac->bad = true;
        ac->reply->readAll();
        ac->reply->abort();
        return;
    }

    if (!ac->usedFinal && !ac->urlCaptured) {
        ac->urlCaptured = true;
        const QString u = ac->reply->url().toString();
        if (!u.isEmpty() && u != m_url) {
            m_finalUrl = u;
            m_finalAge.restart();
        }
    }

    Chunk& c = m_chunks[ac->idx];
    const QByteArray data = ac->reply->readAll();
    if (data.isEmpty()) return;

    const qint64 room = (c.end - c.start + 1) - c.got;
    const QByteArray d = (data.size() > room) ? data.left(qMax<qint64>(0, room)) : data;

    if (!m_file->seek(c.start + c.got) || m_file->write(d) != d.size()) {
        ac->diskError = true;
        ac->reply->abort();
        return;
    }

    c.got += d.size();
    m_doneBytes += d.size();
    ac->gotNow += d.size();
    ac->watchdog->start();
    emitProgress(false);
}

void RangedDownload::onChunkFinished(ActiveChunk* ac)
{
    QNetworkReply* r = ac->reply;
    const int st = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError err = r->error();
    QString es = r->errorString();
    if (ac->timedOut) es = QStringLiteral("Превышено время ожидания данных");

    const bool httpOk = (err == QNetworkReply::NoError && st >= 200 && st < 300);
    if (httpOk) onChunkReadyRead(ac);

    const int idx = ac->idx;
    const bool progressed = ac->gotNow > 0;
    const bool diskErr = ac->diskError;
    const bool bad = ac->bad;
    const bool recycled = ac->recycled;
    const bool usedFinal = ac->usedFinal;
    teardownChunk(ac);

    if (!m_running) return;

    if (recycled) {
        m_todo.prepend(idx);
        pumpChunks();
        return;
    }

    if (diskErr) {
        fail(QStringLiteral("Ошибка записи на диск: ") + (m_file ? m_file->errorString() : QString()));
        return;
    }

    Chunk& c = m_chunks[idx];
    const qint64 expected = c.end - c.start + 1;

    if (!bad && httpOk && c.got >= expected) {
        c.done = true;
        saveState(false);
        pumpChunks();
        return;
    }

    if (bad) {
        fail(QStringLiteral("Сервер не поддерживает докачку по частям"));
        return;
    }
    const bool shortRead = httpOk;
    const bool urlExpired = usedFinal && (st == 400 || st == 401 || st == 403 || st == 404);
    if (urlExpired) m_finalUrl.clear();
    if (!shortRead && !urlExpired && !DownloadUtil::isRetryableHttp(st)) {
        fail(QStringLiteral("HTTP %1: %2").arg(st).arg(es));
        return;
    }

    if (progressed) c.attempt = 0;
    c.attempt++;
    if (c.attempt >= kChunkMaxAttempts) {
        fail(QStringLiteral("Не удалось докачать файл после нескольких попыток: ") + es);
        return;
    }

    qWarning() << "[DL] chunk" << idx << "retry" << c.attempt << "-" << es;
    m_retrying++;
    const quint64 gen = m_gen;
    QTimer::singleShot(DownloadUtil::backoffMs(c.attempt), this, [this, idx, gen]() {
        if (gen != m_gen || !m_running) return;
        m_retrying--;
        m_todo.prepend(idx);
        pumpChunks();
    });
}

void RangedDownload::teardownChunk(ActiveChunk* ac)
{
    m_active.removeOne(ac);
    if (ac->watchdog) ac->watchdog->stop();
    if (ac->reply) {
        ac->reply->disconnect(this);
        if (ac->reply->isRunning()) ac->reply->abort();
        ac->reply->deleteLater();
    }
    delete ac;
}

void RangedDownload::fail(const QString& err)
{
    if (!m_running) return;
    ++m_gen;
    m_running = false;
    if (!m_chunks.isEmpty()) saveState(false);
    abortAll();
    emit finished(false, err);
}

void RangedDownload::complete()
{
    m_running = false;
    if (m_stallTimer) m_stallTimer->stop();

    if (m_file) {
        m_file->flush();
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }

    if (QFileInfo(m_path).size() != m_total) {
        QFile::remove(statePath(m_path));
        emit finished(false, QStringLiteral("Размер скачанного файла не совпал с ожидаемым"));
        return;
    }

    saveState(true);
    m_doneBytes = m_total;
    emitProgress(true);
    emit finished(true, QString());
}

void RangedDownload::saveState(bool complete) const
{
    QJsonArray done;
    for (int i = 0; i < m_chunks.size(); ++i)
        if (m_chunks[i].done) done.append(i);

    QJsonObject o;
    o[QStringLiteral("url")] = m_url;
    o[QStringLiteral("size")] = double(m_total);
    o[QStringLiteral("chunk")] = double(m_chunkBytes);
    o[QStringLiteral("done")] = done;
    o[QStringLiteral("complete")] = complete;

    QFile f(statePath(m_path));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

bool RangedDownload::loadState(qint64 total)
{
    QFile f(statePath(m_path));
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();

    if (o.value(QStringLiteral("url")).toString() != m_url) return false;
    if (qint64(o.value(QStringLiteral("size")).toDouble()) != total) return false;
    if (qint64(o.value(QStringLiteral("chunk")).toDouble()) != m_chunkBytes) return false;
    if (QFileInfo(m_path).size() != total) return false;

    for (const QJsonValue& v : o.value(QStringLiteral("done")).toArray()) {
        const int i = v.toInt(-1);
        if (i >= 0 && i < m_chunks.size()) {
            m_chunks[i].done = true;
            m_chunks[i].got = m_chunks[i].end - m_chunks[i].start + 1;
        }
    }
    return true;
}