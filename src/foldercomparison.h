#pragma once
#include <QThread>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>

enum class ComparisonState { Completed, Cancelled, Limited, Partial, Failed };
enum class ComparisonKind { LeftOnly, RightOnly, MetadataMatch, MetadataDifferent, ContentEqual, ContentDifferent, Folders, TypeConflict, IgnoredLink, Unknown };
struct ComparisonRequest {
    QString left, right;
    bool recursive = true;
    bool content = false;
    int maxEntriesPerSide = 5000;
    int maxDepth = 128;
};
struct ComparisonEntry {
    QString relativePath, leftPath, rightPath;
    ComparisonKind kind = ComparisonKind::Unknown;
    QString leftDescription, rightDescription, detail;
};
struct ComparisonReport {
    ComparisonState state = ComparisonState::Failed;
    QVector<ComparisonEntry> entries;
    quint64 examined = 0, bytesRead = 0, errors = 0, skippedLinks = 0;
    QStringList details;
};
using ComparisonProgress = std::function<void(quint64 examined, quint64 bytesRead)>;
ComparisonReport compareFolders(const ComparisonRequest &, const std::atomic_bool &cancel, const ComparisonProgress &progress = {});
QString comparisonLabel(ComparisonKind kind);

class ComparisonJob final : public QThread {
    Q_OBJECT
public:
    explicit ComparisonJob(ComparisonRequest request, QObject *parent = nullptr);
    ~ComparisonJob() override;
    void cancel() { m_cancel = true; }
    quint64 examined() const { return m_examined.load(); }
    quint64 bytesRead() const { return m_bytesRead.load(); }
    ComparisonReport takeReport(); // Only after finished/wait; transfers the sole retained result.
protected:
    void run() override;
private:
    ComparisonRequest m_request;
    std::atomic_bool m_cancel{false};
    std::atomic<quint64> m_examined{0}, m_bytesRead{0};
    ComparisonReport m_report;
};
