#pragma once

#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <array>

class QNetworkReply;
class QFile;
class QTimer;

struct DownloadItem {
    QString url;
    QString path;
    qint64  size = -1;
    QString sha1;
};

namespace DownloadUtil {

QNetworkRequest makeRequest(const QUrl& url);

bool isRetryableHttp(int status);

int backoffMs(int attempt);

bool isFileComplete(const DownloadItem& item);

QString sha1OfFile(const QString& path);

struct SpeedMeter {
    QElapsedTimer clock;
    qint64 lastMs = 0;
    qint64 lastBytes = 0;
    double speed = 0.0;

    void reset() { clock.start(); lastMs = 0; lastBytes = 0; speed = 0.0; }
    double update(qint64 bytes) {
        if (!clock.isValid()) reset();
        const qint64 now = clock.elapsed();
        const qint64 dt = now - lastMs;
        if (dt >= 700) {
            double inst = double(bytes - lastBytes) * 1000.0 / double(dt);
            if (inst < 0) inst = 0;
            speed = (speed <= 0.0) ? inst : (speed * 0.6 + inst * 0.4);
            lastBytes = bytes;
            lastMs = now;
        }
        return speed;
    }
};

}

class DownloadBatch : public QObject
{
    Q_OBJECT
public:
    explicit DownloadBatch(QObject* parent = nullptr);
    ~DownloadBatch() override;

    void start(const QList<DownloadItem>& items, int parallel = 16);

    void cancel();

    bool isRunning() const { return m_running; }
    int failedCount() const { return m_failed; }
    QString lastError() const { return m_lastError; }

signals:
    void progress(qint64 doneBytes, qint64 totalBytes, int doneFiles, int totalFiles, double bytesPerSec);
    void finished(bool ok, const QString& error);

private:
    struct Active;
    struct Pending {
        DownloadItem item;
        int attempt = 0;
    };

    void pump();
    void launch(const Pending& p);
    void onReadyRead(Active* a);
    void onReplyFinished(Active* a);
    void teardown(Active* a);
    void failItem(Active* a, const QString& err, bool retryable, bool dropPart);
    bool finalizeFile(const DownloadItem& item, QString* err);
    void checkDone();
    void emitProgress(bool force);
    QNetworkAccessManager* nextManager();

    std::array<QNetworkAccessManager*, 4> m_mgrs{};
    int m_nextMgr = 0;

    QList<Pending> m_queue;
    QList<Active*> m_active;
    int m_retrying = 0;
    int m_parallel = 16;
    bool m_running = false;
    quint64 m_gen = 0;

    qint64 m_totalBytes = 0;
    qint64 m_doneBytes = 0;
    int m_totalFiles = 0;
    int m_doneFiles = 0;
    int m_failed = 0;
    QString m_lastError;

    DownloadUtil::SpeedMeter m_speed;
    QElapsedTimer m_emitClock;
};

class RangedDownload : public QObject
{
    Q_OBJECT
public:
    explicit RangedDownload(QObject* parent = nullptr);
    ~RangedDownload() override;

    void start(const QString& url, const QString& path, int chunkMb = 4, int parallel = 8);
    void cancel();
    bool isRunning() const { return m_running; }

    static void removeState(const QString& path);

signals:
    void progress(qint64 done, qint64 total, double bytesPerSec);
    void finished(bool ok, const QString& error);

private:
    struct Chunk {
        qint64 start = 0;
        qint64 end = 0;
        qint64 got = 0;
        int attempt = 0;
        bool done = false;
    };
    struct ActiveChunk;

    void probe();
    void onProbeHeaders(QNetworkReply* reply);
    void onProbeFinished(QNetworkReply* reply);
    void probeFailed(const QString& err);

    void beginChunks(qint64 total);
    void startFallback(qint64 sizeHint);
    void pumpChunks();
    void launchChunk(int idx);
    void onChunkReadyRead(ActiveChunk* ac);
    void onChunkFinished(ActiveChunk* ac);
    void teardownChunk(ActiveChunk* ac);
    void abortAll();
    void fail(const QString& err);
    void complete();
    void saveState(bool complete) const;
    bool loadState(qint64 total);
    bool isAlreadyComplete() const;
    void emitProgress(bool force);
    void onStallTick();
    void recycleConnections();
    QNetworkAccessManager* nextManager();

    std::array<QNetworkAccessManager*, 4> m_mgrs{};
    int m_nextMgr = 0;

    QString m_url;
    QString m_finalUrl;
    QElapsedTimer m_finalAge;
    QTimer* m_stallTimer = nullptr;
    QElapsedTimer m_recycleClock;
    double m_avgSpeed = 0.0;
    double m_peakSpeed = 0.0;
    qint64 m_stallLastBytes = 0;
    int m_stallLowTicks = 0;
    int m_stallTicks = 0;
    QString m_path;
    qint64 m_chunkBytes = 4 * 1024 * 1024;
    int m_parallel = 8;
    bool m_running = false;
    quint64 m_gen = 0;

    int m_probeAttempt = 0;
    int m_probeKind = 0;
    qint64 m_probeTotal = 0;
    QNetworkReply* m_probeReply = nullptr;

    qint64 m_total = 0;
    qint64 m_doneBytes = 0;
    QFile* m_file = nullptr;
    QList<Chunk> m_chunks;
    QList<int> m_todo;
    QList<ActiveChunk*> m_active;
    int m_retrying = 0;
    DownloadBatch* m_fallback = nullptr;

    DownloadUtil::SpeedMeter m_speed;
    QElapsedTimer m_emitClock;
};

#endif