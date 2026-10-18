#pragma once
#include "specializedtools.h"
#include "renameidentity.h"
#include <QMap>
#include <QImage>
enum class MediaTask { Metadata, Images, AudioVideo };
struct MediaOptions {
    MediaTask task = MediaTask::Metadata;
    QString executable, probeExecutable, destination, format = "png";
    QMap<QString, QString> tags;
    int width = 0, height = 0, rotation = 0, quality = 90;
};
struct MediaItem { QString source, output, details; RenameIdentity identity; QImage preview; };
struct MediaPlan { MediaOptions options; QVector<MediaItem> items; QString error; bool cancelled = false; bool valid() const { return error.isEmpty() && !cancelled && !items.isEmpty(); } };
struct MediaResult { ToolState state = ToolState::Failed; QString message, diagnostics; QStringList outputs; int completed = 0; };
QMap<QString, QString> editableMetadataTags();
QString mediaToolExecutable(const QString &name, const QString &configured = {});
MediaPlan inspectMedia(const QStringList &, const MediaOptions &, const std::atomic_bool &cancel);
MediaResult convertMedia(const MediaPlan &, const std::atomic_bool &, const std::function<void(QString)> &progress = {});
class MediaToolsJob final : public QThread {
    Q_OBJECT
public:
    MediaToolsJob(QStringList sources, MediaOptions options, QObject *parent = nullptr);
    MediaToolsJob(MediaPlan plan, QObject *parent = nullptr);
    ~MediaToolsJob() override;
    void cancel() { m_cancel = true; }
    MediaPlan plan() const { return m_plan; }
    MediaResult result() const { return m_result; }
signals:
    void progress(QString text);
protected:
    void run() override;
private:
    bool m_apply = false;
    QStringList m_sources;
    MediaOptions m_options;
    MediaPlan m_plan;
    MediaResult m_result;
    std::atomic_bool m_cancel{false};
};
