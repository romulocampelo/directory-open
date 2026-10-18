#pragma once
#include <QMainWindow>
#include "sessionstore.h"
#include "filetransfer.h"
#include <optional>
#include <QPointer>
#include <QHash>
#include "fileannotations.h"
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
class QToolBar;
class QListWidget;
class SearchDialog;
class FolderSizeDialog;
class ComparisonDialog;
class GlobalHotkeys;
class OperationQueueDialog;
class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString &left = {}, const QString &right = {}, bool nativeOperationUi = true, const QString &sessionFile = {});
    ~MainWindow() override;
    FilePanel *leftPanel() const { return m_left; }
    FilePanel *rightPanel() const { return m_right; }
    WindowSession session() const;
    SavedWorkspace workspace() const;
    bool applyWorkspace(const SavedWorkspace &workspace,bool appearance=true);
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
    QAction *m_independentTreesAction = nullptr;
    void applyTrees();
    QAction *m_darkAction;
    QAction *m_nativeContextMenuAction;
    QAction *m_teraCopyAction;
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
    QPointer<OperationQueueDialog> m_queueDialog;
    bool m_closeAfterQueue=false;
    void showOperationQueue();
    QAction *m_copyAction = nullptr;
    QAction *m_moveAction = nullptr;
    QAction *m_renameAction = nullptr;
    QAction *m_batchRenameAction = nullptr;
    void startBatchRename();
    void showArchive(bool create);
    void showTools(int page);
    void showDuplicateFinder();
    void showUserScript();
    void showWindowsIntegration();
    void showMediaTools(int page);
    ToolSettings m_tools;
    QAction *m_contentAction = nullptr, *m_backupAction = nullptr, *m_remoteAction = nullptr;
    QAction *m_metadataAction = nullptr, *m_imageConvertAction = nullptr, *m_mediaConvertAction = nullptr;
    QAction *m_packAction = nullptr, *m_archiveAction = nullptr;
    QAction *m_renameHistoryAction = nullptr;
    QString m_renameHistoryRoot;
    void showRenameHistory();
    QAction *m_folderAction = nullptr;
    QAction *m_trashAction = nullptr;
    QAction *m_cancelOperation = nullptr;
    QProgressBar *m_operationProgress = nullptr;
    bool m_nativeOperationUi;
    bool m_closeAfterOperation = false;
    SessionStore m_sessionStore;
    QString m_sessionPath;
    QVector<SavedWorkspace> m_layouts;
    QVector<SavedTabGroup> m_tabGroups;
    void showWorkspaces();
    FolderViewCatalog *m_folderViews;
    QVector<FavoriteEntry> m_favorites;
    QMenu *m_favoritesMenu = nullptr;
    QListWidget *m_favoritesList = nullptr;
    QPointer<SearchDialog> m_searchDialog;
    QPointer<FolderSizeDialog> m_folderSizeDialog;
    void showFolderSize();
    void refreshFavorites();
    void openFavorite(const QString &id);
    void addCurrentFavorite();
    void editFavorites();
    QPointer<ComparisonDialog> m_comparisonDialog;
    void showComparison();
    void showSearch();
    void finishClose(QCloseEvent *event);
    void showFileContextMenu(const QPoint &position);
    void showTreeContextMenu(const QPoint &position);
    void showTreeContextMenu(QTreeView *tree,const QPoint &position);
    void showContextMenu(const QPoint &position, const QStringList &selection, const QString &folder,
                         QMenu &menu, const QHash<QString, QAction *> &routes, FilePanel *selectionPanel = nullptr);
    void startOperation(OperationKind kind);
    void runOperation(OperationRequest request, std::optional<ClipboardSnapshot> clipboard = {});
    void prepareClipboard(OperationKind kind);
    void pasteFiles();
    void updateClipboardVisuals();
    void updateOperationActions();
    bool operationBusy()const;
    void activate(FilePanel *panel);
    void openPath(FilePanel *panel, const QString &path);
    void preview(const QString &path);
    void showViewer(bool fullScreen);
    void applyTheme(bool dark);
    UiCustomization m_ui;
    QVector<UiCommand> m_commands;
    QVector<QString> m_defaultToolbar;
    QToolBar *m_commandToolbar = nullptr;
    QMenu *m_customMenu = nullptr;
    struct MenuNode { QString command; QPointer<QMenu> submenu; bool separator=false; };
    QMap<QString,QVector<MenuNode>> m_defaultMenus;
    QMap<QString,QToolBar*> m_namedToolbars;
    QMap<QString,QIcon> m_defaultIcons;
    void captureInterfaceDefaults();
    void synchronizeLaunchers();
    void applyAdvancedInterface();
    void showInterfaceStudio();
    void launchUserCommand(const QString &id);
    GlobalHotkeys *m_globalHotkeys=nullptr;
    void showGlobalKeys();
    void showViewerSettings();
    void showOcr();
    void showAnnotations(bool images=false);
    AnnotationCatalog m_temporaryAnnotations;
    void initializeCommands();
    void applyCustomization();
    void showPreferences();
    void showCommands();
};
