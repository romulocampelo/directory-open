#pragma once
#include "fileoperation.h"
#include "renamerecipe.h"
#include <QDateTime>
#include <atomic>

enum class RenameDateSource { Modified, Created, Fixed };
struct BatchRenameOptions {
    QString pattern = "{name}_{n}", find, replacement;
    bool preserveExtension = true, caseSensitive = true;
    int start = 1, step = 1, digits = 3;
    RenameDateSource dateSource = RenameDateSource::Modified;
    QDate fixedDate = QDate::currentDate();
    QString dateFormat = "yyyy-MM-dd";
    RenameRecipe recipe;
};
struct BatchRenameItem {
    QString source, target, name, error;
    RenameIdentity identity;
    bool unchanged = false;
};
struct BatchRenamePlan {
    QVector<BatchRenameItem> items;
    QString error;
    bool cancelled = false;
    bool valid() const;
    int changes() const;
};
enum class RenameProbeState { Present, Missing, Failed };
struct RenameProbe {
    RenameProbeState state = RenameProbeState::Failed;
    RenameIdentity identity;
    QString actualName, error;
};
RenameProbe inspectRenameItem(const QString &path);
BatchRenamePlan planBatchRename(const QStringList &sources, const BatchRenameOptions &, const std::atomic_bool &cancel);
OperationResult performBatchRename(const BatchRenamePlan &, const std::atomic_bool &cancel, const OperationProgress &progress = {});
class BatchRenameJob final : public QThread {
    Q_OBJECT
public:
    BatchRenameJob(QStringList sources, BatchRenameOptions options, QObject *parent = nullptr);
    BatchRenameJob(BatchRenamePlan plan, QObject *parent = nullptr, QString historyRoot = {});
    ~BatchRenameJob() override;
    void cancel() { m_cancel = true; }
    const BatchRenamePlan &plan() const { return m_plan; } // After wait only.
    const OperationResult &result() const { return m_result; }
    int done() const { return m_done.load(); }
protected:
    void run() override;
private:
    bool m_apply = false;
    QString m_historyRoot;
    QStringList m_sources;
    BatchRenameOptions m_options;
    BatchRenamePlan m_plan;
    OperationResult m_result;
    std::atomic_bool m_cancel{false};
    std::atomic_int m_done{0};
};
