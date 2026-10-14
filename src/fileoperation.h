#pragma once
#include <QThread>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>

enum class OperationConflict { Ask, KeepBoth };
enum class OperationKind { Copy, Move, Rename, CreateFolder, Trash };
enum class OperationState { Completed, Cancelled, Failed, Incomplete };
enum class OperationItemState { NotStarted, Completed, Skipped, Cancelled, Failed, Incomplete };
struct OperationRequest {
    QStringList sources;
    QString destination;
    OperationConflict conflict = OperationConflict::Ask;
    bool nativeUi = true;
    quintptr ownerWindow = 0;
    OperationKind kind = OperationKind::Copy;
    QString name;
};
struct OperationItemResult {
    QString source;
    QString target;
    OperationItemState state = OperationItemState::NotStarted;
    QString error;
    QString recycledParsingName;
};
struct OperationResult {
    OperationState state = OperationState::Failed;
    QVector<OperationItemResult> items;
    QString error;
    int completedCount() const;
};
using OperationProgress = std::function<void(quint32, quint32, const QString &)>;
QString operationLabel(OperationKind kind);
QString validateItemName(const QString &name);
QString validateOperationRequest(const OperationRequest &request);
OperationResult performOperation(const OperationRequest &request, const std::atomic_bool &cancel, const OperationProgress &progress = {});

class OperationJob final : public QThread {
    Q_OBJECT
public:
    explicit OperationJob(OperationRequest request, QObject *parent = nullptr);
    void cancel() { m_cancel.store(true); }
    const OperationResult &result() const { return m_result; } // Read only after wait().
signals:
    void progress(quint32 total, quint32 done, const QString &item);
protected:
    void run() override;
private:
    OperationRequest m_request;
    std::atomic_bool m_cancel{false};
    OperationResult m_result;
};
