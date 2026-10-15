#pragma once
#include "batchrename.h"
#include <QByteArray>
#include <QDateTime>

enum class UndoRenameState { Ready, AlreadyOriginal, Unchanged, Blocked };
struct UndoRenameItem {
    int index = -1;
    QString original, current, detail;
    UndoRenameState state = UndoRenameState::Blocked;
    BatchRenameItem reverse;
};
struct RenameHistorySummary {
    QString id, folder, phase, error;
    QDateTime created;
    int count = 0;
};
struct UndoRenamePreview {
    QString id, error;
    QByteArray baseline;
    QVector<UndoRenameItem> items;
};
struct RenameHistoryReport {
    QVector<RenameHistorySummary> records;
    UndoRenamePreview preview;
    OperationResult operation;
    QString error;
};
enum class RenameHistoryMode { List, Preview, Undo };
struct RenameHistoryRequest {
    RenameHistoryMode mode = RenameHistoryMode::List;
    QString id;
    QByteArray baseline;
    QVector<int> selected;
};
OperationResult performRecordedBatchRename(const BatchRenamePlan &, const QString &root,
    const std::atomic_bool &cancel, const OperationProgress &progress = {});
RenameHistoryReport runRenameHistory(const QString &root, const RenameHistoryRequest &,
    const std::atomic_bool &cancel, const OperationProgress &progress = {});

class RenameHistoryJob final : public QThread {
    Q_OBJECT
public:
    RenameHistoryJob(QString root, RenameHistoryRequest request, QObject *parent = nullptr);
    ~RenameHistoryJob() override;
    void cancel() { m_cancel = true; }
    const RenameHistoryReport &report() const { return m_report; } // After wait only.
protected:
    void run() override;
private:
    QString m_root;
    RenameHistoryRequest m_request;
    RenameHistoryReport m_report;
    std::atomic_bool m_cancel{false};
};
