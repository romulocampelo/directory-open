#pragma once
#include "filesearch.h"
#include <QRegularExpression>
#include <variant>

// Owns input validation, scope normalization and filename matching for both engines.
struct PreparedSearch {
    QString root;
    QRegularExpression namePattern;
    std::optional<QString> literalName;
    bool matchPath = false;
    FolderRules filters;
    FolderRuleMatcher filterMatcher;
    bool contains(const QString &path) const;
    bool matches(const QString &name) const { return namePattern.match(name).hasMatch(); }
    bool matchesPath(const QString &path) const;
    std::variant<bool, SearchFailure> accepts(const SearchEntry &entry) const;
};
using SearchPreparation = std::variant<PreparedSearch, SearchFailure>;
SearchPreparation prepareSearch(const SearchRequest &request);
