#pragma once
#include <QObject>
#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QString>
#include <QVector>
#include <array>
#include <optional>

// Logical column 0 (name) is always visible, first, and stretches to fit.
// Table mode is derived from column visibility. Thumbnails select another presentation.
struct FolderView {
    bool thumbnails = false;
    int thumbnailSize = 144;
    int sortColumn = 0;
    bool descending = false;
    std::array<int, 3> order{1, 2, 3};
    std::array<int, 3> widths{90, 120, 145};
    std::array<bool, 3> visible{true, true, true};
    bool operator==(const FolderView &) const = default;
};
struct FolderViewEntry { QString path; FolderView view; };

QString folderViewKey(const QString &path);
QString validateFolderViews(const QVector<FolderViewEntry> &entries);
std::optional<QVector<FolderViewEntry>> decodeFolderViews(const QJsonValue &value);
QJsonArray encodeFolderViews(const QVector<FolderViewEntry> &entries);

// Shared by the window's panels. Never probes folders while loading preferences:
// unavailable references survive, and no filesystem case-folding is assumed.
class FolderViewCatalog final : public QObject {
    Q_OBJECT
public:
    explicit FolderViewCatalog(QObject *parent = nullptr) : QObject(parent) {}
    const QVector<FolderViewEntry> &entries() const { return m_entries; }
    std::optional<FolderView> find(const QString &path) const;
    QString replace(const QVector<FolderViewEntry> &entries);
    QString remember(const QString &path, const FolderView &view);
    void forget(const QString &path);
signals:
    void changed();
private:
    QVector<FolderViewEntry> m_entries;
    QHash<QString, qsizetype> m_indices;
};
