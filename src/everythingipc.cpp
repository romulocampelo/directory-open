#include "everythingipc.h"
#include "windowspath.h"
#include <QDir>
#include <QtEndian>

namespace EverythingIpc {
PageResult decodePage(QByteArrayView bytes, quint32 expectedOffset, quint32 capacity) {
    const auto invalid = [] { return SearchFailure{SearchErrorCode::Protocol, "O Everything enviou uma resposta inválida ou incompatível."}; };
    if (bytes.size() < 20 || bytes.size() > MaxPageBytes || capacity == 0 || capacity > PageSize) return invalid();
    const auto read = [&](qsizetype at) { return qFromLittleEndian<quint32>(bytes.data() + at); };
    const auto total = read(0), count = read(4), offset = read(8), flags = read(12), sort = read(16);
    if (offset != expectedOffset || flags != FullPath || sort != NameAscending || count > capacity
        || offset > total || count > total - offset || (count == 0 && offset < total)
        || bytes.size() < 20 + qsizetype(count) * 8) return invalid();
    Page page; page.total = total; page.offset = offset; page.entries.reserve(count);
    const auto dataStart = 20 + qsizetype(count) * 8;
    for (quint32 i = 0; i < count; ++i) {
        const auto itemFlags = read(20 + qsizetype(i) * 8), data = read(24 + qsizetype(i) * 8);
        if (itemFlags & ~quint32(3) || data < quint32(dataStart) || quint64(data) + 4 > quint64(bytes.size())) return invalid();
        const auto length = read(data);
        const quint64 end = quint64(data) + 4 + (quint64(length) + 1) * 2;
        if (length == 0 || length > MaxPathChars || end > quint64(bytes.size())) return invalid();
        QString path; path.resize(length);
        for (quint32 c = 0; c < length; ++c) {
            const auto value = qFromLittleEndian<quint16>(bytes.data() + data + 4 + qsizetype(c) * 2);
            if (!value) return invalid();
            path[qsizetype(c)] = QChar(value);
        }
        if (qFromLittleEndian<quint16>(bytes.data() + qsizetype(end) - 2) != 0) return invalid();
        path = WindowsPath::fromFileApi(path);
        if (!QDir::isAbsolutePath(path) || path.startsWith("//./") || path.contains("/../") || path.contains("/./")) return invalid();
        page.entries.append({path, bool(itemFlags & 1)});
    }
    return page;
}
}
