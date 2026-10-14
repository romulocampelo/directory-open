#pragma once
#include <QByteArray>
#include <QImage>
#include <QString>

inline constexpr qint64 maxImageFileBytes = 64 * 1024 * 1024;
struct DecodedImage { QImage image; QSize originalSize; QString error; };
// Owns source dimension checks, decoder scaling and EXIF orientation for both
// consumers. edge bounds the retained bitmap; decoding scratch is Qt-owned.
DecodedImage decodeImagePreview(const QByteArray &bytes, int edge);
