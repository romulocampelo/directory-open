#include "mainwindow.h"
#include "filepanel.h"
#include "operationdialog.h"
#include "favoritesdialog.h"
#include "searchdialog.h"
#include "comparisondialog.h"
#include "previewwidget.h"
#include "thumbnailcache.h"
#include <QDialog>
#include <QShortcut>
#include <QApplication>
#include <QAction>
#include <QAbstractButton>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QTimer>
#include <QMessageBox>
#include <QProgressBar>
#include <QScopedValueRollback>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

MainWindow::MainWindow(const QString &left, const QString &right, bool nativeOperationUi, const QString &sessionFile) : m_nativeOperationUi(nativeOperationUi), m_sessionStore(sessionFile) {
    m_clipboard = new FileClipboard(this);
    setWindowTitle(tr("Directory Open — Arquivos")); resize(1400, 820);
    setDockOptions(QMainWindow::AllowNestedDocks);
    auto *panes = new QSplitter(this); m_splitter = panes; panes->setObjectName("fileSplit");
    auto *sidebar = new QWidget(this); m_sidebar = sidebar; sidebar->setObjectName("sidebar");
    auto *sideLayout = new QVBoxLayout(sidebar); sideLayout->setContentsMargins(0, 0, 0, 0); sideLayout->setSpacing(0);
    auto *placesTitle = new QLabel(tr("  Locais"), this); placesTitle->setObjectName("sectionTitle"); sideLayout->addWidget(placesTitle);
    auto *places = new QListWidget(this); places->setObjectName("places"); places->setFixedHeight(156);
    auto addPlace = [&](const QString &label, const QString &path, QStyle::StandardPixmap icon) {
        if (path.isEmpty() || !QFileInfo(path).isDir()) return;
        auto *item = new QListWidgetItem(style()->standardIcon(icon), label, places);
        item->setData(Qt::UserRole, path); item->setToolTip(QDir::toNativeSeparators(path));
        m_favorites.append({QUuid::createUuid().toString(QUuid::WithoutBraces), label, path});
    };
    addPlace(tr("Pasta pessoal"), QDir::homePath(), QStyle::SP_DirHomeIcon);
    addPlace(tr("Área de trabalho"), QStandardPaths::writableLocation(QStandardPaths::DesktopLocation), QStyle::SP_DesktopIcon);
    addPlace(tr("Documentos"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), QStyle::SP_DirIcon);
    addPlace(tr("Downloads"), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), QStyle::SP_DirIcon);
    addPlace(tr("Projeto"), QDir::currentPath(), QStyle::SP_DirIcon);
    sideLayout->addWidget(places);
    auto *favoritesTitle = new QLabel(tr("  Favoritos"), this); favoritesTitle->setObjectName("sectionTitle"); sideLayout->addWidget(favoritesTitle);
    m_favoritesList = new QListWidget(this); m_favoritesList->setObjectName("favoritesList"); m_favoritesList->setFixedHeight(112); sideLayout->addWidget(m_favoritesList);
    connect(m_favoritesList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { openFavorite(item->data(Qt::UserRole).toString()); });
    auto *treeTitle = new QLabel(tr("  Árvore de pastas"), this); treeTitle->setObjectName("sectionTitle"); sideLayout->addWidget(treeTitle);
    m_treeModel = new QFileSystemModel(this); m_treeModel->setReadOnly(true);
    m_treeModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Drives);
    m_treeModel->setRootPath({});
    m_tree = new QTreeView(this); m_tree->setObjectName("folderTree"); m_tree->setModel(m_treeModel);
    m_tree->setHeaderHidden(true); m_tree->setUniformRowHeights(true);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers); m_tree->setAnimated(false);
    for (int column = 1; column < 4; ++column) m_tree->hideColumn(column);
    sideLayout->addWidget(m_tree, 1); panes->addWidget(sidebar);
    m_folderViews = new FolderViewCatalog(this);
    auto *thumbnails = new ThumbnailCache(this); thumbnails->setObjectName("thumbnailCache");
    m_left = new FilePanel(this, m_folderViews, thumbnails); m_right = new FilePanel(this, m_folderViews, thumbnails); m_active = m_left;
    panes->addWidget(m_left); panes->addWidget(m_right);
    panes->setSizes({230, 580, 580}); panes->setCollapsible(0, true); panes->setCollapsible(1, false); panes->setCollapsible(2, false);
    setCentralWidget(panes);
    connect(places, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { m_active->navigate(item->data(Qt::UserRole).toString()); });
    connect(m_tree, &QTreeView::clicked, this, [this](const QModelIndex &index) { m_active->navigate(m_treeModel->filePath(index)); });
    for (auto *panel : {m_left, m_right}) {
        connect(panel, &FilePanel::transferRequested, this, [this](FileTransfer transfer, const QString &destination) {
            OperationRequest request; request.kind = transfer.kind; request.sources = transfer.paths; request.destination = destination;
            runOperation(request);
        });
        connect(panel, &FilePanel::contextMenuRequested, this, &MainWindow::showFileContextMenu);
        connect(panel, &FilePanel::activated, this, [this, panel] { activate(panel); });
        connect(panel, &FilePanel::selected, this, [this, panel](const QString &path) { if (panel == m_active) { preview(path); updateOperationActions(); } });
        connect(panel, &FilePanel::pathChanged, this, [this, panel](const QString &) { if (panel == m_active) activate(panel); });
    }

    auto action = [this](const QString &name, const QString &label, QStyle::StandardPixmap icon, const QKeySequence &key, auto fn) {
        auto *a = new QAction(style()->standardIcon(icon), label, this);
        a->setObjectName(name); a->setShortcut(key); addAction(a); connect(a, &QAction::triggered, this, fn); return a;
    };
    auto *newTab = action("newTab", tr("Nova aba"), QStyle::SP_FileDialogNewFolder, QKeySequence("Ctrl+T"), [this] { m_active->newTab(); });
    auto *closeTab = action("closeTab", tr("Fechar aba"), QStyle::SP_DialogCloseButton, QKeySequence("Ctrl+W"), [this] {
        auto *tabs = m_active->findChild<QTabBar *>("folderTabs"); m_active->closeTab(tabs->currentIndex());
    });
    auto *open = action("open", tr("Abrir"), QStyle::SP_DialogOpenButton, QKeySequence("Ctrl+O"), [this] {
        const QString path = m_active->selectedPath();
        if (path.isEmpty()) return;
        if (QFileInfo(path).isDir()) m_active->navigate(path);
        else if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) QMessageBox::information(this, tr("Abrir arquivo"), tr("Não foi possível abrir este arquivo."));
    });
    m_openAction = open;
    m_openTabAction = action("openFolderInTab", tr("Abrir em nova aba"), QStyle::SP_DirOpenIcon, {}, [this] {
        const auto paths = m_active->selectedPaths(); if (paths.size() == 1 && QFileInfo(paths.first()).isDir()) m_active->newTab(paths.first());
    });
    auto *copyPath = action("copyPath", tr("Copiar caminho"), QStyle::SP_FileLinkIcon, QKeySequence("Ctrl+Shift+C"), [this] {
        auto paths = m_active->selectedPaths(); if (paths.isEmpty()) paths = {m_active->path()};
        for (auto &path : paths) path = QDir::toNativeSeparators(path); qApp->clipboard()->setText(paths.join('\n'));
    });
    m_copyPathAction = copyPath;
    m_copyAction = action("copyFiles", tr("Copiar"), QStyle::SP_FileDialogDetailedView, QKeySequence("F8"), [this] { startOperation(OperationKind::Copy); });
    m_copyAction->setToolTip(tr("Copiar seleção para o outro painel (F8)"));
    m_clipboardCopyAction = action("clipboardCopy", tr("Copiar"), QStyle::SP_FileDialogDetailedView, QKeySequence("Ctrl+C"), [this] { prepareClipboard(OperationKind::Copy); });
    m_clipboardCutAction = action("clipboardCut", tr("Recortar"), QStyle::SP_ArrowRight, QKeySequence("Ctrl+X"), [this] { prepareClipboard(OperationKind::Move); });
    m_pasteAction = action("pasteFiles", tr("Colar"), QStyle::SP_DialogApplyButton, QKeySequence("Ctrl+V"), [this] { pasteFiles(); });
    for (auto *clipboardAction : {m_clipboardCopyAction, m_clipboardCutAction, m_pasteAction}) {
        clipboardAction->setShortcutContext(Qt::WidgetShortcut);
        for (auto *panel : {m_left, m_right}) panel->addFileAction(clipboardAction);
    }
    m_moveAction = action("moveFiles", tr("Mover"), QStyle::SP_ArrowRight, QKeySequence("Ctrl+M"), [this] { startOperation(OperationKind::Move); });
    m_moveAction->setToolTip(tr("Mover seleção para o outro painel (Ctrl+M)"));
    m_renameAction = action("renameFile", tr("Renomear"), QStyle::SP_FileIcon, QKeySequence("F2"), [this] { startOperation(OperationKind::Rename); });
    m_folderAction = action("createFolder", tr("Nova pasta"), QStyle::SP_FileDialogNewFolder, QKeySequence("Ctrl+Shift+N"), [this] { startOperation(OperationKind::CreateFolder); });
    m_trashAction = action("trashFiles", tr("Lixeira"), QStyle::SP_TrashIcon, QKeySequence("Delete"), [this] { startOperation(OperationKind::Trash); });
    m_trashAction->setToolTip(tr("Enviar seleção à Lixeira (Delete)"));
    m_trashAction->setShortcutContext(Qt::WidgetShortcut);
    for (auto *panel : {m_left, m_right}) panel->addFileAction(m_trashAction);
    m_cancelOperation = action("cancelOperation", tr("Cancelar operação"), QStyle::SP_DialogCancelButton, {}, [this] {
        if (m_operationJob) { m_operationJob->cancel(); m_cancelOperation->setEnabled(false); statusBar()->showMessage(tr("Cancelamento solicitado. Aguardando o Windows encerrar a operação…")); }
    });
    m_cancelOperation->setEnabled(false);
    m_cancelOperation->setVisible(false);
    action("address", tr("Editar endereço"), QStyle::SP_DirIcon, QKeySequence("Ctrl+L"), [this] { m_active->focusAddress(); });
    action("filter", tr("Filtrar pasta"), QStyle::SP_FileDialogContentsView, QKeySequence("Ctrl+F"), [this] { m_active->focusFilter(); });
    auto *search = action("searchFiles", tr("Buscar"), QStyle::SP_FileDialogContentsView, QKeySequence("Ctrl+Shift+F"), [this] { showSearch(); });
    search->setToolTip(tr("Buscar nomes na pasta atual e nas subpastas (Ctrl+Shift+F)"));
    action("back", tr("Voltar"), QStyle::SP_ArrowBack, QKeySequence("Alt+Left"), [this] { m_active->back(); });
    action("forward", tr("Avançar"), QStyle::SP_ArrowForward, QKeySequence("Alt+Right"), [this] { m_active->forward(); });
    action("up", tr("Pasta acima"), QStyle::SP_ArrowUp, QKeySequence("Alt+Up"), [this] { m_active->up(); });
    auto *refresh = action("refresh", tr("Atualizar"), QStyle::SP_BrowserReload, QKeySequence("F5"), [this] { m_active->refresh(); });
    m_refreshAction = refresh;
    auto *fileMenu = menuBar()->addMenu(tr("&Arquivo")); fileMenu->addActions({newTab, closeTab, open}); fileMenu->addSeparator();
    fileMenu->addAction(tr("Sair"), QKeySequence("Alt+F4"), this, &QWidget::close);
    auto *editMenu = menuBar()->addMenu(tr("&Editar")); editMenu->addActions({m_clipboardCopyAction, m_clipboardCutAction, m_pasteAction}); editMenu->addSeparator();
    auto *transferMenu = editMenu->addMenu(tr("Para o outro painel")); transferMenu->addActions({m_copyAction, m_moveAction});
    editMenu->addActions({m_renameAction, m_folderAction, m_trashAction}); editMenu->addSeparator(); editMenu->addAction(copyPath); editMenu->addAction(m_cancelOperation);
    auto *viewMenu = menuBar()->addMenu(tr("&Exibir"));
    auto *dual = new QAction(tr("Dois painéis"), this); dual->setObjectName("dualPane"); dual->setCheckable(true); dual->setChecked(true);
    m_dualAction = dual;
    dual->setShortcut(QKeySequence("F6")); addAction(dual);
    connect(dual, &QAction::toggled, this, [this](bool on) { m_right->setVisible(on); if (!on) activate(m_left); }); viewMenu->addAction(dual);
    auto *treeToggle = viewMenu->addAction(tr("Árvore de pastas")); treeToggle->setCheckable(true); treeToggle->setChecked(true);
    m_treeAction = treeToggle; treeToggle->setObjectName("treeVisible");
    connect(treeToggle, &QAction::toggled, sidebar, &QWidget::setVisible);
    auto *dark = viewMenu->addAction(tr("Tema escuro")); dark->setObjectName("darkTheme"); dark->setCheckable(true);
    m_darkAction = dark;
    connect(dark, &QAction::toggled, this, &MainWindow::applyTheme);
    auto *folderMenu = menuBar()->addMenu(tr("&Pastas")); folderMenu->addAction(newTab); folderMenu->addAction(m_folderAction); folderMenu->addAction(refresh);
    auto *compare = action("compareFolders", tr("Comparar pastas…"), QStyle::SP_FileDialogDetailedView, QKeySequence("Ctrl+Shift+K"), [this] { showComparison(); });
    auto *toolsMenu = menuBar()->addMenu(tr("&Ferramentas")); toolsMenu->addAction(search); toolsMenu->addAction(compare); toolsMenu->addAction(copyPath);
    auto *helpMenu = menuBar()->addMenu(tr("A&juda")); helpMenu->addAction(tr("Sobre o Directory Open"), this, [this] {
        QMessageBox::about(this, tr("Directory Open"), tr("Directory Open %1\nGerenciador de arquivos aberto.\n\nNavegação, operações de arquivos, visualização, comparação de pastas e sessão.").arg(QString::fromLatin1(DIROPEN_VERSION)));
    });
    auto *toolbar = addToolBar(tr("Comandos")); toolbar->setObjectName("commands");
    toolbar->setMovable(false); toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); toolbar->setIconSize(QSize(18, 18));
    toolbar->addAction(open); toolbar->addSeparator(); toolbar->addAction(m_copyAction);
    toolbar->addActions({m_moveAction, m_renameAction, m_folderAction, m_trashAction});
    toolbar->addSeparator(); toolbar->addAction(m_pasteAction); toolbar->addAction(copyPath); toolbar->addAction(refresh); toolbar->addAction(m_cancelOperation); toolbar->addSeparator();
    toolbar->addAction(dual);
    toolbar->addAction(search); toolbar->addAction(compare);
    auto *favorites = new QToolButton(this); favorites->setText(tr("Favoritos")); favorites->setIcon(style()->standardIcon(QStyle::SP_DirHomeIcon));
    favorites->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); favorites->setPopupMode(QToolButton::InstantPopup);
    m_favoritesMenu = new QMenu(favorites); m_favoritesMenu->setObjectName("favoritesMenu");
    favorites->setMenu(m_favoritesMenu); toolbar->addWidget(favorites);
    auto *addFavorite = action("addCurrentFavorite", tr("Adicionar pasta atual aos favoritos"), QStyle::SP_DirHomeIcon, QKeySequence("Ctrl+D"), [this] { addCurrentFavorite(); });
    auto *manageFavorites = action("manageFavorites", tr("Organizar favoritos…"), QStyle::SP_FileDialogListView, {}, [this] { editFavorites(); });
    folderMenu->addSeparator(); folderMenu->addActions({addFavorite, manageFavorites});

    m_preview = new QDockWidget(tr("Pré-visualização"), this); m_preview->setObjectName("previewDock");
    m_preview->setAllowedAreas(Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea); m_preview->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    m_previewWidget = new PreviewWidget(this);
    m_preview->setWidget(m_previewWidget); addDockWidget(Qt::RightDockWidgetArea, m_preview); m_preview->hide();
    connect(m_previewWidget, &PreviewWidget::navigateFile, this, [this](int direction) { m_active->selectAdjacentFile(direction); });
    connect(m_previewWidget, &PreviewWidget::enlargeRequested, this, &MainWindow::showViewer);
    action("viewFile", tr("Visualizar arquivo"), QStyle::SP_FileDialogContentsView, QKeySequence("F3"), [this] { showViewer(false); });
    action("viewFullScreen", tr("Visualizar em tela cheia"), QStyle::SP_FileDialogContentsView, QKeySequence("F11"), [this] { showViewer(true); });
    auto *previewAction = m_preview->toggleViewAction(); previewAction->setShortcut(QKeySequence("F7"));
    addAction(previewAction); viewMenu->addAction(previewAction); toolbar->addAction(previewAction);
    connect(m_preview, &QDockWidget::visibilityChanged, this, [this](bool visible) { if (visible) preview(m_active->selectedPath()); else m_previewWidget->setPath({}); });
    m_operationProgress = new QProgressBar(this); m_operationProgress->setMaximumWidth(220); m_operationProgress->hide(); statusBar()->addPermanentWidget(m_operationProgress);
    statusBar()->addPermanentWidget(new QLabel(tr("Directory Open %1").arg(QString::fromLatin1(DIROPEN_VERSION)), this));
    const auto restored = m_sessionStore.load();
    const bool validSession = restored.state == SessionLoadState::Loaded || restored.state == SessionLoadState::Recovered;
    int unavailable = 0;
    if (validSession) {
        if (restored.session.favorites) m_favorites = *restored.session.favorites;
        if (restored.session.folderViews) m_folderViews->replace(*restored.session.folderViews);
        unavailable += m_left->restoreSession(restored.session.left); unavailable += m_right->restoreSession(restored.session.right);
        if (!restored.session.geometry.isEmpty()) restoreGeometry(restored.session.geometry);
        if (!restored.session.windowState.isEmpty()) restoreState(restored.session.windowState, 1);
        m_dualAction->setChecked(restored.session.dualPane); m_treeAction->setChecked(restored.session.treeVisible);
        m_darkAction->setChecked(restored.session.dark); m_preview->setVisible(restored.session.previewVisible);
        if (!restored.session.splitterState.isEmpty()) panes->restoreState(restored.session.splitterState);
    }
    if (!left.isEmpty()) m_left->navigate(left);
    if (!right.isEmpty()) m_right->navigate(right);
    refreshFavorites();
    connect(m_clipboard, &FileClipboard::changed, this, &MainWindow::updateClipboardVisuals);
    updateClipboardVisuals();
    applyTheme(m_darkAction->isChecked());
    activate(validSession && restored.session.activePanel == 1 && m_dualAction->isChecked() ? m_right : m_left);
    if (validSession) m_active->filesView()->setFocus(Qt::OtherFocusReason);
    QString message = restored.message;
    if (unavailable) message += (message.isEmpty() ? QString{} : " ") + tr("%1 pasta(s) salva(s) indisponível(is). As abas usam a pasta pessoal até você escolher outro local.").arg(unavailable);
    if (!message.isEmpty()) QTimer::singleShot(0, this, [this, message] { statusBar()->showMessage(message); });

}

