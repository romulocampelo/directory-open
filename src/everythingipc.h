#pragma once
#include "filesearch.h"
#include <QByteArrayView>
#include <variant>

// Public Query2W wire contract: https://www.voidtools.com/support/everything/sdk/ipc/
// Definitions verified against the official Everything-SDK.zip ipc/everything_ipc.h.
// Own declarations; no SDK library, binary or source is incorporated.
namespace EverythingIpc {
constexpr quint32 Query2W = 18, FullPath = 4, NameAscending = 1;
constexpr quint32 MaxPathChars = 32767;
// Derive capacity before asking the external process to allocate its reply.
// A 64 MiB transport budget includes maximum-length UTF-16 paths and wire metadata.
constexpr qsizetype MaxPageBytes = 64 * 1024 * 1024;
constexpr quint32 PageSize = (MaxPageBytes - 20) / (8 + 4 + (MaxPathChars + 1) * 2);
struct Query { quint32 replyWindow, replyId, flags, offset, maxResults, requestFlags, sort; };
static_assert(sizeof(Query) == 28);
struct Page { quint32 total = 0, offset = 0; QVector<SearchEntry> entries; };
using PageResult = std::variant<Page, SearchFailure>;
PageResult decodePage(QByteArrayView bytes, quint32 offset, quint32 capacity);
}
