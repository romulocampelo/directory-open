#pragma once
#include <QByteArray>
#include <QString>
#include <optional>
// Zone.Identifier is opaque provenance, not a display field or trusted policy.
QString readFileOrigin(const QString &, std::optional<QByteArray> &);
QString writeFileOrigin(const QString &, const std::optional<QByteArray> &);
