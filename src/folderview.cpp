#include "folderview.h"
#include <QDir>
#include <QJsonObject>
#include <QSet>

namespace {
constexpr int maxFolders = 256;
bool integer(const QJsonValue &value, int minimum, int maximum) {
    return value.isDouble() && value.toDouble() == value.toInt(-1)
        && value.toInt(-1) >= minimum && value.toInt(-1) <= maximum;
}
}

QString folderViewKey(const QString &path) {
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}
QString validateFolderViews(const QVector<FolderViewEntry> &entries) {
    if (entries.size() > maxFolders) return "Você pode guardar exibições de até 256 pastas. Esqueça uma delas antes de guardar outra.";
    QSet<QString> paths;
    for (const auto &entry : entries) {
        if (!QDir::isAbsolutePath(entry.path) || entry.path.size() > 32768 || entry.path.contains(QChar(0)))
            return "Uma exibição de pasta contém um caminho inválido.";
        const auto key = folderViewKey(entry.path);
        if (paths.contains(key)) return "Uma pasta tem mais de uma exibição guardada.";
        paths.insert(key);
        const auto &view = entry.view;
        if (view.thumbnailSize != 96 && view.thumbnailSize != 144 && view.thumbnailSize != 192 && view.thumbnailSize != 256)
            return "O tamanho de miniaturas é inválido.";
        if (view.sortColumn < 0 || view.sortColumn > 3) return "A coluna de ordenação é inválida.";
        QSet<int> columns;
        for (int i = 0; i < 3; ++i) {
            if (view.order[i] < 1 || view.order[i] > 3 || columns.contains(view.order[i])) return "A ordem das colunas é inválida.";
            columns.insert(view.order[i]);
            if (view.widths[i] < 32 || view.widths[i] > 8192) return "A largura de uma coluna deve estar entre 32 e 8192 pixels.";
        }
    }
    return {};
}
std::optional<QVector<FolderViewEntry>> decodeFolderViews(const QJsonValue &value) {
    if (!value.isArray()) return std::nullopt;
    const auto array = value.toArray();
    if (array.size() > maxFolders) return std::nullopt;
    QVector<FolderViewEntry> entries;
    for (const auto &item : array) {
        if (!item.isObject()) return std::nullopt;
        const auto object = item.toObject();
        if (!object["path"].isString() || !integer(object["sortColumn"], 0, 3)
            || !object["descending"].isBool()) return std::nullopt;
        FolderView view; view.sortColumn = object["sortColumn"].toInt(); view.descending = object["descending"].toBool();
        if (object.contains("thumbnails")) {
            if (!object["thumbnails"].isBool()) return std::nullopt;
            view.thumbnails = object["thumbnails"].toBool();
        }
        if (object.contains("thumbnailSize")) {
            if (!integer(object["thumbnailSize"], 96, 256)) return std::nullopt;
            view.thumbnailSize = object["thumbnailSize"].toInt();
        }
        for (const auto &key : {"order", "widths", "visible"})
            if (!object[key].isArray() || object[key].toArray().size() != 3) return std::nullopt;
        const auto order = object["order"].toArray(), widths = object["widths"].toArray(), visible = object["visible"].toArray();
        for (int i = 0; i < 3; ++i) {
            if (!integer(order[i], 1, 3) || !integer(widths[i], 32, 8192) || !visible[i].isBool()) return std::nullopt;
            view.order[i] = order[i].toInt(); view.widths[i] = widths[i].toInt(); view.visible[i] = visible[i].toBool();
        }
        entries.append({object["path"].toString(), view});
    }
    if (!validateFolderViews(entries).isEmpty()) return std::nullopt;
    return entries;
}
QJsonArray encodeFolderViews(const QVector<FolderViewEntry> &entries) {
    QJsonArray array;
    for (const auto &entry : entries) {
        QJsonArray order, widths, visible;
        for (int i = 0; i < 3; ++i) { order.append(entry.view.order[i]); widths.append(entry.view.widths[i]); visible.append(entry.view.visible[i]); }
        array.append(QJsonObject{{"path", entry.path}, {"sortColumn", entry.view.sortColumn}, {"descending", entry.view.descending},
            {"order", order}, {"widths", widths}, {"visible", visible}, {"thumbnails", entry.view.thumbnails}, {"thumbnailSize", entry.view.thumbnailSize}});
    }
    return array;
}
std::optional<FolderView> FolderViewCatalog::find(const QString &path) const {
    const auto key = folderViewKey(path);
    const auto index = m_indices.constFind(key);
    return index == m_indices.cend() ? std::nullopt : std::optional<FolderView>{m_entries[index.value()].view};
}
QString FolderViewCatalog::replace(const QVector<FolderViewEntry> &entries) {
    const auto error = validateFolderViews(entries); if (!error.isEmpty()) return error;
    m_entries = entries; m_indices.clear();
    for (qsizetype i = 0; i < m_entries.size(); ++i) m_indices.insert(folderViewKey(m_entries[i].path), i);
    emit changed(); return {};
}
QString FolderViewCatalog::remember(const QString &path, const FolderView &view) {
    auto updated = m_entries; const auto key = folderViewKey(path);
    const auto index = m_indices.constFind(key);
    if (index != m_indices.cend()) {
        updated[index.value()].view = view; return replace(updated); // Keep the original reference spelling.
    }
    updated.append({path, view}); return replace(updated);
}
void FolderViewCatalog::forget(const QString &path) {
    const auto key = folderViewKey(path);
    const auto index = m_indices.constFind(key);
    if (index != m_indices.cend()) { auto updated = m_entries; updated.removeAt(index.value()); replace(updated); }
}
