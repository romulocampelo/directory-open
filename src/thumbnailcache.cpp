#include "thumbnailcache.h"
#include "imagepreview.h"
#include <QFile>
#include <QImageReader>
#include <QSet>
#include <cmath>

size_t qHash(const ThumbnailSource &source, size_t seed) {
    return qHashMulti(seed, source.path, source.size, source.modified);
}
std::optional<ThumbnailSource> thumbnailSource(const QFileInfo &info) {
    // A bounded candidate list derived from installed Qt decoders. Do not probe
    // arbitrary documents or read image headers on the UI thread.
    static const auto formats = QImageReader::supportedImageFormats();
    if (!info.isFile() || info.isSymLink() || !formats.contains(info.suffix().toLower().toLatin1())) return std::nullopt;
    return ThumbnailSource{info.absoluteFilePath(), info.size(), info.lastModified().toMSecsSinceEpoch()};
}
namespace {
ThumbnailResult load(const ThumbnailSource &source, quint64 generation) {
    ThumbnailResult result; result.source = source; result.generation = generation;
    const auto matches = [&source](const QFileInfo &info) {
        return info.isFile() && !info.isSymLink() && info.size() == source.size && info.lastModified().toMSecsSinceEpoch() == source.modified;
    };
    if (!matches(QFileInfo(source.path))) { result.error = "O arquivo mudou. Use Atualizar (F5)."; return result; }
    if (source.size > maxImageFileBytes) { result.error = "Imagem acima do limite de 64 MiB para miniaturas."; return result; }
    QFile file(source.path);
    if (!file.open(QIODevice::ReadOnly)) { result.error = "Não foi possível ler a imagem: " + file.errorString(); return result; }
    const auto bytes = file.read(maxImageFileBytes + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() != source.size) { result.error = "Leitura incompleta da imagem. Use Atualizar (F5)."; return result; }
    file.close();
    if (!matches(QFileInfo(source.path))) { result.error = "O arquivo mudou durante a leitura. Use Atualizar (F5)."; return result; }
    const auto decoded = decodeImagePreview(bytes, 256);
    result.image = decoded.image.convertToFormat(QImage::Format_ARGB32_Premultiplied); result.error = decoded.error; return result;
}
}
ThumbnailWorker::~ThumbnailWorker() {
    { QMutexLocker lock(&m_mutex); m_stopping = true; m_pending.clear(); m_wake.wakeAll(); }
    wait();
}
void ThumbnailWorker::submit(const QVector<ThumbnailSource> &sources, quint64 generation) {
    QMutexLocker lock(&m_mutex); if (m_stopping) return;
    m_generation = generation; m_pending.clear();
    for (const auto &source : sources) {
        if (m_busy && *m_busy == source && m_busyGeneration == generation) continue;
        if (m_result && m_result->source == source && m_result->generation == generation) continue;
        if (m_pending.size() == 256) break;
        m_pending.append(source);
    }
    if (!isRunning() && !m_pending.isEmpty()) start();
    m_wake.wakeAll();
}
std::optional<ThumbnailResult> ThumbnailWorker::takeResult() {
    QMutexLocker lock(&m_mutex); auto result = std::move(m_result); m_result.reset(); m_wake.wakeAll(); return result;
}
void ThumbnailWorker::run() {
    for (;;) {
        ThumbnailSource source; quint64 generation;
        {
            QMutexLocker lock(&m_mutex);
            while (!m_stopping && (m_pending.isEmpty() || m_result)) m_wake.wait(&m_mutex);
            if (m_stopping) return;
            source = m_pending.takeFirst(); generation = m_generation; m_busy = source; m_busyGeneration = generation;
        }
        auto result = load(source, generation);
        {
            QMutexLocker lock(&m_mutex); m_busy.reset(); if (m_stopping) return;
            // Discard completed work if a refresh changed its identity.
            if (generation != m_generation) continue;
            m_result = std::move(result);
        }
        emit ready();
    }
}
ThumbnailCache::ThumbnailCache(QObject *parent) : QObject(parent) {
    connect(&m_worker, &ThumbnailWorker::ready, this, [this] {
        auto result = m_worker.takeResult(); if (!result || result->generation != m_generation) return;
        const auto requested = sources();
        if (requested.contains(result->source)) {
            ++m_completed;
            const auto cost = qMax(1, int((result->image.sizeInBytes() + 1023) / 1024));
            const auto source = result->source;
            m_cache.insert(source, new ThumbnailResult(std::move(*result)), cost);
            // Keep failure entries bounded even when they have no bitmap.
            while (m_cache.size() > 256) m_cache.remove(m_cache.keys().first());
            emit changed();
        }
        schedule();
    }, Qt::QueuedConnection);
}
const ThumbnailResult *ThumbnailCache::find(const ThumbnailSource &source) const { return m_cache.object(source); }
QVector<ThumbnailSource> ThumbnailCache::sources() const {
    QVector<ThumbnailSource> all; QSet<ThumbnailSource> seen;
    for (const auto &demand : m_demands) for (const auto &source : demand) {
        if (!seen.contains(source)) { seen.insert(source); all.append(source); }
        if (all.size() == 256) return all;
    }
    return all;
}
int ThumbnailCache::demandedCount() const { return sources().size(); }
void ThumbnailCache::schedule() {
    QVector<ThumbnailSource> pending;
    for (const auto &source : sources()) if (!find(source)) pending.append(source);
    m_worker.submit(pending, m_generation);
}
void ThumbnailCache::setDemand(QObject *owner, const QVector<ThumbnailSource> &sources) {
    if (!owner) return;
    const auto bounded = sources.mid(0, 128);
    const auto old = m_demands.constFind(owner);
    if (old != m_demands.cend() && old.value() == bounded) return;
    if (old == m_demands.cend()) connect(owner, &QObject::destroyed, this, [this, owner] { removeDemand(owner); });
    m_demands.insert(owner, bounded); schedule();
}
void ThumbnailCache::removeDemand(QObject *owner) { if (m_demands.remove(owner)) schedule(); }
void ThumbnailCache::invalidate() { ++m_generation; m_cache.clear(); schedule(); emit changed(); }
