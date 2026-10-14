#include "filepanel.h"
#include "filelistview.h"
#include "folderviewsdialog.h"
#include "thumbnailcache.h"
#include "thumbnailview.h"
#include <QStackedWidget>
#include <QColor>
#include <QSet>
#include <QApplication>
#include <QDateTime>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QLineEdit>
#include <QPainter>
#include <QPolygonF>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QStorageInfo>
#include <QStyle>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

class ReadOnlyFileModel final : public QFileSystemModel {
public:
    explicit ReadOnlyFileModel(QObject *parent) : QFileSystemModel(parent) {}
    void setCutPaths(const QStringList &paths) {
        m_cut.clear(); for (const auto &path : paths) m_cut.insert(QDir::cleanPath(QDir::fromNativeSeparators(path)).toCaseFolded());
        const auto root = index(rootPath()); const auto count = rowCount(root);
        if (count) emit dataChanged(index(0, 0, root), index(count - 1, 3, root), {Qt::ForegroundRole});
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
            const QStringList labels{tr("Nome"), tr("Tamanho"), tr("Tipo"), tr("Modificado em")};
            if (section >= 0 && section < labels.size()) return labels[section];
        }
        return QFileSystemModel::headerData(section, orientation, role);
    }
    QVariant data(const QModelIndex &index, int role) const override {
        if (role == Qt::ForegroundRole && m_cut.contains(QDir::cleanPath(filePath(index)).toCaseFolded())) return QColor("#85909e");
        if (role == Qt::DisplayRole && index.column() == 2 && isDir(index)) return tr("Pasta");
        return QFileSystemModel::data(index, role);
    }
private:
    QSet<QString> m_cut;
};

class FolderFilter final : public QSortFilterProxyModel {
public:
    explicit FolderFilter(QObject *parent) : QSortFilterProxyModel(parent) {
        setSortCaseSensitivity(Qt::CaseInsensitive);
    }
    QString root;
    QString text;
    void update(const QString &path, const QString &query) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange(); root = path; text = query; endFilterChange(Direction::Rows);
#else
        root = path; text = query; invalidateFilter();
#endif
    }
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override {
        const auto *model = qobject_cast<QFileSystemModel *>(sourceModel());
        if (model->filePath(parent) != root || text.isEmpty()) return true;
        return model->fileName(model->index(row, 0, parent)).contains(text, Qt::CaseInsensitive);
    }
    bool lessThan(const QModelIndex &a, const QModelIndex &b) const override {
        const auto *model = qobject_cast<QFileSystemModel *>(sourceModel());
        const auto left = model->fileInfo(a), right = model->fileInfo(b);
        if (left.isDir() != right.isDir()) return left.isDir();
        if (a.column() == 1) return left.size() < right.size();
        if (a.column() == 3) return left.lastModified() < right.lastModified();
        if (a.column() == 2) return QString::localeAwareCompare(model->type(a), model->type(b)) < 0;
        return QString::localeAwareCompare(left.fileName().toCaseFolded(), right.fileName().toCaseFolded()) < 0;
    }
};

