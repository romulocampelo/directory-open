#pragma once
#include <QProcessEnvironment>
#include <QThread>
#include <QStringList>
#include <atomic>
#include <functional>

// Only non-secret preferences belong in the session. Passwords live in a request.
struct ToolSettings { QString restic, rclone, ripgrep, rcloneConfig, repository, exiftool, magick, ffmpeg, ffprobe, duplicateFinderProject, python, scriptManifest; };
enum class ToolAction { ContentSearch, InitBackup, Snapshots, Backup, Restore, Remotes, RemoteList, TransferPreview, TransferCopy };
enum class ToolState { Completed, Failed, Incomplete, Cancelled };
struct ToolRequest {
    ToolAction action = ToolAction::ContentSearch;
    QString executable, root, pattern, repository, password, config, source, destination, snapshot;
    QStringList sources;
    bool regex = false, caseSensitive = false, hidden = false, ignored = false;
};
struct ToolRow { QString key, detail, text; qint64 number = 0; bool directory = false; };
struct ToolResult {
    ToolState state = ToolState::Failed;
    QVector<ToolRow> rows;
    QString message, diagnostics, output;
    qint64 elapsedMs = 0;
};
QString discoverTool(const QString &name, const QString &configured = {});
QString validateToolRequest(const ToolRequest &request);
// Owns argument construction and protocol interpretation; callers consume typed facts.
ToolResult performTool(const ToolRequest &, const std::atomic_bool &cancel,
                       const std::function<void(QString)> &progress = {});
class ToolJob final : public QThread {
    Q_OBJECT
public:
    explicit ToolJob(ToolRequest request, QObject *parent = nullptr);
    ~ToolJob() override;
    void cancel() { m_cancel.store(true); }
    ToolResult result() const { return m_result; } // after finished/wait
signals:
    void progress(QString message);
protected:
    void run() override;
private:
    ToolRequest m_request;
    ToolResult m_result;
    std::atomic_bool m_cancel{false};
};
