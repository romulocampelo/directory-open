#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <optional>
#include "folderview.h"
#include "specializedtools.h"
#include "uicustomization.h"
#include "workspace.h"

struct FavoriteEntry { QString id, label, path; };
QString validateFavorites(const QVector<FavoriteEntry> &favorites);

struct WindowSession {
    PanelSession left, right;
    QByteArray geometry, windowState, splitterState;
    bool dark = false, dualPane = true, treeVisible = true, previewVisible = false;
    bool nativeContextMenu = true;
    bool teraCopyForCopy = false;
    bool independentTrees = false;
    ToolSettings tools;
    UiCustomization ui;
    int activePanel = 0;
    QVector<SavedWorkspace> layouts;
    QVector<SavedTabGroup> tabGroups;
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