void MainWindow::activate(FilePanel *panel) {
    m_active = panel; m_left->setActive(panel == m_left); m_right->setActive(panel == m_right);
    const auto index = m_treeModel->index(panel->path());
    if (index.isValid()) { m_tree->setCurrentIndex(index); m_tree->scrollTo(index); }
    if (!m_operationJob) statusBar()->showMessage(QDir::toNativeSeparators(panel->path()));
    preview(panel->selectedPath());
    updateOperationActions();
}
void MainWindow::updateOperationActions() {
    if (!m_copyAction) return;
    const bool available = !m_operationJob && !m_confirming && !m_viewerDialog;
    const auto selected = m_active->selectedPaths();
    m_openAction->setEnabled(selected.size() == 1);
    m_openTabAction->setEnabled(selected.size() == 1 && QFileInfo(selected.first()).isDir());
    m_clipboardCopyAction->setEnabled(available && !selected.isEmpty());
    m_clipboardCutAction->setEnabled(available && !selected.isEmpty());
    m_pasteAction->setEnabled(available && m_clipboard->snapshot().transfer.valid() && QFileInfo(m_active->path()).isDir());
    m_left->setTransfersEnabled(available); m_right->setTransfersEnabled(available);
    m_copyAction->setEnabled(available && !selected.isEmpty());
    m_moveAction->setEnabled(available && !selected.isEmpty());
    m_renameAction->setEnabled(available && selected.size() == 1);
    m_trashAction->setEnabled(available && !selected.isEmpty());
    m_folderAction->setEnabled(available && QFileInfo(m_active->path()).isDir());
}
void MainWindow::startOperation(OperationKind kind) {
    if (m_operationJob) return;
    OperationRequest request; request.kind = kind; request.nativeUi = m_nativeOperationUi;
    request.sources = kind == OperationKind::CreateFolder ? QStringList{} : m_active->selectedPaths();
    request.destination = (kind == OperationKind::Copy || kind == OperationKind::Move) ? (m_active == m_left ? m_right : m_left)->path() : m_active->path();
    if (kind != OperationKind::CreateFolder && request.sources.isEmpty()) return;
    if (kind == OperationKind::Rename) { if (request.sources.size() != 1) return; request.name = QFileInfo(request.sources.first()).fileName(); }
    if (kind == OperationKind::CreateFolder) request.name = tr("Nova pasta");
    runOperation(request);
}
void MainWindow::runOperation(OperationRequest request, std::optional<ClipboardSnapshot> clipboard) {
    if (m_operationJob || m_confirming || m_viewerDialog) return;
    request.nativeUi = m_nativeOperationUi;
    bool accepted = false;
    {
        QScopedValueRollback<bool> confirming(m_confirming, true); updateOperationActions();
        OperationDialog dialog(request, this); accepted = dialog.exec() == QDialog::Accepted;
        if (accepted) request = dialog.request();
    }
    if (!accepted) { updateOperationActions(); return; }
    const auto kind = request.kind; request.ownerWindow = m_nativeOperationUi ? quintptr(winId()) : 0;
    auto *job = new OperationJob(request, this); m_operationJob = job; updateOperationActions(); m_cancelOperation->setEnabled(true); m_cancelOperation->setVisible(true);
    m_operationProgress->setRange(0, 0); m_operationProgress->show(); statusBar()->showMessage(tr("Preparando: %1…").arg(operationLabel(kind)));
    connect(job, &OperationJob::progress, this, [this](quint32 total, quint32 done, const QString &item) {
        if (total > 0) { m_operationProgress->setRange(0, 100); m_operationProgress->setValue(int(qMin<quint64>(100, quint64(done) * 100 / total))); }
        if (!item.isEmpty() && m_cancelOperation->isEnabled()) statusBar()->showMessage((total > 0 ? tr("Executando: %1") : tr("Preparando: %1")).arg(QFileInfo(item).fileName()));
    });
    connect(job, &QThread::finished, this, [this, job, kind, clipboard] {
        job->wait(); const auto result = job->result();
        const auto clipboardError = clipboard && kind == OperationKind::Move ? m_clipboard->finishMove(*clipboard, result) : QString{};
        m_operationJob = nullptr; job->deleteLater();
        m_operationProgress->hide(); m_cancelOperation->setEnabled(false); m_cancelOperation->setVisible(false); m_left->refresh(); m_right->refresh(); updateOperationActions();
        QString title;
        switch (result.state) {
        case OperationState::Completed: title = tr("Operação concluída"); break;
        case OperationState::Cancelled: title = tr("Operação cancelada"); break;
        case OperationState::Failed: title = tr("A operação encontrou um erro"); break;
        case OperationState::Incomplete: title = tr("Operação parcialmente concluída"); break;
        }
        const auto summary = tr("%1 — %2 de %3 itens concluídos.").arg(operationLabel(kind)).arg(result.completedCount()).arg(result.items.size());
        statusBar()->showMessage(title + " — " + summary);
        if (m_closeAfterOperation) { close(); return; }
        auto *message = new QMessageBox(result.state == OperationState::Completed && clipboardError.isEmpty() ? QMessageBox::Information : QMessageBox::Warning,
            title, summary, QMessageBox::Ok, this);
        message->setObjectName("operationResult"); message->setAttribute(Qt::WA_DeleteOnClose);
        QStringList details;
        if (!clipboardError.isEmpty()) { details.append(clipboardError); message->setInformativeText(clipboardError); }
        if (!result.error.isEmpty()) details.append(result.error);
        for (const auto &item : result.items) {
            QString state;
            switch (item.state) {
            case OperationItemState::Completed: state = tr("Concluído"); break;
            case OperationItemState::Skipped: state = tr("Ignorado"); break;
            case OperationItemState::Cancelled: state = tr("Cancelado"); break;
            case OperationItemState::Failed: state = tr("Erro"); break;
            case OperationItemState::Incomplete: state = tr("Parcial / não confirmado"); break;
            case OperationItemState::NotStarted: state = tr("Não iniciado"); break;
            }
            details.append(state + ": " + QDir::toNativeSeparators(item.source) + (item.target.isEmpty() ? QString{} : " → " + QDir::toNativeSeparators(item.target))
                + (item.error.isEmpty() ? QString{} : "\n" + item.error));
        }
        message->setDetailedText(details.join('\n')); message->open();
    });
    job->start();
}
MainWindow::~MainWindow() { if (m_operationJob) { m_operationJob->cancel(); m_operationJob->wait(); } }
void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_confirming) { event->ignore(); return; }
    if (m_viewerDialog) { event->ignore(); m_closeAfterViewer = true; m_viewerDialog->reject(); return; }
    if (!m_operationJob) { finishClose(event); return; }
    event->ignore();
    QMessageBox question(QMessageBox::Question, tr("Operação em andamento"), tr("Cancelar a operação e fechar após ela encerrar?"), QMessageBox::Yes | QMessageBox::No, this);
    question.setDefaultButton(QMessageBox::No);
    question.button(QMessageBox::Yes)->setText(tr("Sim")); question.button(QMessageBox::No)->setText(tr("Não"));
    if (question.exec() == QMessageBox::Yes) {
        // The nested question event loop may have delivered job completion.
        if (!m_operationJob) { finishClose(event); return; }
        m_closeAfterOperation = true; m_operationJob->cancel(); m_cancelOperation->setEnabled(false); statusBar()->showMessage(tr("Aguardando o encerramento da operação…"));
    }
}
WindowSession MainWindow::session() const {
    WindowSession state; state.left = m_left->session(); state.right = m_right->session();
    state.geometry = saveGeometry(); state.windowState = saveState(1); state.splitterState = m_splitter->saveState();
    state.dark = m_darkAction->isChecked(); state.dualPane = m_dualAction->isChecked(); state.treeVisible = m_treeAction->isChecked();
    state.previewVisible = !m_preview->isHidden(); state.activePanel = m_active == m_right ? 1 : 0; state.favorites = m_favorites;
    state.folderViews = m_folderViews->entries(); return state;
}
void MainWindow::refreshFavorites() {
    m_favoritesMenu->clear(); m_favoritesList->clear();
    for (const auto &entry : m_favorites) {
        auto menuLabel = entry.label; menuLabel.replace("&", "&&");
        auto *a = m_favoritesMenu->addAction(style()->standardIcon(QStyle::SP_DirIcon), menuLabel);
        a->setData(entry.id); a->setToolTip(QDir::toNativeSeparators(entry.path));
        connect(a, &QAction::triggered, this, [this, id = entry.id] { openFavorite(id); });
        auto *item = new QListWidgetItem(style()->standardIcon(QStyle::SP_DirIcon), entry.label, m_favoritesList);
        item->setData(Qt::UserRole, entry.id); item->setToolTip(QDir::toNativeSeparators(entry.path));
    }
    if (m_favorites.isEmpty()) { auto *empty = m_favoritesMenu->addAction(tr("Nenhum favorito")); empty->setEnabled(false); }
    m_favoritesMenu->addSeparator(); m_favoritesMenu->addAction(findChild<QAction *>("addCurrentFavorite")); m_favoritesMenu->addAction(findChild<QAction *>("manageFavorites"));
}
void MainWindow::openFavorite(const QString &id) {
    for (const auto &entry : m_favorites) if (entry.id == id) {
        if (!m_active->navigate(entry.path)) statusBar()->showMessage(tr("O favorito “%1” está indisponível. Seu atalho foi mantido.").arg(entry.label));
        return;
    }
}
void MainWindow::addCurrentFavorite() {
    const auto path = m_active->path();
    for (const auto &entry : m_favorites) if (QDir::cleanPath(entry.path).compare(QDir::cleanPath(path), Qt::CaseInsensitive) == 0) { statusBar()->showMessage(tr("Esta pasta já está nos favoritos.")); return; }
    if (m_favorites.size() >= 64) { statusBar()->showMessage(tr("O limite de 64 favoritos foi atingido. Use Organizar favoritos para editar a lista.")); return; }
    const auto name = QFileInfo(path).fileName();
    m_favorites.append({QUuid::createUuid().toString(QUuid::WithoutBraces), name.isEmpty() ? QDir::toNativeSeparators(path) : name, path}); refreshFavorites();
    statusBar()->showMessage(tr("Favorito adicionado. Será salvo ao fechar o Directory Open."));
}
void MainWindow::editFavorites() {
    FavoritesDialog dialog(m_favorites, m_active->path(), this);
    if (dialog.exec() == QDialog::Accepted) { m_favorites = dialog.favorites(); refreshFavorites(); statusBar()->showMessage(tr("Favoritos atualizados. Serão salvos ao fechar o Directory Open.")); }
}
void MainWindow::showComparison() {
    if (!m_comparisonDialog) {
        m_comparisonDialog = new ComparisonDialog(m_left->path(), m_right->path(), this);
        connect(m_comparisonDialog, &ComparisonDialog::openFolderRequested, this, [this](const QString &path, bool left) {
            auto *panel = left ? m_left : m_right;
            if (!left) m_dualAction->setChecked(true);
            panel->newTab(path); activate(panel);
        });
    }
    m_comparisonDialog->show(); m_comparisonDialog->raise(); m_comparisonDialog->activateWindow();
}
void MainWindow::showSearch() {
    if (!m_searchDialog) {
        m_searchDialog = new SearchDialog(m_active->path(), this);
        connect(m_searchDialog, &SearchDialog::openFolderRequested, this, [this](const QString &path) { m_active->newTab(path); });
    }
    m_searchDialog->show(); m_searchDialog->raise(); m_searchDialog->activateWindow();
}
void MainWindow::finishClose(QCloseEvent *event) {
    if (m_sessionStore.writable()) {
        const auto error = m_sessionStore.save(session());
        if (!error.isEmpty()) QMessageBox::warning(this, tr("Guardar sessão"), tr("Não foi possível guardar esta sessão.\n\n%1").arg(error));
    }
    QMainWindow::closeEvent(event);
}
void MainWindow::showFileContextMenu(const QPoint &position) {
    updateOperationActions();
    QMenu menu(this); menu.setObjectName("fileContextMenu");
    menu.addActions({m_openAction, m_openTabAction}); menu.addSeparator();
    menu.addActions({m_clipboardCopyAction, m_clipboardCutAction, m_pasteAction});
    auto *between = menu.addMenu(tr("Para o outro painel")); between->addActions({m_copyAction, m_moveAction});
    menu.addSeparator(); menu.addActions({m_renameAction, m_trashAction}); menu.addSeparator();
    menu.addActions({m_folderAction, m_copyPathAction, m_refreshAction});
    menu.exec(position);
}
void MainWindow::prepareClipboard(OperationKind kind) {
    if (m_operationJob || m_confirming || m_viewerDialog) return;
    const auto paths = m_active->selectedPaths(); if (paths.isEmpty()) return;
    const auto error = m_clipboard->publish(paths, kind); updateClipboardVisuals();
    if (!error.isEmpty()) { statusBar()->showMessage(error); return; }
    const auto selection = paths.size() == 1 ? tr("1 item preparado") : tr("%1 itens preparados").arg(paths.size());
    statusBar()->showMessage(selection + (kind == OperationKind::Move ? tr(" para mover. Abra o destino e use Colar.")
        : tr(" para copiar. Abra o destino e use Colar.")));
}
void MainWindow::pasteFiles() {
    if (m_operationJob || m_confirming || m_viewerDialog) return;
    const auto clipboard = m_clipboard->snapshot(); if (!clipboard.transfer.valid()) return;
    OperationRequest request; request.sources = clipboard.transfer.paths; request.kind = clipboard.transfer.kind; request.destination = m_active->path();
    runOperation(request, clipboard);
}
void MainWindow::updateClipboardVisuals() {
    const auto transfer = m_clipboard->snapshot().transfer;
    const auto paths = transfer.valid() && transfer.kind == OperationKind::Move ? transfer.paths : QStringList{};
    m_left->setCutPaths(paths); m_right->setCutPaths(paths); updateOperationActions();
}
void MainWindow::preview(const QString &path) {
    if (m_preview && m_preview->isVisible()) m_previewWidget->setPath(path);
}
void MainWindow::showViewer(bool fullScreen) {
    if (m_confirming) return;
    if (auto *dialog = qobject_cast<QDialog *>(m_previewWidget->window())) {
        if (dialog->isFullScreen()) dialog->showNormal(); else dialog->showFullScreen(); return;
    }
    m_preview->show(); preview(m_active->selectedPath());
    QDialog dialog(this); m_viewerDialog = &dialog; updateOperationActions(); dialog.setObjectName("viewerDialog"); dialog.setWindowTitle(tr("Directory Open — Visualizador")); dialog.resize(1000, 800);
    auto *layout = new QVBoxLayout(&dialog); layout->setContentsMargins(0, 0, 0, 0);
    m_preview->setWidget(nullptr); layout->addWidget(m_previewWidget); m_previewWidget->show();
    auto *toggleScreen = new QShortcut(QKeySequence("F11"), &dialog);
    connect(toggleScreen, &QShortcut::activated, &dialog, [&dialog] { if (dialog.isFullScreen()) dialog.showNormal(); else dialog.showFullScreen(); });
    if (fullScreen) dialog.showFullScreen();
    dialog.exec(); m_preview->setWidget(m_previewWidget); m_previewWidget->show();
    m_viewerDialog.clear(); updateOperationActions();
    // Restore the dock ownership/layout before applying a pending window close.
    if (m_closeAfterViewer) { m_closeAfterViewer = false; QTimer::singleShot(0, this, &QWidget::close); }
}
void MainWindow::applyTheme(bool dark) {
    const QString bg = dark ? "#20252c" : "#f3f5f7";
    const QString surface = dark ? "#181d23" : "#ffffff";
    const QString fg = dark ? "#dce3ed" : "#243043";
    const QString border = dark ? "#36404d" : "#d4dce5";
    const QString selected = dark ? "#254a70" : "#dcecff";
    qApp->setStyleSheet(QString(R"(
        QWidget { font-family: 'Segoe UI'; font-size: 9pt; color: %1; }
        QMainWindow, QDialog, QMessageBox, QMenuBar, QToolBar, QStatusBar, QWidget#sidebar { background: %2; }
        QMenuBar { padding: 3px 6px; border-bottom: 1px solid %4; }
        QMenuBar::item { padding: 5px 10px; } QMenuBar::item:selected { background: %5; }
        QMenu { background: %3; border: 1px solid %4; padding: 4px; }
        QMenu::item { padding: 6px 22px; } QMenu::item:selected { background: %5; }
        QMenu::item:disabled { color: #85909e; }
        QToolBar { spacing: 3px; padding: 4px; border-bottom: 1px solid %4; }
        QToolBar::separator { background: %4; width: 1px; margin: 4px 7px; }
        QToolButton { border: 1px solid transparent; padding: 4px 6px; border-radius: 3px; }
        QToolButton:hover, QToolButton:checked { background: %5; border-color: %4; }
        QToolButton:disabled { color: #85909e; }
        QPushButton, QComboBox { background: %3; border: 1px solid %4; border-radius: 3px; padding: 6px 10px; }
        QPushButton:hover { background: %5; }
        QPushButton:default { background: #397bb7; color: white; border-color: #397bb7; }
        QComboBox QAbstractItemView { background: %3; selection-background-color: %5; }
        QToolBar#panelNavigation { background: %2; padding: 4px; }
        QToolBar#panelNavigation[active='true'] { background: %5; border-bottom: 2px solid #4c8ac5; }
        QLineEdit { background: %3; border: 1px solid %4; border-radius: 3px; padding: 4px 6px; selection-background-color: #397bb7; }
        QTreeView, QListView, QListWidget, QPlainTextEdit { background: %3; border: 0; selection-background-color: %5; selection-color: %1; }
        QTreeView::item { height: 25px; } QListWidget::item { height: 28px; padding-left: 7px; }
        QTreeView::item:hover, QListView::item:hover, QListWidget::item:hover { background: %2; }
        QHeaderView::section { background: %2; color: %1; border: 0; border-right: 1px solid %4; border-bottom: 1px solid %4; padding: 5px 7px; }
        QLabel#sectionTitle { background: %2; border-bottom: 1px solid %4; padding: 5px; font-weight: 600; }
        QSplitter::handle { background: %4; width: 3px; }
        QTabBar::tab { background: %2; border: 1px solid %4; padding: 5px 12px; min-width: 55px; }
        QTabBar::tab:selected { background: %5; border-bottom: 2px solid #4c8ac5; }
        QWidget#panelFooter { background: %2; border-top: 1px solid %4; }
        QStatusBar { border-top: 1px solid %4; padding: 3px; }
        QDockWidget { background: %3; } QDockWidget::title { background: %2; padding: 6px; }
        QScrollBar:vertical { background: %2; width: 12px; } QScrollBar::handle:vertical { background: %4; min-height: 30px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
    )").arg(fg, bg, surface, border, selected));
    m_left->setActive(m_active == m_left); m_right->setActive(m_active == m_right);
}
