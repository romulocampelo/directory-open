#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <optional>
#include "folderview.h"

struct FavoriteEntry { QString id, label, path; };
QString validateFavorites(const QVector<FavoriteEntry> &favorites);

struct TabSession { QString path; QString filter; };
struct PanelSession {
    QVector<TabSession> tabs;
    int currentTab = 0;
    int sortColumn = 0;
    bool descending = false;
};
struct WindowSession {
    PanelSession left, right;
    QByteArray geometry, windowState, splitterState;
    bool dark = false, dualPane = true, treeVisible = true, previewVisible = false;
    int activePanel = 0;
    // Omission identifies pre-favorites sessions; an empty array stays empty.
    std::optional<QVector<FavoriteEntry>> favorites;
    std::optional<QVector<FolderViewEntry>> folderViews;
};
enum class SessionLoadState { Missing, Loaded, Recovered, Invalid, Unsupported };
struct SessionLoadResult {
    SessionLoadState state = SessionLoadState::Missing;
    WindowSession session;
    QString message;
};

// Owns parsing, backup policy, and the identity of the last read/written record.
class SessionStore final {
public:
    explicit SessionStore(QString path);
    static QString defaultPath();
    SessionLoadResult load();
    QString save(const WindowSession &session);
    bool writable() const { return m_writable; }
private:
    QString m_path;
    QByteArray m_baseline;
    bool m_existed = false, m_writable = true, m_loaded = false;
    QJsonObject m_document;
};