FilePanel::FilePanel(QWidget *parent, FolderViewCatalog *folderViews, ThumbnailCache *thumbnails) : QWidget(parent),
    m_folderViews(folderViews ? folderViews : new FolderViewCatalog(this)) {
    m_thumbnails = thumbnails ? thumbnails : new ThumbnailCache(this);
    setObjectName("filePanel");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    auto *nav = new QToolBar(this); nav->setObjectName("panelNavigation");
    nav->setIconSize(QSize(16, 16));
    m_back = nav->addAction(style()->standardIcon(QStyle::SP_ArrowBack), tr("Voltar"), this, &FilePanel::back);
    m_forward = nav->addAction(style()->standardIcon(QStyle::SP_ArrowForward), tr("Avançar"), this, &FilePanel::forward);
    m_up = nav->addAction(style()->standardIcon(QStyle::SP_ArrowUp), tr("Pasta acima"), this, &FilePanel::up);
    nav->addAction(style()->standardIcon(QStyle::SP_DirHomeIcon), tr("Pasta pessoal"), this, [this] { navigate(QDir::homePath()); });
    m_address = new QLineEdit(this); m_address->setObjectName("address");
    m_address->setAccessibleName(tr("Endereço da pasta")); m_address->setClearButtonEnabled(true);
    nav->addWidget(m_address);
    connect(m_address, &QLineEdit::returnPressed, this, [this] {
        if (navigate(m_address->text())) filesView()->setFocus();
    });
    layout->addWidget(nav);
    m_model = new ReadOnlyFileModel(this);
    m_model->setReadOnly(true); m_model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    m_filter = new FolderFilter(this); m_filter->setSourceModel(m_model);
    auto *transferView = new FileListView(this); m_view = transferView; m_view->setObjectName("files");
    m_iconView = new FileIconView(this); m_iconView->setObjectName("fileThumbnails");
    auto configureTransfers = [this](auto *view) {
        view->selectedFiles = [this] { return selectedPaths(); };
        view->destinationAt = [this, view](const QPoint &point) {
            const auto index = m_filter->mapToSource(view->indexAt(point));
            return index.isValid() && m_model->isDir(index) ? m_model->filePath(index) : path();
        };
        view->receiveFiles = [this](FileTransfer transfer, const QString &destination) { emit activated(); emit transferRequested(transfer, destination); };
    };
    configureTransfers(transferView); configureTransfers(m_iconView);
    m_view->setModel(m_filter); m_view->setRootIsDecorated(false); m_view->setItemsExpandable(false);
    m_view->setSortingEnabled(true); m_view->sortByColumn(0, Qt::AscendingOrder);
    m_view->setUniformRowHeights(true); m_view->setAllColumnsShowFocus(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->installEventFilter(this); m_view->viewport()->installEventFilter(this);
    connect(m_view, &QTreeView::customContextMenuRequested, this, [this](const QPoint &point) { requestContextMenu(point, false); });
    m_view->header()->setStretchLastSection(false);
    m_view->header()->setSectionsMovable(true);
    m_view->header()->setFirstSectionMovable(false);
    m_view->header()->setMinimumSectionSize(32);
    m_view->header()->setMaximumSectionSize(8192);
    m_view->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_view->setColumnWidth(1, 90); m_view->setColumnWidth(2, 120); m_view->setColumnWidth(3, 145);
    connect(m_view->header(), &QHeaderView::sectionResized, this, [this](int column, int, int size) {
        // Qt reports zero when hiding a column. Keep its last usable width.
        if (column >= 1 && column <= 3 && size >= 32 && size <= 8192) m_columnWidths[column - 1] = size;
    });
    m_iconView->setModel(m_filter);
    auto *oldSelection = m_iconView->selectionModel();
    m_iconView->setSelectionModel(m_view->selectionModel()); delete oldSelection;
    m_iconView->setViewMode(QListView::IconMode); m_iconView->setResizeMode(QListView::Adjust);
    m_iconView->setMovement(QListView::Static); m_iconView->setWrapping(true);
    m_iconView->configureTransferInteraction();
    m_iconView->setLayoutMode(QListView::Batched); m_iconView->setBatchSize(128);
    m_iconView->setUniformItemSizes(true); m_iconView->setSpacing(0);
    m_iconView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_iconView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_iconView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_iconView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_iconView->installEventFilter(this); m_iconView->viewport()->installEventFilter(this);
    connect(m_iconView, &QListView::customContextMenuRequested, this, [this](const QPoint &point) { requestContextMenu(point, false); });
    m_thumbnailView = new ThumbnailView(m_iconView, m_model, m_filter, m_thumbnails);
    m_views = new QStackedWidget(this); m_views->addWidget(m_view); m_views->addWidget(m_iconView);
    auto doubleClick = [this](const QModelIndex &index) {
        const auto info = m_model->fileInfo(m_filter->mapToSource(index));
        if (info.isDir()) navigate(info.absoluteFilePath());
        else emit selected(info.absoluteFilePath());
    };
    connect(m_iconView, &QListView::doubleClicked, this, doubleClick);
    connect(m_view, &QTreeView::doubleClicked, this, doubleClick);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        updateSummary(); emit selected(selectedPath());
    });
    connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { emit selected(selectedPath()); });
    layout->addWidget(m_views, 1);
    auto *tabRow = new QWidget(this); auto *tabsLayout = new QHBoxLayout(tabRow);
    tabsLayout->setContentsMargins(0, 0, 2, 0); tabsLayout->setSpacing(0);
    m_tabBar = new QTabBar(this); m_tabBar->setObjectName("folderTabs");
    m_tabBar->setExpanding(false); m_tabBar->setElideMode(Qt::ElideMiddle);
    m_tabBar->setShape(QTabBar::RoundedSouth);
    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) { if (index >= 0 && index < m_tabs.size()) applyPath(); });
    connect(m_tabBar, &QTabBar::tabCloseRequested, this, &FilePanel::closeTab);
    auto *add = new QToolButton(this); add->setText("+"); add->setToolTip(tr("Nova aba (Ctrl+T)"));
    connect(add, &QToolButton::clicked, this, [this] { newTab(); });
    tabsLayout->addWidget(m_tabBar); tabsLayout->addWidget(add); tabsLayout->addStretch();
    layout->addWidget(tabRow);
    auto *footer = new QWidget(this); footer->setObjectName("panelFooter");
    auto *footerLayout = new QHBoxLayout(footer); footerLayout->setContentsMargins(8, 4, 6, 4);
    m_summary = new QLabel(this); footerLayout->addWidget(m_summary, 1);
    m_search = new QLineEdit(this); m_search->setObjectName("filter");
    m_search->setPlaceholderText(tr("Filtrar arquivos…")); m_search->setClearButtonEnabled(true);
    m_search->setMaximumWidth(195); m_search->setAccessibleName(tr("Filtro da pasta atual"));
    connect(m_search, &QLineEdit::textChanged, this, &FilePanel::setFilter);
    footerLayout->addWidget(m_search); layout->addWidget(footer);
    connect(m_model, &QFileSystemModel::directoryLoaded, this, [this](const QString &) { updateSummary(); });
    connect(m_filter, &QAbstractItemModel::rowsInserted, this, [this] { updateSummary(); });
    connect(m_filter, &QAbstractItemModel::rowsRemoved, this, [this] { updateSummary(); });
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        if (now && (now == this || isAncestorOf(now))) emit activated();
    });
    createViewMenu(nav);
    connect(m_folderViews, &FolderViewCatalog::changed, this, &FilePanel::applyFolderView);
    newTab(QDir::currentPath());
}

