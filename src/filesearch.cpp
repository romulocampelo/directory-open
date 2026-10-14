#include "filesearch.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QRegularExpression>
#include <filesystem>
#include <vector>
#include <windows.h>

namespace {
namespace fs = std::filesystem;
QString displayPath(const fs::path &path) { return QDir::fromNativeSeparators(QString::fromStdWString(path.native())); }
bool isReparsePoint(const fs::path &path, std::error_code &error) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) { error = std::error_code(int(GetLastError()), std::system_category()); return false; }
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}
}

SearchSummary searchFiles(const SearchRequest &request, const std::atomic_bool &cancel, const SearchUpdate &update) {
    SearchSummary summary;
    auto recordError = [&](const QString &path, const QString &message) {
        ++summary.errors;
        if (summary.details.size() < 20) summary.details.append(QDir::toNativeSeparators(path) + ": " + message);
    };
    if (request.name.trimmed().isEmpty() || request.name.size() > 1024 || request.name.contains(QChar(0))
        || !QDir::isAbsolutePath(request.root) || request.root.contains(QChar(0))
        || request.maxResults < 1 || request.maxResults > 10000 || request.maxVisited < 1 || request.maxVisited > 1000000) {
        recordError(request.root, "Escolha uma pasta e um nome para buscar."); return summary;
    }
    if (cancel) { summary.state = SearchState::Cancelled; return summary; }
    const fs::path root(QDir::toNativeSeparators(request.root).toStdWString());
    std::error_code error;
    const bool rootLink = isReparsePoint(root, error);
    if (error || rootLink || !fs::is_directory(root, error) || error) {
        recordError(request.root, rootLink ? "Escolha a pasta de destino do link para buscar." : "A pasta não pôde ser acessada."); return summary;
    }
    const QRegularExpression pattern(request.wildcard ? QRegularExpression::wildcardToRegularExpression(request.name) : QString{}, QRegularExpression::CaseInsensitiveOption);
    if (request.wildcard && !pattern.isValid()) { recordError(request.root, "Padrão de nome inválido."); return summary; }
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
                const bool matched = request.wildcard ? pattern.match(name).hasMatch() : name.contains(request.name, Qt::CaseInsensitive);
                if (matched) { batch.append({displayPath(path), directory}); ++summary.matches; }
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

SearchJob::SearchJob(SearchRequest request, QObject *parent) : QThread(parent), m_request(std::move(request)) {
    qRegisterMetaType<QVector<SearchEntry>>(); qRegisterMetaType<SearchSummary>();
}
void SearchJob::run() {
    m_summary = searchFiles(m_request, m_cancel, [this](const QVector<SearchEntry> &entries, const SearchSummary &summary) { emit updated(entries, summary); });
}
