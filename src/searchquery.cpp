#include "searchquery.h"
#include "windowspath.h"
#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <limits>
#include <windows.h>

bool PreparedSearch::contains(const QString &path) const {
    const auto prefix = root.endsWith('/') ? root : root + '/';
    return path.startsWith(prefix, Qt::CaseInsensitive) && path.size() > prefix.size();
}
bool PreparedSearch::matchesPath(const QString &path) const {
    return matches(matchPath ? QDir::toNativeSeparators(path) : QFileInfo(path).fileName());
}
std::variant<bool, SearchFailure> PreparedSearch::accepts(const SearchEntry &entry) const {
    if (filters == FolderRules{}) return true;
    WIN32_FILE_ATTRIBUTE_DATA info{};
    const auto native = WindowsPath::forFileApi(entry.path).toStdWString();
    if (!GetFileAttributesExW(native.c_str(), GetFileExInfoStandard, &info)
        || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        || bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != entry.directory)
        return SearchFailure{SearchErrorCode::Access, "Metadados indisponíveis ou item alterado durante a filtragem: " + entry.path};
    const quint64 ticks = (quint64(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime;
    const auto date = QDateTime::fromMSecsSinceEpoch(qint64(ticks / 10000) - 11644473600000LL).toLocalTime().date();
    const quint64 bytes = (quint64(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    if (!entry.directory && ((bytes > quint64(std::numeric_limits<qint64>::max()))
        || ((!date.isValid()) && (filters.modifiedFrom.isValid() || filters.modifiedTo.isValid()))))
        return SearchFailure{SearchErrorCode::Access, "Metadados fora do intervalo suportado: " + entry.path};
    return filterMatcher.matches(FolderRuleFacts{QFileInfo(entry.path).fileName(), entry.directory,
        bool(info.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN), qint64(bytes), date});
}
SearchPreparation prepareSearch(const SearchRequest &r) {
    if (r.name.trimmed().isEmpty() || r.name.size() > 1024 || r.name.contains(QChar(0))
        || !QDir::isAbsolutePath(r.root) || r.root.contains(QChar(0)) || r.root.contains('"')
        || r.maxResults < 1 || r.maxResults > 10000 || r.maxVisited < 1 || r.maxVisited > 1000000
        || r.timeoutMs < 1 || r.timeoutMs > 30000 || r.everythingInstance.size() > 128 || r.everythingInstance.contains(QChar(0))
        || (r.engine != SearchEngine::Direct && r.engine != SearchEngine::Everything && r.engine != SearchEngine::Automatic))
        return SearchFailure{SearchErrorCode::InvalidRequest, "Escolha uma pasta e um nome válidos para buscar."};
    const auto root = QDir::cleanPath(WindowsPath::fromFileApi(QDir::toNativeSeparators(r.root)));
    const auto native = WindowsPath::forFileApi(root).toStdWString();
    const DWORD attributes = GetFileAttributesW(native.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return SearchFailure{SearchErrorCode::Access, "A pasta não pôde ser acessada."};
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
        return SearchFailure{SearchErrorCode::Access, "Escolha a pasta de destino do link para buscar."};
    const auto filterError = validateFolderRules(r.filters);
    if (!filterError.isEmpty()) return SearchFailure{SearchErrorCode::InvalidRequest, filterError};
    const auto name = r.matchPath ? QDir::toNativeSeparators(QDir::fromNativeSeparators(r.name)) : r.name;
    const auto pattern = r.wildcard ? QRegularExpression::wildcardToRegularExpression(name,
        QRegularExpression::NonPathWildcardConversion) : QRegularExpression::escape(name);
    QRegularExpression namePattern(pattern, QRegularExpression::CaseInsensitiveOption);
    if (!namePattern.isValid()) return SearchFailure{SearchErrorCode::InvalidRequest, "Padrão de nome inválido."};
    PreparedSearch prepared; prepared.root = root; prepared.namePattern = namePattern;
    prepared.literalName = r.wildcard ? std::nullopt : std::optional<QString>(name);
    prepared.matchPath = r.matchPath; prepared.filters = r.filters; prepared.filterMatcher.setRules(r.filters);
    return prepared;
}
