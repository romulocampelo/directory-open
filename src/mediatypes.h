#pragma once
#include <QFileInfo>
#include <QSet>
enum class MediaKind { None, Audio, Video };
inline MediaKind mediaKindForPath(const QString &path) {
    static const QSet<QString> audio{"mp3","wav","flac","ogg","opus","aac","m4a","wma"};
    static const QSet<QString> video{"mp4","m4v","mov","mkv","webm","avi","wmv","mpg","mpeg","ts","3gp"};
    const auto suffix = QFileInfo(path).suffix().toLower();
    return video.contains(suffix) ? MediaKind::Video : audio.contains(suffix) ? MediaKind::Audio : MediaKind::None;
}
