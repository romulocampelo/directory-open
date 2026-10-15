#pragma once
#include <QDir>
#include <QString>

// Filesystem API paths and user-facing paths have distinct representations.
// Callers validate absolute input first. Extended paths avoid MAX_PATH regardless
// of the host's long-path policy; remove only our transport prefix for display.
namespace WindowsPath {
inline QString forFileApi(const QString &absolutePath) {
    const auto native = QDir::toNativeSeparators(QDir::cleanPath(QDir::fromNativeSeparators(absolutePath)));
    if (native.startsWith("\\\\?\\") || native.startsWith("\\\\.\\")) return native;
    return native.startsWith("\\\\") ? "\\\\?\\UNC\\" + native.mid(2) : "\\\\?\\" + native;
}
inline QString fromFileApi(QString path) {
    if (path.startsWith("\\\\?\\UNC\\")) path = "\\\\" + path.mid(8);
    else if (path.startsWith("\\\\?\\")) path = path.mid(4);
    return QDir::fromNativeSeparators(path);
}
}
