#pragma once
#include "fileoperation.h"
#include <QMimeData>
#include <QObject>
#include <memory>

struct FileTransfer {
    QStringList paths;
    OperationKind kind = OperationKind::Copy;
    QString token;
    QString error;
    bool valid() const { return error.isEmpty() && !paths.isEmpty(); }
};
Q_DECLARE_METATYPE(FileTransfer)
QString preferredDropEffectFormat();
FileTransfer readFileTransfer(const QMimeData *mime);
std::unique_ptr<QMimeData> makeFileTransfer(const QStringList &paths, OperationKind kind);

// Drag data is process-owned and never advertises filesystem URLs to other apps.
// Only our receiver performs moves; Qt's source model never deletes rows.
class FileDragData final : public QMimeData {
public:
    explicit FileDragData(const QStringList &paths);
    const FileTransfer &transfer() const { return m_transfer; }
private:
    FileTransfer m_transfer;
};
struct ClipboardSnapshot {
    FileTransfer transfer;
    quint64 revision = 0;
    quint32 sequence = 0;
};
class FileClipboard final : public QObject {
    Q_OBJECT
public:
    explicit FileClipboard(QObject *parent = nullptr);
    ClipboardSnapshot snapshot();
    QString publish(const QStringList &paths, OperationKind kind);
    QString finishMove(const ClipboardSnapshot &snapshot, const OperationResult &result);
signals:
    void changed();
private:
    quint64 m_revision = 0;
    bool m_cached = false;
    bool m_notificationPending = false;
    ClipboardSnapshot m_snapshot;
};