FolderView FilePanel::currentView() const {
    FolderView view; view.thumbnails = m_thumbnailMode; view.thumbnailSize = m_thumbnailSize; const auto *header = m_view->header();
    view.sortColumn = header->sortIndicatorSection(); view.descending = header->sortIndicatorOrder() == Qt::DescendingOrder;
    view.widths = m_columnWidths;
    for (int i = 0; i < 3; ++i) {
        view.order[i] = header->logicalIndex(i + 1);
        view.visible[i] = !m_view->isColumnHidden(i + 1);
    }
    return view;
}
QAbstractItemView *FilePanel::filesView() const { return m_thumbnailMode ? static_cast<QAbstractItemView *>(m_iconView) : m_view; }
void FilePanel::addFileAction(QAction *action) { m_view->addAction(action); m_iconView->addAction(action); }
void FilePanel::setThumbnailPresentation(bool on, int size) {
    const bool focused = filesView()->hasFocus();
    m_thumbnailMode = on; m_thumbnailSize = size; m_thumbnailView->setSize(size);
    m_views->setCurrentWidget(on ? static_cast<QWidget *>(m_iconView) : m_view);
    if (focused) filesView()->setFocus();
    if (filesView()->currentIndex().isValid()) filesView()->scrollTo(filesView()->currentIndex());
}
void FilePanel::applyView(const FolderView &view) {
    auto *header = m_view->header();
    for (int i = 0; i < 3; ++i) {
        header->moveSection(header->visualIndex(view.order[i]), i + 1);
        m_view->setColumnHidden(i + 1, !view.visible[i]);
        m_view->setColumnWidth(i + 1, view.widths[i]);
    }
    setThumbnailPresentation(view.thumbnails, view.thumbnailSize);
    m_columnWidths = view.widths;
    m_view->sortByColumn(view.sortColumn, view.descending ? Qt::DescendingOrder : Qt::AscendingOrder);
}
void FilePanel::applyFolderView() {
    if (m_tabs.isEmpty() || m_tabBar->currentIndex() < 0) return;
    const auto key = folderViewKey(path());
    const auto &tab = m_tabs[m_tabBar->currentIndex()];
    const auto saved = tab.unavailablePath.isEmpty() ? m_folderViews->find(path()) : std::nullopt;
    const bool newFolder = m_displayedFolder != key;
    if (newFolder && !m_usingSavedView) m_baselineView = currentView();
    if (saved) {
        if (!m_usingSavedView || newFolder || saved != m_appliedSavedView) applyView(*saved);
    } else if (m_usingSavedView) applyView(m_baselineView);
    // A saved folder never becomes the fallback for unrelated folders.
    // Keep unsaved panel choices as the baseline only while no preset applies.
    if (!saved) m_baselineView = currentView();
    m_usingSavedView = saved.has_value(); m_appliedSavedView = saved; m_displayedFolder = key;
}
void FilePanel::createViewMenu(QToolBar *nav) {
    auto *button = new QToolButton(this); button->setObjectName("folderViewButton");
    button->setText(tr("Exibição")); button->setPopupMode(QToolButton::InstantPopup);
    button->setToolTip(tr("Colunas, ordenação e exibição guardada para esta pasta"));
    auto *menu = new QMenu(button); menu->setObjectName("folderViewMenu"); button->setMenu(menu); nav->addWidget(button);
    auto *details = menu->addAction(tr("Detalhes")); details->setObjectName("viewDetails"); details->setCheckable(true);
    auto *names = menu->addAction(tr("Somente nomes")); names->setObjectName("viewNames"); names->setCheckable(true);
    connect(details, &QAction::triggered, this, [this] { setThumbnailPresentation(false, m_thumbnailSize); for (int i = 1; i < 4; ++i) m_view->showColumn(i); });
    connect(names, &QAction::triggered, this, [this] { setThumbnailPresentation(false, m_thumbnailSize); for (int i = 1; i < 4; ++i) m_view->hideColumn(i); });
    auto *thumbnails = menu->addAction(tr("Miniaturas")); thumbnails->setObjectName("viewThumbnails"); thumbnails->setCheckable(true);
    connect(thumbnails, &QAction::triggered, this, [this] { setThumbnailPresentation(true, m_thumbnailSize); });
    auto *sizesMenu = menu->addMenu(tr("Tamanho das miniaturas")); QVector<QAction *> sizes;
    for (const int pixels : {96, 144, 192, 256}) {
        auto *size = sizesMenu->addAction(tr("%1 px").arg(pixels)); size->setData(pixels); size->setObjectName(QString("thumbnailSize%1").arg(pixels)); size->setCheckable(true); sizes.append(size);
        connect(size, &QAction::triggered, this, [this, pixels] { setThumbnailPresentation(true, pixels); });
    }
    menu->addSeparator();
    const QStringList labels{tr("Nome"), tr("Tamanho"), tr("Tipo"), tr("Modificado em")};
    QVector<QAction *> columns, sorts;
    auto *columnsMenu = menu->addMenu(tr("Colunas"));
    for (int i = 1; i < 4; ++i) {
        auto *action = columnsMenu->addAction(labels[i]); action->setObjectName(QString("viewColumn%1").arg(i)); action->setCheckable(true); columns.append(action);
        connect(action, &QAction::triggered, this, [this, i](bool on) { m_view->setColumnHidden(i, !on); });
    }
    auto *sortMenu = menu->addMenu(tr("Ordenar por"));
    for (int i = 0; i < 4; ++i) {
        auto *action = sortMenu->addAction(labels[i]); action->setObjectName(QString("viewSort%1").arg(i)); action->setCheckable(true); sorts.append(action);
        connect(action, &QAction::triggered, this, [this, i] { m_view->sortByColumn(i, m_view->header()->sortIndicatorOrder()); });
    }
    sortMenu->addSeparator(); auto *descending = sortMenu->addAction(tr("Ordem decrescente")); descending->setObjectName("viewDescending"); descending->setCheckable(true);
    connect(descending, &QAction::triggered, this, [this](bool on) { m_view->sortByColumn(m_view->header()->sortIndicatorSection(), on ? Qt::DescendingOrder : Qt::AscendingOrder); });
    auto *reset = menu->addAction(tr("Restaurar exibição padrão")); reset->setObjectName("viewReset");
    connect(reset, &QAction::triggered, this, [this] { applyView(FolderView{}); });
    menu->addSeparator();
    auto *remember = menu->addAction(tr("Guardar exibição desta pasta")); remember->setObjectName("rememberFolderView");
    remember->setToolTip(tr("Guarda modo, tamanho das miniaturas, colunas e ordenação; a sessão é gravada ao fechar normalmente."));
    connect(remember, &QAction::triggered, this, [this] {
        if (m_tabs.isEmpty() || !m_tabs[m_tabBar->currentIndex()].unavailablePath.isEmpty() || !QFileInfo(path()).isDir()) return;
        const auto error = m_folderViews->remember(path(), currentView());
        if (!error.isEmpty()) QMessageBox::information(this, tr("Guardar exibição"), error);
    });
    auto *forget = menu->addAction(tr("Esquecer exibição desta pasta")); forget->setObjectName("forgetFolderView");
    connect(forget, &QAction::triggered, this, [this] {
        if (!m_tabs.isEmpty() && m_tabs[m_tabBar->currentIndex()].unavailablePath.isEmpty()) m_folderViews->forget(path());
    });
    auto *manage = menu->addAction(tr("Exibições guardadas…")); manage->setObjectName("manageFolderViews");
    connect(manage, &QAction::triggered, this, [this] {
        FolderViewsDialog dialog(m_folderViews->entries(), this);
        if (dialog.exec() == QDialog::Accepted) m_folderViews->replace(dialog.entries());
    });
    connect(menu, &QMenu::aboutToShow, this, [this, details, names, thumbnails, sizes, columnsMenu, columns, sorts, descending, remember, forget] {
        emit activated(); const auto view = currentView();
        details->setChecked(!view.thumbnails && view.visible == std::array<bool, 3>{true, true, true});
        names->setChecked(!view.thumbnails && view.visible == std::array<bool, 3>{false, false, false});
        thumbnails->setChecked(view.thumbnails); columnsMenu->setEnabled(!view.thumbnails);
        for (auto *size : sizes) size->setChecked(size->data().toInt() == view.thumbnailSize);
        for (int i = 0; i < 3; ++i) columns[i]->setChecked(view.visible[i]);
        for (int i = 0; i < 4; ++i) sorts[i]->setChecked(view.sortColumn == i);
        descending->setChecked(view.descending);
        const bool available = !m_tabs.isEmpty() && m_tabs[m_tabBar->currentIndex()].unavailablePath.isEmpty() && QFileInfo(path()).isDir();
        remember->setEnabled(available); forget->setEnabled(available && m_folderViews->find(path()).has_value());
    });
}

