#pragma once
#include <QWidget>
#include <QStringList>
#include <QVector>
#include "sessionstore.h"
#include "filetransfer.h"
class QFileSystemModel;
class QTreeView;
class QLineEdit;
class QTabBar;
class QLabel;
class QAction;
class QToolBar;
class FolderFilter;
class FileIconView;
class QAbstractItemView;
class QStackedWidget;
class ThumbnailCache;
class ThumbnailView;
class QSplitter;

class FilePanel final : public QWidget {
    Q_OBJECT
public:
    explicit FilePanel(QWidget *parent = nullptr, FolderViewCatalog *folderViews = nullptr, ThumbnailCache *thumbnails = nullptr);
    QAbstractItemView *filesView() const;
    void addFileAction(QAction *action);
    QString path() const;
    QString selectedPath() const;
    QStringList selectedPaths() const;
    QStringList listedPaths(int limit=256) const;
    int tabCount() const;
    int visibleCount() const;
    bool navigate(const QString &path);
    void newTab(const QString &path = {});
    void closeTab(int index);
    void back();
    void forward();
    void up();
    void focusAddress();
    void focusFilter();
    void setActive(bool active);
    void setFilter(const QString &text);
    void refresh();
    void selectAdjacentFile(int direction);
    PanelSession session() const;
    int restoreSession(const PanelSession &session);
    void setTransfersEnabled(bool enabled);
    void setCutPaths(const QStringList &paths);
    void setTreeVisible(bool visible);
    QTreeView *folderTree() const { return m_tree; }
    void duplicateTab();
    void setTabLocked(bool locked);
    bool tabLocked() const;
    void editRules();
    QString applyRules(const FolderRules &rules);
    FolderRules rules() const;
    void setGrouping(int grouping);
    void setTabMenu(std::optional<QStringList> order){m_tabMenu=std::move(order);}
signals:
    void activated();
    void pathChanged(const QString &path);
    void selected(const QString &path);
    void openRequested(const QString &path);
    void contextMenuRequested(const QPoint &globalPosition);
    void transferRequested(FileTransfer transfer, const QString &destination);
    void treeContextMenuRequested(QTreeView *tree,const QPoint &position);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    struct Tab { QStringList history; int position = 0; QString filter; QString unavailablePath; bool locked=false; FolderRules rules; };
    QVector<Tab> m_tabs;
    std::optional<QStringList> m_tabMenu;
    QFileSystemModel *m_model;
    FolderFilter *m_filter;
    QTreeView *m_view;
    FileIconView *m_iconView;
    QStackedWidget *m_views;
    QSplitter *m_content;
    QTreeView *m_tree = nullptr;
    QFileSystemModel *m_treeModel = nullptr;
    QByteArray m_treeState;
    ThumbnailCache *m_thumbnails;
    ThumbnailView *m_thumbnailView;
    bool m_thumbnailMode = false;
    int m_thumbnailSize = 144;
    void setThumbnailPresentation(bool on, int size);
    QLineEdit *m_address;
    QLineEdit *m_search;
    QTabBar *m_tabBar;
    QLabel *m_summary;
    QAction *m_back;
    QAction *m_forward;
    QAction *m_up;
    FolderViewCatalog *m_folderViews;
    FolderView m_baselineView;
    std::array<int, 3> m_columnWidths{90, 120, 145};
    bool m_usingSavedView = false;
    QString m_displayedFolder;
    std::optional<FolderView> m_appliedSavedView;
    FolderView currentView() const;
    void applyView(const FolderView &view);
    void applyFolderView();
    void createViewMenu(QToolBar *nav);
    void applyPath();
    void updateSummary();
    void requestContextMenu(const QPoint &position, bool keyboard);
};
