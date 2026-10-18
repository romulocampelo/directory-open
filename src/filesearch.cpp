#include "filesearch.h"
#include "windowspath.h"
#include "searchquery.h"
#include "everythingsearch.h"
#include <QDir>
#include <QElapsedTimer>
#include <filesystem>
#include <vector>
#include <windows.h>

namespace {
namespace fs = std::filesystem;
QString displayPath(const fs::path &path) { return WindowsPath::fromFileApi(QString::fromStdWString(path.native())); }
bool isReparsePoint(const fs::path &path, std::error_code &error) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) { error = std::error_code(int(GetLastError()), std::system_category()); return false; }
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}
}

static SearchSummary searchDirect(const SearchRequest &request, const PreparedSearch &prepared, const std::atomic_bool &cancel,
                                 const SearchUpdate &update, const QString &fallbackReason = {}) {
    SearchSummary summary; summary.source = "Busca direta"; summary.fallbackReason = fallbackReason;
    auto recordError = [&](const QString &path, const QString &message) {
        ++summary.errors;
        if (summary.details.size() < 20) summary.details.append(QDir::toNativeSeparators(path) + ": " + message);
    };
    if (cancel) { summary.state = SearchState::Cancelled; return summary; }
    const fs::path root(WindowsPath::forFileApi(prepared.root).toStdWString());
    struct Frame { fs::path path; fs::directory_iterator iterator; };
    std::vector<Frame> stack;
    auto enter = [&](const fs::path &path) {
        std::error_code ec; fs::directory_iterator iterator(path, ec);
        if (ec) { recordError(displayPath(path), QString::fromStdString(ec.message())); return; }
        stack.push_back({path, std::move(iterator)});
    };
    enter(root);
    if (stack.empty()) return summary;
    summary.state = SearchState::Completed;
    QVector<SearchEntry> batch; batch.reserve(100); QElapsedTimer timer; timer.start();
    auto flush = [&] { if (update) update(batch, summary); batch.clear(); timer.restart(); };
    while (!stack.empty()) {
        if (cancel) { summary.state = SearchState::Cancelled; break; }
        auto &frame = stack.back();
        if (frame.iterator == fs::directory_iterator{}) { stack.pop_back(); continue; }
        if (summary.visited >= request.maxVisited) { summary.state = SearchState::Limited; break; }
        // Copy the entry before advancing; a failed enumeration ends only this folder.
        const auto entry = *frame.iterator;
        std::error_code ec; frame.iterator.increment(ec);
        if (ec) { recordError(displayPath(frame.path), QString::fromStdString(ec.message())); frame.iterator = {}; }
        ++summary.visited;
        const auto path = entry.path(); const auto name = QString::fromStdWString(path.filename().native());
        ec.clear(); const bool link = isReparsePoint(path, ec);
        if (ec) recordError(displayPath(path), QString::fromStdString(ec.message()));
        else if (link) ++summary.skippedLinks;
        else {
            const bool directory = entry.is_directory(ec);
            if (ec) recordError(displayPath(path), QString::fromStdString(ec.message()));
            else {
                const auto shown = displayPath(path);
                if (prepared.matches(prepared.matchPath ? QDir::toNativeSeparators(shown) : name)) {
                    const SearchEntry candidate{shown, directory};
                    const auto accepted = prepared.accepts(candidate);
                    if (const auto *error = std::get_if<SearchFailure>(&accepted)) recordError(shown, error->message);
                    else if (std::get<bool>(accepted)) { batch.append(candidate); ++summary.matches; }
                }
                if (summary.matches >= quint64(request.maxResults)) { summary.state = SearchState::Limited; break; }
                if (directory) {
                    if (stack.size() >= 256) recordError(displayPath(path), "Limite de profundidade atingido; esta pasta não foi examinada.");
                    else enter(path);
                }
            }
        }
        if (batch.size() >= 100 || timer.elapsed() >= 100) flush();
    }
    if (summary.state == SearchState::Completed && summary.errors) summary.state = SearchState::Partial;
    flush(); return summary;
}

SearchSummary searchFiles(const SearchRequest &request, const std::atomic_bool &cancel, const SearchUpdate &update) {
    if (cancel) { SearchSummary summary; summary.state = SearchState::Cancelled; return summary; }
    const auto preparation = prepareSearch(request);
    if (const auto *error = std::get_if<SearchFailure>(&preparation)) {
        SearchSummary summary; summary.failure = *error; summary.errors = 1; summary.details = {error->message}; return summary;
    }
    const auto &prepared = std::get<PreparedSearch>(preparation);
    if (request.engine == SearchEngine::Direct) return searchDirect(request, prepared, cancel, update);
    const auto indexed = searchEverything(request, prepared, cancel, update);
    if (request.engine == SearchEngine::Automatic && indexed.failure && indexed.matches == 0
        && (indexed.failure->code == SearchErrorCode::Unavailable || indexed.failure->code == SearchErrorCode::NotReady
            || indexed.failure->code == SearchErrorCode::Unsupported))
        return searchDirect(request, prepared, cancel, update, indexed.failure->message);
    return indexed;
}

SearchJob::SearchJob(SearchRequest request, QObject *parent) : QThread(parent), m_request(std::move(request)) {
    qRegisterMetaType<QVector<SearchEntry>>(); qRegisterMetaType<SearchSummary>();
}
void SearchJob::run() {
    m_summary = searchFiles(m_request, m_cancel, [this](const QVector<SearchEntry> &entries, const SearchSummary &summary) { emit updated(entries, summary); });
}
