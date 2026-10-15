#pragma once
#include <QMainWindow>
#include "sessionstore.h"
#include "filetransfer.h"
#include <optional>
#include <QPointer>
class FilePanel;
class QFileSystemModel;
class QTreeView;
class QDockWidget;
class QLabel;
class PreviewWidget;
class QDialog;
class OperationJob;
enum class OperationKind;
class QAction;
class QProgressBar;
class QCloseEvent;
class QSplitter;
class QMenu;
class QListWidget;
class SearchDialog;
class ComparisonDialog;
class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString &left = {}, const QString &right = {}, bool nativeOperationUi = true, const QString &sessionFile = {});
    ~MainWindow() override;
    FilePanel *leftPanel() const { return m_left; }
    FilePanel *rightPanel() const { return m_right; }
    WindowSession session() const;
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    FilePanel *m_left;
    FilePanel *m_right;
    FilePanel *m_active;
    QFileSystemModel *m_treeModel;
    QTreeView *m_tree;
    QSplitter *m_splitter;
    QWidget *m_sidebar;
    QAction *m_openAction;
    QAction *m_openTabAction;
    QAction *m_copyPathAction;
    QAction *m_refreshAction;
    QAction *m_dualAction;
    QAction *m_treeAction;
    QAction *m_darkAction;
    FileClipboard *m_clipboard;
    QAction *m_clipboardCopyAction = nullptr;
    QAction *m_clipboardCutAction = nullptr;
    QAction *m_pasteAction = nullptr;
    bool m_confirming = false;
    QDockWidget *m_preview = nullptr;
    PreviewWidget *m_previewWidget;
    QPointer<QDialog> m_viewerDialog;
    bool m_closeAfterViewer = false;
    OperationJob *m_operationJob = nullptr;
    QAction *m_copyAction = nullptr;
    QAction *m_moveAction = nullptr;
    QAction *m_renameAction = nullptr;
    QAction *m_batchRenameAction = nullptr;
    void startBatchRename();
    QAction *m_folderAction = nullptr;
    QAction *m_trashAction = nullptr;
    QAction *m_cancelOperation = nullptr;
    QProgressBar *m_operationProgress = nullptr;
    bool m_nativeOperationUi;
    bool m_closeAfterOperation = false;
    SessionStore m_sessionStore;
    FolderViewCatalog *m_folderViews;
    QVector<FavoriteEntry> m_favorites;
    QMenu *m_favoritesMenu = nullptr;
    QListWidget *m_favoritesList = nullptr;
    QPointer<SearchDialog> m_searchDialog;
    void refreshFavorites();
    void openFavorite(const QString &id);
    void addCurrentFavorite();
    void editFavorites();
    QPointer<ComparisonDialog> m_comparisonDialog;
    void showComparison();
    void showSearch();
    void finishClose(QCloseEvent *event);
    void showFileContextMenu(const QPoint &position);
    void startOperation(OperationKind kind);
    void runOperation(OperationRequest request, std::optional<ClipboardSnapshot> clipboard = {});
    void prepareClipboard(OperationKind kind);
    void pasteFiles();
    void updateClipboardVisuals();
    void updateOperationActions();
    void activate(FilePanel *panel);
    void preview(const QString &path);
    void showViewer(bool fullScreen);
    void applyTheme(bool dark);
};
