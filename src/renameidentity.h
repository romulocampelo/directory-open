#pragma once
#include <QtGlobal>
struct RenameIdentity {
    quint32 volume = 0, indexHigh = 0, indexLow = 0, attributes = 0;
    quint64 bytes = 0, modified = 0, created = 0;
    bool operator==(const RenameIdentity &) const = default;
};
