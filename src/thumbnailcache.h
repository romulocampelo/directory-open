#pragma once
#include <QCache>
#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <QVector>
#include <QWaitCondition>
#include <optional>

enum class ThumbnailKind { Image, Pdf, Video };
struct ThumbnailSource {
    QString path;
    qint64 size = 0, modified = 0;
    ThumbnailKind kind = ThumbnailKind::Image;
    bool operator==(const ThumbnailSource &) const = default;
};
size_t qHash(const ThumbnailSource &source, size_t seed = 0);
std::optional<ThumbnailSource> thumbnailSource(const QFileInfo &info);
struct ThumbnailResult {
    ThumbnailSource source;
    quint64 generation = 0;
    QImage image;
    QString error;
};
// One worker, replaceable viewport queue and one result. No queued bitmap payloads.
class ThumbnailWorker final : public QThread {
    Q_OBJECT
public:
    explicit ThumbnailWorker(QObject *parent = nullptr) : QThread(parent) {}
    ~ThumbnailWorker() override;
    void submit(const QVector<ThumbnailSource> &sources, quint64 generation);
    std::optional<ThumbnailResult> takeResult();
protected: void run() override;
signals: void ready();
private:
    QMutex m_mutex;
    QWaitCondition m_wake;
    QVector<ThumbnailSource> m_pending;
    std::optional<ThumbnailResult> m_result;
    std::optional<ThumbnailSource> m_busy;
    quint64 m_generation = 0, m_busyGeneration = 0;
    bool m_stopping = false;
};
// Shared by both panels. Freshness uses model size/mtime; F5 invalidates explicitly.
// Nothing is written to the browsed folders or to a disk cache.
class ThumbnailCache final : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailCache(QObject *parent = nullptr);
    const ThumbnailResult *find(const ThumbnailSource &source) const;
    void setDemand(QObject *owner, const QVector<ThumbnailSource> &sources);
    void removeDemand(QObject *owner);
    void invalidate();
    int entryCount() const { return m_cache.size(); }
    int retainedKiB() const { return m_cache.totalCost(); }
    quint64 completedDecodes() const { return m_completed; }
    int demandedCount() const;
signals: void changed();
private:
    ThumbnailWorker m_worker;
    QCache<ThumbnailSource, ThumbnailResult> m_cache{64 * 1024};
    QHash<QObject *, QVector<ThumbnailSource>> m_demands;
    quint64 m_generation = 1, m_completed = 0;
    QVector<ThumbnailSource> sources() const;
    void schedule();
};