QString FilePanel::path() const {
    const int current = m_tabBar->currentIndex();
    return current >= 0 && current < m_tabs.size() ? m_tabs[current].history.value(m_tabs[current].position) : QString{};
}
QString FilePanel::selectedPath() const {
    const auto current = filesView()->currentIndex();
    if (current.isValid() && m_view->selectionModel()->isRowSelected(current.row(), current.parent()))
        return m_model->filePath(m_filter->mapToSource(current));
    return selectedPaths().value(0);
}
QStringList FilePanel::selectedPaths() const {
    QStringList paths;
    for (const auto &index : m_view->selectionModel()->selectedRows()) {
        const auto source = m_filter->mapToSource(index);
        if (source.isValid()) paths.append(m_model->filePath(source));
    }
    return paths;
}
int FilePanel::tabCount() const { return m_tabBar->count(); }
int FilePanel::visibleCount() const { return m_filter->rowCount(m_view->rootIndex()); }
bool FilePanel::navigate(const QString &requested) {
    QString candidate = QDir::fromNativeSeparators(requested.trimmed());
    if (QDir::isRelativePath(candidate)) candidate = QDir(path()).absoluteFilePath(candidate);
    const QFileInfo info(candidate);
    if (!info.exists() || !info.isDir()) {
        m_address->setToolTip(tr("A pasta não existe ou não está disponível."));
        m_address->setStyleSheet("border: 1px solid #c95248;"); return false;
    }
    const QString resolved = info.canonicalFilePath();
    if (resolved.isEmpty()) return false;
    auto &tab = m_tabs[m_tabBar->currentIndex()];
    tab.unavailablePath.clear();
    if (resolved != path()) {
        while (tab.history.size() > tab.position + 1) tab.history.removeLast();
        tab.history.append(resolved); ++tab.position;
    }
    applyPath(); return true;
}
void FilePanel::newTab(const QString &requested) {
    const QString start = requested.isEmpty() ? path() : requested;
    const QFileInfo info(start);
    if (!info.exists() || !info.isDir() || info.canonicalFilePath().isEmpty()) return;
    m_tabs.append(Tab{{info.canonicalFilePath()}, 0, {}, {}});
    const QSignalBlocker blocker(m_tabBar);
    const int index = m_tabBar->addTab(info.fileName().isEmpty() ? QDir::toNativeSeparators(info.canonicalFilePath()) : info.fileName());
    m_tabBar->setCurrentIndex(index); m_tabBar->setTabsClosable(m_tabs.size() > 1);
    applyPath();
}
void FilePanel::closeTab(int index) {
    if (m_tabs.size() <= 1 || index < 0 || index >= m_tabs.size()) return;
    const QSignalBlocker blocker(m_tabBar);
    m_tabs.removeAt(index); m_tabBar->removeTab(index); m_tabBar->setTabsClosable(m_tabs.size() > 1);
    applyPath();
}
void FilePanel::applyPath() {
    const QString current = path();
    if (current.isEmpty()) return;
    auto &tab = m_tabs[m_tabBar->currentIndex()];
    m_address->setStyleSheet({}); m_address->setToolTip(QDir::toNativeSeparators(current));
    if (!tab.unavailablePath.isEmpty()) {
        m_address->setStyleSheet("border: 1px solid #bc8b33;");
        m_address->setToolTip(tr("Pasta salva indisponível: %1\nMostrando a pasta pessoal. Navegue para escolher outra pasta.").arg(QDir::toNativeSeparators(tab.unavailablePath)));
    }
    m_address->setText(QDir::toNativeSeparators(current));
    const QSignalBlocker blocker(m_search); m_search->setText(tab.filter);
    m_filter->update(current, tab.filter);
    const auto root = m_model->setRootPath(current);
    m_view->setRootIndex(m_filter->mapFromSource(root)); m_iconView->setRootIndex(m_view->rootIndex()); m_view->clearSelection();
    m_thumbnailView->requestVisible();
    applyFolderView();
    const auto name = QFileInfo(current).fileName();
    m_tabBar->setTabText(m_tabBar->currentIndex(), name.isEmpty() ? QDir::toNativeSeparators(current) : name);
    m_tabBar->setTabToolTip(m_tabBar->currentIndex(), QDir::toNativeSeparators(current));
    m_back->setEnabled(tab.position > 0); m_forward->setEnabled(tab.position + 1 < tab.history.size());
    updateSummary(); emit pathChanged(current); emit selected({});
}
void FilePanel::back() { auto &tab = m_tabs[m_tabBar->currentIndex()]; if (tab.position > 0) { --tab.position; applyPath(); } }
void FilePanel::forward() { auto &tab = m_tabs[m_tabBar->currentIndex()]; if (tab.position + 1 < tab.history.size()) { ++tab.position; applyPath(); } }
void FilePanel::up() { QDir folder(path()); if (folder.cdUp()) navigate(folder.absolutePath()); }
void FilePanel::focusAddress() { m_address->setFocus(); m_address->selectAll(); }
void FilePanel::focusFilter() { m_search->setFocus(); m_search->selectAll(); }
void FilePanel::setActive(bool active) {
    setProperty("active", active); style()->unpolish(this); style()->polish(this);
    auto *nav = findChild<QToolBar *>("panelNavigation"); nav->setProperty("active", active);
    nav->style()->unpolish(nav); nav->style()->polish(nav);
    auto arrow = [nav](int direction) {
        QPixmap pixmap(32, 32); pixmap.setDevicePixelRatio(2); pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap); painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen); painter.setBrush(nav->palette().color(QPalette::WindowText));
        QPolygonF points;
        if (direction == 0) points = {{11, 3}, {5, 8}, {11, 13}};
        else if (direction == 1) points = {{5, 3}, {11, 8}, {5, 13}};
        else points = {{3, 11}, {8, 5}, {13, 11}};
        painter.drawPolygon(points); return QIcon(pixmap);
    };
    m_back->setIcon(arrow(0)); m_forward->setIcon(arrow(1)); m_up->setIcon(arrow(2));
}
void FilePanel::setFilter(const QString &text) {
    if (m_tabBar->currentIndex() < 0) return;
    m_tabs[m_tabBar->currentIndex()].filter = text;
    { const QSignalBlocker blocker(m_search); m_search->setText(text); }
    m_filter->update(path(), text);
    m_view->setRootIndex(m_filter->mapFromSource(m_model->index(path())));
    m_iconView->setRootIndex(m_view->rootIndex()); m_thumbnailView->requestVisible();
    updateSummary();
}
void FilePanel::refresh() {
    m_thumbnails->invalidate();
    // QFileSystemModel monitors directory changes; reapply the current root.
    applyPath();
}
void FilePanel::selectAdjacentFile(int direction) {
    if (direction != -1 && direction != 1) return;
    const auto current = filesView()->currentIndex(); const auto root = filesView()->rootIndex();
    int row = current.isValid() && current.parent() == root ? current.row() + direction : (direction > 0 ? 0 : m_filter->rowCount(root) - 1);
    for (; row >= 0 && row < m_filter->rowCount(root); row += direction) {
        const auto index = m_filter->index(row, 0, root); const auto source = m_filter->mapToSource(index);
        if (source.isValid() && !m_model->isDir(source)) {
            m_view->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            filesView()->scrollTo(index); return;
        }
    }
}
void FilePanel::setTransfersEnabled(bool enabled) { static_cast<FileListView *>(m_view)->transferEnabled = enabled; m_iconView->transferEnabled = enabled; }
void FilePanel::setCutPaths(const QStringList &paths) { static_cast<ReadOnlyFileModel *>(m_model)->setCutPaths(paths); }
void FilePanel::updateSummary() {
    const auto count = m_view->selectionModel()->selectedRows().size();
    const auto total = visibleCount();
    const auto items = total == 1 ? tr("1 item") : tr("%1 itens").arg(total);
    const auto selected = count == 1 ? tr("1 selecionado") : tr("%1 selecionados").arg(count);
    m_summary->setText(items + "  ·  " + selected);
}
PanelSession FilePanel::session() const {
    PanelSession state;
    for (const auto &tab : m_tabs) state.tabs.append({tab.unavailablePath.isEmpty() ? tab.history.value(tab.position) : tab.unavailablePath, tab.filter});
    const auto view = m_usingSavedView ? m_baselineView : currentView();
    state.currentTab = m_tabBar->currentIndex(); state.sortColumn = view.sortColumn;
    state.descending = view.descending; return state;
}
int FilePanel::restoreSession(const PanelSession &state) {
    if (state.tabs.isEmpty()) return 0;
    const auto fallback = QFileInfo(QDir::homePath()).canonicalFilePath().isEmpty() ? path() : QFileInfo(QDir::homePath()).canonicalFilePath();
    int unavailable = 0; const QSignalBlocker blocker(m_tabBar);
    while (m_tabBar->count() > 0) m_tabBar->removeTab(0);
    m_tabs.clear();
    m_usingSavedView = false; m_appliedSavedView.reset(); m_displayedFolder.clear();
    for (const auto &saved : state.tabs) {
        const QFileInfo info(saved.path); const auto resolved = info.isDir() ? info.canonicalFilePath() : QString{};
        const bool missing = resolved.isEmpty(); if (missing) ++unavailable;
        m_tabs.append(Tab{{missing ? fallback : resolved}, 0, saved.filter, missing ? saved.path : QString{}});
        const auto display = missing ? fallback : resolved;
        m_tabBar->addTab(QFileInfo(display).fileName().isEmpty() ? QDir::toNativeSeparators(display) : QFileInfo(display).fileName());
        m_tabBar->setTabToolTip(m_tabBar->count() - 1, QDir::toNativeSeparators(display));
    }
    m_tabBar->setCurrentIndex(qBound(0, state.currentTab, int(m_tabs.size()) - 1)); m_tabBar->setTabsClosable(m_tabs.size() > 1);
    FolderView defaults; defaults.sortColumn = qBound(0, state.sortColumn, 3); defaults.descending = state.descending;
    applyView(defaults);
    m_baselineView = currentView();
    applyPath(); return unavailable;
}
bool FilePanel::eventFilter(QObject *watched, QEvent *event) {
    if ((watched == m_view || watched == m_view->viewport() || watched == m_iconView || watched == m_iconView->viewport()) && event->type() == QEvent::ContextMenu) {
        auto *context = static_cast<QContextMenuEvent *>(event);
        if (context->reason() == QContextMenuEvent::Keyboard) { requestContextMenu({}, true); return true; }
    }
    return QWidget::eventFilter(watched, event);
}
void FilePanel::requestContextMenu(const QPoint &position, bool keyboard) {
    emit activated();
    QModelIndex index = keyboard ? filesView()->currentIndex() : filesView()->indexAt(position);
    if (!keyboard && index.isValid() && !m_view->selectionModel()->isRowSelected(index.row(), index.parent())) {
        m_view->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    } else if (!keyboard && !index.isValid()) m_view->clearSelection();
    if (index.isValid()) m_view->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
    const auto point = keyboard ? (index.isValid() ? filesView()->visualRect(index).center() : filesView()->viewport()->rect().center()) : position;
    emit contextMenuRequested(filesView()->viewport()->mapToGlobal(point));
}
