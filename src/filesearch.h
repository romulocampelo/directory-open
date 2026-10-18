#pragma once
#include <QThread>
#include <QString>
#include <QVector>
#include <atomic>
#include <functional>
#include <optional>
#include "folderpresentation.h"

enum class SearchEngine { Direct, Everything, Automatic };
enum class SearchErrorCode { InvalidRequest, Unavailable, NotReady, Unsupported, Timeout, Protocol, Access, Internal };
struct SearchFailure { SearchErrorCode code; QString message; };

struct SearchRequest {
    QString root, name;
    bool wildcard = false;
    int maxResults = 10000;
    quint64 maxVisited = 1000000;
    SearchEngine engine = SearchEngine::Direct;
    QString everythingInstance;
    int timeoutMs = 5000;
    bool matchPath = false;
    FolderRules filters;
};
struct SearchEntry { QString path; bool directory = false; };
enum class SearchState { Completed, Cancelled, Limited, Partial, Failed };
struct SearchSummary {
    SearchState state = SearchState::Failed;
    quint64 visited = 0, matches = 0, skippedLinks = 0, errors = 0;
    QStringList details;
    SearchEngine engine = SearchEngine::Direct;
    QString source;
    QString fallbackReason;
    std::optional<quint64> indexedTotal;
    std::optional<SearchFailure> failure;
};
using SearchUpdate = std::function<void(const QVector<SearchEntry> &, const SearchSummary &)>;
SearchSummary searchFiles(const SearchRequest &, const std::atomic_bool &cancel, const SearchUpdate &update);
Q_DECLARE_METATYPE(SearchEntry)
Q_DECLARE_METATYPE(QVector<SearchEntry>)
Q_DECLARE_METATYPE(SearchSummary)

class SearchJob final : public QThread {
    Q_OBJECT
public:
    explicit SearchJob(SearchRequest request, QObject *parent = nullptr);
    void cancel() { m_cancel = true; }
    SearchSummary summary() const { return m_summary; } // Read after finished/wait.
signals:
    void updated(QVector<SearchEntry> entries, SearchSummary summary);
protected:
    void run() override;
private:
    SearchRequest m_request;
    std::atomic_bool m_cancel{false};
    SearchSummary m_summary;
};
