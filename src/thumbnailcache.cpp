#include "thumbnailcache.h"
#include "imagepreview.h"
#include "pdfrenderer.h"
#include "mediatypes.h"
#include <QEventLoop>
#include <QMediaPlayer>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
#include <QFile>
#include <QImageReader>
#include <QSet>
#include <cmath>

size_t qHash(const ThumbnailSource &source, size_t seed) {
    return qHashMulti(seed, source.path, source.size, source.modified, int(source.kind));
}
std::optional<ThumbnailSource> thumbnailSource(const QFileInfo &info) {
    // Classify by metadata only; all content decoding belongs to the worker.
    static const auto formats = QImageReader::supportedImageFormats();
    if (!info.isFile() || info.isSymLink() || info.isJunction()) return std::nullopt;
    const auto suffix = info.suffix().toLower();
    ThumbnailKind kind;
    if (suffix == "pdf") kind = ThumbnailKind::Pdf;
    else if (mediaKindForPath(info.fileName()) == MediaKind::Video) kind = ThumbnailKind::Video;
    else if (formats.contains(suffix.toLatin1())) kind = ThumbnailKind::Image;
    else return std::nullopt;
    return ThumbnailSource{info.absoluteFilePath(),info.size(),info.lastModified().toMSecsSinceEpoch(),kind};
}

namespace {
QImage videoFrame(QFile &file, QString &error, const std::function<bool()> &stale) {
    // The worker owns the player, device and sink until decoder cleanup finishes.
    // No audio output is attached. Never retain the backend's video frames.
    QImage image;
    QMediaPlayer player;
    QVideoSink sink;
    QEventLoop loop;
    QTimer deadline, cancellation;
    deadline.setSingleShot(true); deadline.setInterval(8000); cancellation.setInterval(25);
    bool finished = false;
    auto quit = [&] { finished = true; QTimer::singleShot(0,&loop,&QEventLoop::quit); };
    QObject::connect(&deadline,&QTimer::timeout,&loop,[&] { if (!finished) { error = "O vídeo não forneceu um quadro em 8 segundos."; quit(); } });
    QObject::connect(&cancellation,&QTimer::timeout,&loop,[&] { if (!finished && stale()) { error = "Miniatura cancelada."; quit(); } });
    QObject::connect(&player,&QMediaPlayer::errorOccurred,&loop,[&](QMediaPlayer::Error,const QString &message) {
        if (!finished) { error = "Não foi possível decodificar o vídeo: " + message; quit(); }
    });
    QObject::connect(&sink,&QVideoSink::videoFrameChanged,&loop,[&](const QVideoFrame &frame) {
        if (finished || !frame.isValid()) return;
        if (stale()) { error = "Miniatura cancelada."; quit(); return; }
        const auto size = frame.size();
        if (!size.isValid() || qint64(size.width()) * size.height() > 16 * 1024 * 1024) {
            error = "O quadro excede o limite de 16 megapixels para miniaturas."; quit(); return;
        }
        const auto decoded = frame.toImage();
        if (decoded.isNull()) { error = "O decodificador não retornou uma imagem utilizável."; quit(); return; }
        image = decoded.scaled(256,256,Qt::KeepAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        quit();
    });
    player.setVideoSink(&sink); player.setSourceDevice(&file,QUrl::fromLocalFile(file.fileName()));
    deadline.start(); cancellation.start(); player.play(); loop.exec();
    deadline.stop(); cancellation.stop(); player.stop(); player.setSourceDevice(nullptr); player.setVideoSink(nullptr); sink.setVideoFrame({});
    return image;
}
ThumbnailResult load(const ThumbnailSource &source, quint64 generation, PdfRenderer &pdf, const std::function<bool()> &stale) {
    ThumbnailResult result; result.source = source; result.generation = generation;
    const auto matches = [&source](const QFileInfo &info) {
        return info.isFile() && !info.isSymLink() && !info.isJunction() && info.size() == source.size && info.lastModified().toMSecsSinceEpoch() == source.modified;
    };
    if (stale()) return result;
    if (!matches(QFileInfo(source.path))) { result.error = "O arquivo mudou. Use Atualizar (F5)."; return result; }
    if (source.kind != ThumbnailKind::Video && source.size > maxImageFileBytes) { result.error = "Arquivo acima do limite de 64 MiB para miniaturas."; return result; }
    QFile file(source.path);
    if (!file.open(QIODevice::ReadOnly)) { result.error = "Não foi possível ler o arquivo: " + file.errorString(); return result; }
    if (source.kind == ThumbnailKind::Video) {
        pdf.clear(); result.image = videoFrame(file,result.error,stale);
    } else {
        const auto bytes = file.read(maxImageFileBytes + 1);
        if (file.error() != QFileDevice::NoError || bytes.size() != source.size) { result.error = "Leitura incompleta. Use Atualizar (F5)."; return result; }
        file.close();
        if (!matches(QFileInfo(source.path))) { result.error = "O arquivo mudou durante a leitura. Use Atualizar (F5)."; return result; }
        if (stale()) return result;
        if (source.kind == ThumbnailKind::Pdf) {
            const auto decoded = pdf.render(bytes,0,{256,256},stale);
            result.image = decoded.image.convertToFormat(QImage::Format_ARGB32_Premultiplied); result.error = decoded.message;
        } else {
            pdf.clear(); const auto decoded = decodeImagePreview(bytes,256);
            result.image = decoded.image.convertToFormat(QImage::Format_ARGB32_Premultiplied); result.error = decoded.error;
        }
    }
    file.close();
    if (!matches(QFileInfo(source.path))) { result.image = {}; result.error = "O arquivo mudou. Use Atualizar (F5)."; }
    return result;
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
    PdfRenderer pdf;
    for (;;) {
        ThumbnailSource source; quint64 generation;
        {
            QMutexLocker lock(&m_mutex);
            while (!m_stopping && (m_pending.isEmpty() || m_result)) m_wake.wait(&m_mutex);
            if (m_stopping) return;
            source = m_pending.takeFirst(); generation = m_generation; m_busy = source; m_busyGeneration = generation;
        }
        const auto stale = [this,generation] { QMutexLocker lock(&m_mutex); return m_stopping || m_generation != generation; };
        ThumbnailResult result;
        try { result = load(source,generation,pdf,stale); }
        catch (const std::exception &) { result.source = source; result.generation = generation; result.error = "Falha ao preparar miniatura."; pdf.clear(); }
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
