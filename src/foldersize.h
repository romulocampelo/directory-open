#pragma once
#include <QDateTime>
#include <QMutex>
#include <QThread>
#include <QStringList>
#include <QList>
#include <atomic>
#include <functional>

enum class FolderSizeState { Running, Complete, Partial, Cancelled, Limited };
enum class FolderListingState { NotRequested, Running, Complete, Partial, Cancelled, Limited };
struct FolderSizeRequest {
    QStringList roots;
    quint64 maxEntries = 10000000;
    int maxDepth = 256;
    qint64 maxElapsedMs = 600000;
    bool collectListing = false;
    quint64 maxListingEntries = 10000;
    quint64 maxListingBytes = 8 * 1024 * 1024;
};
struct FolderListingEntry {
    QString path;
    bool directory = false;
    quint64 bytes = 0; // Only files have a logical size; directories have no collected subtotal.
};
struct FolderSizeResult {
    FolderSizeState state = FolderSizeState::Running;
    quint64 bytes = 0, examined = 0, files = 0, folders = 0, errors = 0, skippedLinks = 0, overlaps = 0, changes = 0;
    QStringList roots, details;
    QString currentPath, limitReason;
    QDateTime started, finished;
    qint64 elapsedMs = 0;
    bool overflow = false;
    FolderSizeRequest request; // Caller selection and applied limits travel with the facts.
    FolderListingState listingState = FolderListingState::NotRequested;
    QList<FolderListingEntry> listing;
    quint64 listingObserved = 0, listingBytes = 0;
    QString listingLimitReason;
};
QString folderSizeStateText(FolderSizeState);
QString folderListingStateText(FolderListingState);
using FolderSizeUpdate = std::function<void(const FolderSizeResult &)>;
// Checked arithmetic leaves the observed subtotal intact on overflow.
bool addLogicalBytes(quint64 &subtotal, quint64 bytes);
FolderSizeResult calculateFolderSize(const FolderSizeRequest &, const std::atomic_bool &, const FolderSizeUpdate & = {});

// Sole retained result owner. UI polls bounded snapshots; no growing signal queue.
class FolderSizeJob final : public QThread {
public:
    explicit FolderSizeJob(FolderSizeRequest request, QObject *parent = nullptr);
    void cancel() { m_cancel = true; }
    FolderSizeResult result() const;
protected:
    void run() override;
private:
    FolderSizeRequest m_request;
    std::atomic_bool m_cancel{false};
    mutable QMutex m_mutex;
    FolderSizeResult m_result;
};
