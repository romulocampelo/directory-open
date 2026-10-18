#pragma once
#include <QThread>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>
#include <optional>
#include "renameidentity.h"

enum class ArchiveAction { List, Test, Create, Extract };
enum class ArchiveFormat { Zip, SevenZip, Rar, Rar5 };
enum class ArchiveState { Completed, Cancelled, Failed, Incomplete };
struct ArchiveEntry {
    QString path, modified;
    quint64 size = 0;
    bool directory = false, encrypted = false;
};
struct ArchiveRequest {
    ArchiveAction action = ArchiveAction::List;
    ArchiveFormat format = ArchiveFormat::Zip;
    QString archive, output;
    QStringList sources;
    std::optional<RenameIdentity> expectedIdentity;
};
struct ArchiveResult {
    ArchiveState state = ArchiveState::Failed;
    QVector<ArchiveEntry> entries;
    quint64 totalBytes = 0;
    int completedFiles = 0;
    QString output, message, diagnostics;
    std::optional<RenameIdentity> identity;
};
using ArchiveProgress = std::function<void(int, int, const QString &)>;
QString sevenZipExecutable();
bool supportedArchive(const QString &path);
QString validateArchiveRequest(const ArchiveRequest &request);
// Sole owner of the technical listing representation and safe Windows paths.
ArchiveResult parseArchiveListing(const QByteArray &bytes);
ArchiveResult performArchive(const ArchiveRequest &request, const std::atomic_bool &cancel, const ArchiveProgress &progress = {});
class ArchiveJob final : public QThread {
    Q_OBJECT
public:
    explicit ArchiveJob(ArchiveRequest request, QObject *parent = nullptr) : QThread(parent), m_request(std::move(request)) {}
    void cancel() { m_cancel = true; }
    const ArchiveResult &result() const { return m_result; } // after wait()
signals:
    void progress(int completed, int total, const QString &path);
protected:
    void run() override;
private:
    ArchiveRequest m_request;
    std::atomic_bool m_cancel{false};
    ArchiveResult m_result;
};
