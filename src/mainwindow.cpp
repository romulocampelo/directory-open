#include "mainwindow.h"
#include "interfacecommands.h"
#include "globalhotkeys.h"
#include "annotationsdialog.h"
#include "ocrdialog.h"
#include "teracopy.h"
#include "archivedialog.h"
#include "toolsdialog.h"
#include "specializedlaunchdialog.h"
#include "userscriptdialog.h"
#include "windowsintegrationdialog.h"
#include "mediatoolsdialog.h"
#include "preferencesdialog.h"
#include "filepanel.h"
#include "operationdialog.h"
#include "operationqueuedialog.h"
#include "batchrenamedialog.h"
#include "renamehistorydialog.h"
#include "favoritesdialog.h"
#include "searchdialog.h"
#include "foldersizedialog.h"
#include "comparisondialog.h"
#include "synchronizationdialog.h"
#include "previewwidget.h"
#include "thumbnailcache.h"
#include "shellcontextmenu.h"
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
#include <QPalette>
#include <QSet>
#include <QScopedValueRollback>
#include <QSignalBlocker>
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
#include <algorithm>
#include <functional>

MainWindow::MainWindow(const QString &left, const QString &right, bool nativeOperationUi, const QString &sessionFile) : m_nativeOperationUi(nativeOperationUi), m_sessionStore(sessionFile) {
    m_sessionPath=sessionFile;
    if (!sessionFile.isEmpty()) m_renameHistoryRoot = QFileInfo(sessionFile).absoluteFilePath() + ".rename-history";
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
    auto *treeTitle = new QLabel(tr("  Árvore de pastas"), this); treeTitle->setObjectName("treeSectionTitle"); sideLayout->addWidget(treeTitle);
    m_treeModel = new QFileSystemModel(this); m_treeModel->setReadOnly(true);
    m_treeModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Drives);
    m_treeModel->setRootPath({});
    m_tree = new QTreeView(this); m_tree->setObjectName("folderTree"); m_tree->setModel(m_treeModel);
    m_tree->setHeaderHidden(true); m_tree->setUniformRowHeights(true);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers); m_tree->setAnimated(false);
    for (int column = 1; column < 4; ++column) m_tree->hideColumn(column);
    sideLayout->addWidget(m_tree, 1); panes->addWidget(sidebar);
    sideLayout->addStretch(0);
    for(auto *title:{placesTitle,favoritesTitle,treeTitle})title->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);
    m_folderViews = new FolderViewCatalog(this);
    auto *thumbnails = new ThumbnailCache(this); thumbnails->setObjectName("thumbnailCache");
    m_left = new FilePanel(this, m_folderViews, thumbnails); m_right = new FilePanel(this, m_folderViews, thumbnails); m_active = m_left;
    panes->addWidget(m_left); panes->addWidget(m_right);
    panes->setSizes({230, 580, 580}); panes->setCollapsible(0, true); panes->setCollapsible(1, false); panes->setCollapsible(2, false);
    setCentralWidget(panes);
    connect(places, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { m_active->navigate(item->data(Qt::UserRole).toString()); });
    connect(m_tree, &QTreeView::clicked, this, [this](const QModelIndex &index) { m_active->navigate(m_treeModel->filePath(index)); });
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeView::customContextMenuRequested, this, [this](const QPoint&p){showTreeContextMenu(p);});
    for (auto *panel : {m_left, m_right}) {
        connect(panel, &FilePanel::transferRequested, this, [this](FileTransfer transfer, const QString &destination) {
            OperationRequest request; request.kind = transfer.kind; request.sources = transfer.paths; request.destination = destination;
            runOperation(request);
        });
        connect(panel, &FilePanel::contextMenuRequested, this, &MainWindow::showFileContextMenu);
        connect(panel,&FilePanel::treeContextMenuRequested,this,[this,panel](QTreeView *tree,const QPoint&p){activate(panel);showTreeContextMenu(tree,p);});
        connect(panel, &FilePanel::activated, this, [this, panel] { activate(panel); });
        connect(panel, &FilePanel::openRequested, this, [this, panel](const QString &path) {
            activate(panel); openPath(panel, path);
        });
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
        openPath(m_active, m_active->selectedPath());
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
    m_batchRenameAction = action("batchRename", tr("Renomear em lote…"), QStyle::SP_FileDialogListView, QKeySequence("Ctrl+F2"), [this] { startBatchRename(); });
    m_folderAction = action("createFolder", tr("Nova pasta"), QStyle::SP_FileDialogNewFolder, QKeySequence("Ctrl+Shift+N"), [this] { startOperation(OperationKind::CreateFolder); });
    m_trashAction = action("trashFiles", tr("Lixeira"), QStyle::SP_TrashIcon, QKeySequence("Delete"), [this] { startOperation(OperationKind::Trash); });
    m_trashAction->setToolTip(tr("Enviar seleção à Lixeira (Delete)"));
    m_trashAction->setShortcutContext(Qt::WidgetShortcut);
    for (auto *panel : {m_left, m_right}) panel->addFileAction(m_trashAction);
    m_cancelOperation = action("cancelOperation", tr("Cancelar operação"), QStyle::SP_DialogCancelButton, {}, [this] {
        if (m_operationJob) { m_operationJob->cancel(); m_cancelOperation->setEnabled(false); statusBar()->showMessage(tr("Cancelamento solicitado. Aguardando o encerramento da operação…")); }
    });
    m_cancelOperation->setEnabled(false);
    m_cancelOperation->setVisible(false);
    action("address", tr("Editar endereço"), QStyle::SP_DirIcon, QKeySequence("Ctrl+L"), [this] { m_active->focusAddress(); });
    action("filter", tr("Filtrar pasta"), QStyle::SP_FileDialogContentsView, QKeySequence("Ctrl+F"), [this] { m_active->focusFilter(); });
    auto *folderSize = action("calculateFolderSize", tr("Calcular tamanho das pastas…"), QStyle::SP_DirIcon, {}, [this] { showFolderSize(); });
    auto *search = action("searchFiles", tr("Buscar"), QStyle::SP_FileDialogContentsView, QKeySequence("Ctrl+Shift+F"), [this] { showSearch(); });
    search->setToolTip(tr("Buscar nomes na pasta atual e nas subpastas (Ctrl+Shift+F)"));
    action("back", tr("Voltar"), QStyle::SP_ArrowBack, QKeySequence("Alt+Left"), [this] { m_active->back(); });
    action("forward", tr("Avançar"), QStyle::SP_ArrowForward, QKeySequence("Alt+Right"), [this] { m_active->forward(); });
    action("up", tr("Pasta acima"), QStyle::SP_ArrowUp, QKeySequence("Alt+Up"), [this] { m_active->up(); });
    auto *refresh = action("refresh", tr("Atualizar"), QStyle::SP_BrowserReload, QKeySequence("F5"), [this] { m_active->refresh(); });
    m_refreshAction = refresh;
    auto *fileMenu = menuBar()->addMenu(tr("&Arquivo")); fileMenu->setObjectName("fileMenu"); fileMenu->addActions({newTab, closeTab, open}); fileMenu->addSeparator();
    fileMenu->addAction(tr("Sair"), QKeySequence("Alt+F4"), this, &QWidget::close)->setObjectName("exitApplication");
    auto *editMenu = menuBar()->addMenu(tr("&Editar")); editMenu->setObjectName("editMenu"); editMenu->addActions({m_clipboardCopyAction, m_clipboardCutAction, m_pasteAction}); editMenu->addSeparator();
    auto *transferMenu = editMenu->addMenu(tr("Para o outro painel")); transferMenu->addActions({m_copyAction, m_moveAction});
    editMenu->addActions({m_renameAction, m_folderAction, m_trashAction}); editMenu->addSeparator(); editMenu->addAction(copyPath); editMenu->addAction(m_cancelOperation);
    auto *viewMenu = menuBar()->addMenu(tr("&Exibir")); viewMenu->setObjectName("viewMenu");
    auto *dual = new QAction(tr("Dois painéis"), this); dual->setObjectName("dualPane"); dual->setCheckable(true); dual->setChecked(true);
    m_dualAction = dual;
    dual->setShortcut(QKeySequence("F6")); addAction(dual);
    connect(dual, &QAction::toggled, this, [this](bool on) { m_right->setVisible(on); if (!on) activate(m_left); }); viewMenu->addAction(dual);
    auto *treeToggle = viewMenu->addAction(tr("Árvore de pastas")); treeToggle->setCheckable(true); treeToggle->setChecked(true);
    m_treeAction = treeToggle; treeToggle->setObjectName("treeVisible");
    m_independentTreesAction=new QAction(tr("Uma árvore por painel (Consome mais CPU)"),this);m_independentTreesAction->setObjectName("independentTrees");m_independentTreesAction->setCheckable(true);
    connect(treeToggle,&QAction::toggled,this,&MainWindow::applyTrees);connect(m_independentTreesAction,&QAction::toggled,this,&MainWindow::applyTrees);
    auto *dark = viewMenu->addAction(tr("Tema escuro")); dark->setObjectName("darkTheme"); dark->setCheckable(true);
    m_darkAction = dark;
    connect(dark, &QAction::toggled, this, &MainWindow::applyTheme);
    auto *folderMenu = menuBar()->addMenu(tr("&Pastas")); folderMenu->setObjectName("folderMenu"); folderMenu->addAction(newTab); folderMenu->addAction(m_folderAction); folderMenu->addAction(refresh);
    auto *compare = action("compareFolders", tr("Comparar pastas…"), QStyle::SP_FileDialogDetailedView, QKeySequence("Ctrl+Shift+K"), [this] { showComparison(); });
    m_renameHistoryAction = action("renameHistory", tr("Histórico de renomeações…"), QStyle::SP_BrowserReload, QKeySequence("Ctrl+Shift+R"), [this] { showRenameHistory(); });
    auto *toolsMenu = menuBar()->addMenu(tr("&Ferramentas")); toolsMenu->setObjectName("toolsMenu"); toolsMenu->addAction(search); toolsMenu->addAction(folderSize); toolsMenu->addAction(compare); toolsMenu->addAction(m_batchRenameAction); toolsMenu->addAction(m_renameHistoryAction); toolsMenu->addAction(copyPath);
    m_packAction = action("createArchive", tr("Criar compactado…"), QStyle::SP_DriveFDIcon, {}, [this] { showArchive(true); });
    m_archiveAction = action("browseArchive", tr("Abrir compactado…"), QStyle::SP_DirOpenIcon, {}, [this] { showArchive(false); });
    toolsMenu->addSeparator(); toolsMenu->addActions({m_packAction, m_archiveAction});
    m_contentAction = action("searchContent", tr("Buscar conteúdo…"), QStyle::SP_FileDialogContentsView, QKeySequence("Ctrl+Alt+F"), [this] { showTools(0); });
    m_backupAction = action("versionedBackup", tr("Backups com versões…"), QStyle::SP_DriveHDIcon, {}, [this] { showTools(1); });
    m_remoteAction = action("remoteStorage", tr("Armazenamento remoto…"), QStyle::SP_DriveNetIcon, {}, [this] { showTools(2); });
    toolsMenu->addSeparator(); toolsMenu->addActions({m_contentAction, m_backupAction, m_remoteAction});
    toolsMenu->addAction(action("operationQueue",tr("Preparar seleção na fila…"),QStyle::SP_FileDialogListView,QKeySequence("Ctrl+Alt+Q"),[this]{showOperationQueue();}));
    toolsMenu->addAction(action("duplicateFinder",tr("Duplicador especializado…"),QStyle::SP_FileDialogContentsView,{},[this]{showDuplicateFinder();}));
    toolsMenu->addAction(action("userScripts",tr("Scripts e formulários…"),QStyle::SP_FileDialogDetailedView,{},[this]{showUserScript();}));
    m_metadataAction = action("editMetadata", tr("Metadados…"), QStyle::SP_FileDialogInfoView, {}, [this] { showMediaTools(0); });
    m_imageConvertAction = action("convertImages", tr("Converter imagens…"), QStyle::SP_FileDialogContentsView, {}, [this] { showMediaTools(1); });
    m_mediaConvertAction = action("convertMedia", tr("Áudio e vídeo…"), QStyle::SP_MediaPlay, {}, [this] { showMediaTools(2); });
    toolsMenu->addSeparator(); toolsMenu->addActions({m_metadataAction, m_imageConvertAction, m_mediaConvertAction});
    auto *settingsMenu = menuBar()->addMenu(tr("&Configurações")); settingsMenu->setObjectName("settingsMenu");
    auto *preferences = action("preferences", tr("Preferências…"), QStyle::SP_FileDialogDetailedView, QKeySequence("Ctrl+,"), [this] { showPreferences(); });
    auto *commands = action("commandCenter", tr("Central de comandos…"), QStyle::SP_FileDialogListView, QKeySequence("Ctrl+Shift+P"), [this] { showCommands(); });
    settingsMenu->addActions({preferences, commands,m_independentTreesAction}); settingsMenu->addSeparator();
    m_nativeContextMenuAction = settingsMenu->addAction(tr("Menu de contexto do Windows (Consome mais CPU)"));
    m_nativeContextMenuAction->setObjectName("nativeContextMenu"); m_nativeContextMenuAction->setCheckable(true); m_nativeContextMenuAction->setChecked(true);
    m_nativeContextMenuAction->setToolTip(tr("Inclui ações dos programas instalados. Desative para usar o menu simples do Directory Open. A escolha é guardada ao fechar normalmente."));
    m_teraCopyAction = settingsMenu->addAction(tr("Preferir TeraCopy nas cópias"));
    m_teraCopyAction->setObjectName("teraCopyForCopy"); m_teraCopyAction->setCheckable(true);
    m_teraCopyAction->setToolTip(tr("Usa o TeraCopy instalado, com progresso e verificação na janela dele. A cópia será confirmada antes de encaminhar. Movimentação continua com Windows."));
    auto *records = settingsMenu->addAction(tr("Abrir registros de cópias TeraCopy"));
    records->setObjectName("teraCopyRecords");
    connect(records, &QAction::triggered, this, [this] {
        const auto path = teraCopyRecordsPath();
        if (!path.isEmpty() && QDir().mkpath(path)) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        else statusBar()->showMessage(tr("A pasta de registros não está disponível."));
    });
    m_customMenu = menuBar()->addMenu(tr("Meus comandos")); m_customMenu->setObjectName("customCommandsMenu");
    auto *helpMenu = menuBar()->addMenu(tr("A&juda")); helpMenu->setObjectName("helpMenu"); helpMenu->addAction(tr("Sobre o Directory Open"), this, [this] {
        QMessageBox::about(this, tr("Directory Open"), tr("Directory Open %1\nGerenciador de arquivos aberto.\n\nNavegação, operações de arquivos, visualização, comparação de pastas e sessão.").arg(QString::fromLatin1(DIROPEN_VERSION)));
    })->setObjectName("aboutApplication");
    auto *toolbar = addToolBar(tr("Comandos")); toolbar->setObjectName("commands");
    m_commandToolbar = toolbar;
    toolbar->setMovable(false); toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); toolbar->setIconSize(QSize(18, 18));
    toolbar->addAction(open); toolbar->addSeparator(); toolbar->addAction(m_copyAction);
    toolbar->addActions({m_moveAction, m_renameAction, m_batchRenameAction, m_folderAction, m_trashAction});
    toolbar->addSeparator(); toolbar->addAction(m_pasteAction); toolbar->addAction(copyPath); toolbar->addAction(refresh); toolbar->addAction(m_cancelOperation); toolbar->addSeparator();
    toolbar->addAction(dual);
    toolbar->addAction(search); toolbar->addAction(compare);
    auto *favorites = new QToolButton(this); favorites->setText(tr("Favoritos")); favorites->setIcon(style()->standardIcon(QStyle::SP_DirHomeIcon));
    favorites->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); favorites->setPopupMode(QToolButton::InstantPopup);
    m_favoritesMenu = new QMenu(favorites); m_favoritesMenu->setObjectName("favoritesMenu");
    favorites->setMenu(m_favoritesMenu); toolbar->addWidget(favorites)->setObjectName("favoritesPopup");
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
    auto *previewAction = m_preview->toggleViewAction(); previewAction->setObjectName("togglePreview"); previewAction->setShortcut(QKeySequence("F7"));
    addAction(previewAction); viewMenu->addAction(previewAction); toolbar->addAction(previewAction);
    connect(m_preview, &QDockWidget::visibilityChanged, this, [this](bool visible) { if (visible) preview(m_active->selectedPath()); else m_previewWidget->setPath({}); });
    m_operationProgress = new QProgressBar(this); m_operationProgress->setMaximumWidth(220); m_operationProgress->hide(); statusBar()->addPermanentWidget(m_operationProgress);
    statusBar()->addPermanentWidget(new QLabel(tr("Directory Open %1").arg(QString::fromLatin1(DIROPEN_VERSION)), this));
    for (const auto &a : toolbar->actions()) m_defaultToolbar.append(a->isSeparator() ? "-" : a->objectName());
    toolbar->addSeparator(); toolbar->addAction(commands); toolbar->addAction(preferences);
    m_defaultToolbar.append("-"); m_defaultToolbar.append("commandCenter"); m_defaultToolbar.append("preferences");
    auto *folderDisplayMenu = viewMenu->addMenu(tr("Exibição da pasta ativa"));
    const QList<QPair<QString,QString>> folderCommands{{"viewDetails",tr("Detalhes")},{"viewNames",tr("Somente nomes")},{"viewThumbnails",tr("Miniaturas")},{"rememberFolderView",tr("Guardar exibição desta pasta")},{"forgetFolderView",tr("Esquecer exibição desta pasta")},{"manageFolderViews",tr("Exibições guardadas…")}};
    for (const auto &entry : folderCommands) folderDisplayMenu->addAction(action("folder_"+entry.first, entry.second, QStyle::SP_FileDialogDetailedView, {}, [this, id=entry.first] { if (auto *a=m_active->findChild<QAction *>(id)) a->trigger(); }));
    folderMenu->addAction(action("duplicateTab",tr("Duplicar aba"),QStyle::SP_FileDialogNewFolder,QKeySequence("Ctrl+Shift+D"),[this]{m_active->duplicateTab();}));
    folderMenu->addAction(action("lockTab",tr("Bloquear/desbloquear pasta da aba"),QStyle::SP_DialogApplyButton,{},[this]{m_active->setTabLocked(!m_active->tabLocked());}));
    folderDisplayMenu->addAction(action("advancedFolderFilter",tr("Filtro combinado da aba…"),QStyle::SP_FileDialogContentsView,QKeySequence("Ctrl+Shift+L"),[this]{m_active->editRules();}));
    auto *groupsMenu=folderDisplayMenu->addMenu(tr("Agrupar por"));
    for(int i=0;i<4;++i)groupsMenu->addAction(action(QString("groupFolder%1").arg(i),QStringList{tr("Sem agrupamento"),tr("Tipo / extensão"),tr("Mês de modificação"),tr("Faixa de tamanho")}[i],QStyle::SP_FileDialogListView,{},[this,i]{m_active->setGrouping(i);}));
    settingsMenu->addAction(action("manageWorkspaces",tr("Layouts e grupos de abas…"),QStyle::SP_FileDialogDetailedView,QKeySequence("Ctrl+Alt+L"),[this]{showWorkspaces();}));
    settingsMenu->addAction(action("interfaceStudio",tr("Personalização avançada…"),QStyle::SP_FileDialogDetailedView,QKeySequence("Ctrl+Alt+,"),[this]{showInterfaceStudio();}));
    settingsMenu->addAction(action("globalKeys",tr("Atalhos globais…"),QStyle::SP_ComputerIcon,{},[this]{showGlobalKeys();}));
    settingsMenu->addAction(action("viewerSettings",tr("Preferências do visualizador…"),QStyle::SP_FileDialogDetailedView,{},[this]{showViewerSettings();}));
    settingsMenu->addAction(action("windowsIntegration",tr("Associações e abertura de pastas…"),QStyle::SP_ComputerIcon,{},[this]{showWindowsIntegration();}));
    toolsMenu->addAction(action("classifyFiles",tr("Classificar seleção…"),QStyle::SP_DialogApplyButton,{},[this]{showAnnotations(false);}));
    toolsMenu->addAction(action("triageImages",tr("Triar imagens…"),QStyle::SP_FileDialogContentsView,{},[this]{showAnnotations(true);}));
    connect(m_previewWidget,&PreviewWidget::settingsRequested,this,&MainWindow::showViewerSettings);
    toolsMenu->addAction(action("recognizePreview",tr("OCR da prévia…"),QStyle::SP_FileDialogContentsView,{},[this]{showOcr();}));
    connect(m_previewWidget,&PreviewWidget::ocrRequested,this,&MainWindow::showOcr);
    connect(m_previewWidget,&PreviewWidget::displayed,this,&MainWindow::updateOperationActions);
    initializeCommands();captureInterfaceDefaults();
    m_globalHotkeys=new GlobalHotkeys(this);connect(m_globalHotkeys,&GlobalHotkeys::commandRequested,this,[this](const QString &id){if(operationBusy()||m_confirming||m_viewerDialog||qApp->activeModalWidget())return;for(const auto &c:m_commands)if(c.id==id&&c.action->isEnabled()){if(isMinimized())showNormal();raise();activateWindow();c.action->trigger();return;}});
    const auto restored = m_sessionStore.load();
    const bool validSession = restored.state == SessionLoadState::Loaded || restored.state == SessionLoadState::Recovered;
    int unavailable = 0;
    if (validSession) {
        if (restored.session.favorites) m_favorites = *restored.session.favorites;
        if (restored.session.folderViews) m_folderViews->replace(*restored.session.folderViews);
        unavailable += m_left->restoreSession(restored.session.left); unavailable += m_right->restoreSession(restored.session.right);
        if (!restored.session.geometry.isEmpty()) restoreGeometry(restored.session.geometry);

        m_dualAction->setChecked(restored.session.dualPane); m_treeAction->setChecked(restored.session.treeVisible);
        { const QSignalBlocker block(m_darkAction); m_darkAction->setChecked(restored.session.dark); }
        m_preview->setVisible(restored.session.previewVisible);
        m_nativeContextMenuAction->setChecked(restored.session.nativeContextMenu);
        m_teraCopyAction->setChecked(restored.session.teraCopyForCopy);
        m_independentTreesAction->setChecked(restored.session.independentTrees);
        m_tools = restored.session.tools;
        m_layouts=restored.session.layouts;m_tabGroups=restored.session.tabGroups;
        m_ui = restored.session.ui;
        if (!restored.session.splitterState.isEmpty()) panes->restoreState(restored.session.splitterState);
    }
    if (!left.isEmpty()) m_left->navigate(left);
    if (!right.isEmpty()) m_right->navigate(right);
    refreshFavorites();
    connect(m_clipboard, &FileClipboard::changed, this, &MainWindow::updateClipboardVisuals);
    updateClipboardVisuals();
    const auto keyError = validateUiShortcuts(m_ui, m_commands);
    if (!keyError.isEmpty()) m_ui.shortcuts.clear();
    synchronizeLaunchers();applyCustomization();applyTrees();
    if(validSession&&!restored.session.windowState.isEmpty())restoreState(restored.session.windowState,1);
    activate(validSession && restored.session.activePanel == 1 && m_dualAction->isChecked() ? m_right : m_left);
    if (validSession) m_active->filesView()->setFocus(Qt::OtherFocusReason);
    QString message = restored.message;
    if (!keyError.isEmpty()) message += " " + keyError + tr(" Atalhos padrão restaurados; a sessão anterior será preservada no backup.");
    if (unavailable) message += (message.isEmpty() ? QString{} : " ") + tr("%1 pasta(s) salva(s) indisponível(is). As abas usam a pasta pessoal até você escolher outro local.").arg(unavailable);
    if (!message.isEmpty()) QTimer::singleShot(0, this, [this, message] { statusBar()->showMessage(message); });

}

void MainWindow::openPath(FilePanel *panel, const QString &path) {
    if (path.isEmpty()) return;
    if (QFileInfo(path).isDir()) { panel->navigate(path); return; }
    m_previewWidget->releaseMedia();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        QMessageBox::information(this, tr("Abrir arquivo"), tr("Não foi possível abrir este arquivo."));
}
void MainWindow::activate(FilePanel *panel) {
    m_active = panel; m_left->setActive(panel == m_left); m_right->setActive(panel == m_right);
    const auto index = m_treeModel->index(panel->path());
    if (index.isValid()) { m_tree->setCurrentIndex(index); m_tree->scrollTo(index); }
    if (!operationBusy()) statusBar()->showMessage(QDir::toNativeSeparators(panel->path()));
    preview(panel->selectedPath());
    updateOperationActions();
}
bool MainWindow::operationBusy()const{return m_operationJob||(m_queueDialog&&m_queueDialog->busy());}
void MainWindow::updateOperationActions() {
    if (!m_copyAction) return;
    const bool available = !operationBusy() && !m_confirming && !m_viewerDialog;
    if(auto *a=findChild<QAction*>("operationQueue"))a->setEnabled(!m_confirming&&!m_viewerDialog);
    if (auto *a=findChild<QAction *>("preferences")) a->setEnabled(available);
    if (auto *a=findChild<QAction *>("interfaceStudio")) a->setEnabled(available);
    if (auto *a=findChild<QAction *>("duplicateFinder")) a->setEnabled(available);
    if (auto *a=findChild<QAction *>("userScripts")) a->setEnabled(available);
    if (auto *a=findChild<QAction *>("windowsIntegration")) a->setEnabled(available);
    for(const auto &c:m_commands)if(c.id.startsWith("user."))c.action->setEnabled(available);
    if (auto *a=findChild<QAction *>("commandCenter")) a->setEnabled(!m_confirming && !m_viewerDialog);
    const auto selected = m_active->selectedPaths();
    if(auto *a=findChild<QAction*>("recognizePreview"))a->setEnabled(available&&!m_previewWidget->result().image.isNull()&&m_previewWidget->result().path==m_active->selectedPath());
    if(auto *a=findChild<QAction*>("classifyFiles"))a->setEnabled(available&&!selected.isEmpty()&&selected.size()<=256);
    if(auto *a=findChild<QAction*>("triageImages"))a->setEnabled(available);
    m_openAction->setEnabled(selected.size() == 1);
    m_openTabAction->setEnabled(selected.size() == 1 && QFileInfo(selected.first()).isDir());
    m_clipboardCopyAction->setEnabled(available && !selected.isEmpty());
    m_clipboardCutAction->setEnabled(available && !selected.isEmpty());
    m_pasteAction->setEnabled(available && m_clipboard->snapshot().transfer.valid() && QFileInfo(m_active->path()).isDir());
    m_left->setTransfersEnabled(available); m_right->setTransfersEnabled(available);
    m_copyAction->setEnabled(available && !selected.isEmpty());
    m_moveAction->setEnabled(available && !selected.isEmpty());
    m_renameAction->setEnabled(available && selected.size() == 1);
    m_batchRenameAction->setEnabled(available && !selected.isEmpty() && selected.size() <= 1000);
    m_renameHistoryAction->setEnabled(available && !m_renameHistoryRoot.isEmpty());
    m_trashAction->setEnabled(available && !selected.isEmpty());
    m_folderAction->setEnabled(available && QFileInfo(m_active->path()).isDir());
    m_packAction->setEnabled(available && !selected.isEmpty());
    for (auto *a : {m_contentAction, m_backupAction, m_remoteAction}) a->setEnabled(available);
    for (auto *a : {m_metadataAction, m_imageConvertAction, m_mediaConvertAction}) a->setEnabled(available && !selected.isEmpty() && selected.size() <= 200);
    m_archiveAction->setEnabled(available && selected.size() == 1 && QFileInfo(selected.first()).isFile() && supportedArchive(selected.first()));
}
void MainWindow::showArchive(bool create) {
    if (operationBusy() || m_confirming || m_viewerDialog) return;
    const auto sources = m_active->selectedPaths(); if (sources.isEmpty()) return;
    if (!create && (sources.size() != 1 || !supportedArchive(sources.first()))) return;
    const auto destination = (m_active == m_left ? m_right : m_left)->path();
    {
        QScopedValueRollback<bool> guard(m_confirming, true); updateOperationActions();
        ArchiveDialog dialog(sources, destination, create, this);
        connect(&dialog, &ArchiveDialog::revealOutput, this, [this](const QString &path) {
            m_active->newTab(QFileInfo(path).isDir() ? path : QFileInfo(path).absolutePath());
        });
        dialog.exec();
    }
    m_left->refresh(); m_right->refresh(); updateOperationActions();
}
void MainWindow::showTools(int page) {
    if (operationBusy() || m_confirming || m_viewerDialog) return;
    {
        QScopedValueRollback<bool> guard(m_confirming, true); updateOperationActions();
        ToolsDialog dialog(static_cast<ToolPage>(page), m_tools, m_active->path(), m_active->selectedPaths(), this);
        connect(&dialog, &ToolsDialog::openFileRequested, this, [](const QString &p) { QDesktopServices::openUrl(QUrl::fromLocalFile(p)); });
        connect(&dialog, &ToolsDialog::openFolderRequested, this, [this](const QString &p) { m_active->newTab(p); });
        dialog.exec(); m_tools = dialog.settings();
    }
    m_left->refresh(); m_right->refresh(); updateOperationActions();
}
void MainWindow::showDuplicateFinder(){
    if(operationBusy()||m_confirming||m_viewerDialog)return;
    {QScopedValueRollback<bool> guard(m_confirming,true);updateOperationActions();auto locations=m_active->selectedPaths();if(locations.isEmpty())locations.append(m_active->path());DuplicateFinderDialog dialog(m_tools.duplicateFinderProject,locations,this);dialog.exec();m_tools.duplicateFinderProject=dialog.project();}
    updateOperationActions();
}
void MainWindow::showOperationQueue(){
    if(m_confirming||m_viewerDialog)return;
    if(!m_queueDialog){m_queueDialog=new OperationQueueDialog(m_nativeOperationUi,[this]{return !m_operationJob&&!m_confirming&&!m_viewerDialog;},this);connect(m_queueDialog,&OperationQueueDialog::busyChanged,this,[this]{if(m_queueDialog&&m_queueDialog->activeMovesFiles())m_previewWidget->releaseMedia();m_left->refresh();m_right->refresh();updateOperationActions();});connect(m_queueDialog,&QDialog::finished,this,[this]{m_queueDialog.clear();updateOperationActions();if(m_closeAfterQueue){m_closeAfterQueue=false;QTimer::singleShot(0,this,&QWidget::close);}});}
    m_queueDialog->setDraft(m_active->selectedPaths(),(m_active==m_left?m_right:m_left)->path(),m_teraCopyAction->isChecked()?TransferEngine::TeraCopy:TransferEngine::Windows);m_queueDialog->show();m_queueDialog->raise();m_queueDialog->activateWindow();
}
void MainWindow::showUserScript(){
    if(operationBusy()||m_confirming||m_viewerDialog)return;
    {QScopedValueRollback<bool> guard(m_confirming,true);updateOperationActions();UserScriptDialog dialog(m_tools.python,m_tools.scriptManifest,m_active->path(),m_active->selectedPaths(),this);connect(&dialog,&UserScriptDialog::executionStarting,m_previewWidget,&PreviewWidget::releaseMedia);dialog.exec();m_tools.python=dialog.interpreter();m_tools.scriptManifest=dialog.manifest();}
    m_left->refresh();m_right->refresh();updateOperationActions();
}
void MainWindow::showWindowsIntegration(){
    if(operationBusy()||m_confirming||m_viewerDialog)return;
    {QScopedValueRollback<bool> guard(m_confirming,true);updateOperationActions();const auto suffix=QFileInfo(m_active->selectedPath()).suffix();WindowsIntegrationDialog dialog(currentUserFolderRegistry(),QCoreApplication::applicationFilePath(),suffix.isEmpty()?QString{}:"."+suffix,this);dialog.exec();}
    updateOperationActions();
}
void MainWindow::showMediaTools(int page) {
    if (operationBusy() || m_confirming || m_viewerDialog || m_active->selectedPaths().isEmpty()) return;
    m_previewWidget->releaseMedia();
    {
        QScopedValueRollback<bool> guard(m_confirming,true); updateOperationActions();
        MediaToolsDialog dialog(static_cast<MediaTask>(page),m_tools,m_active->selectedPaths(),(m_active==m_left?m_right:m_left)->path(),this);
        connect(&dialog,&MediaToolsDialog::openOutput,this,[this](const QString &path){m_active->newTab(path);});
        dialog.exec();m_tools=dialog.settings();
    }
    m_left->refresh();m_right->refresh();updateOperationActions();
}
void MainWindow::showRenameHistory() {
    if (operationBusy() || m_confirming || m_viewerDialog || m_renameHistoryRoot.isEmpty()) return;
    m_previewWidget->releaseMedia();
    {
        QScopedValueRollback<bool> guard(m_confirming,true); updateOperationActions();
        RenameHistoryDialog dialog(m_renameHistoryRoot,this); dialog.exec();
    }
    m_left->refresh(); m_right->refresh(); updateOperationActions();
}
void MainWindow::startBatchRename() {
    if (operationBusy() || m_confirming || m_viewerDialog) return;
    const auto sources = m_active->selectedPaths();
    if (sources.isEmpty() || sources.size() > 1000) return;
    m_previewWidget->releaseMedia();
    {
        QScopedValueRollback<bool> confirming(m_confirming, true); updateOperationActions();
        BatchRenameDialog dialog(sources, this, m_renameHistoryRoot); dialog.exec();
    }
    m_left->refresh(); m_right->refresh(); updateOperationActions();
}
void MainWindow::startOperation(OperationKind kind) {
    if (operationBusy()) return;
    OperationRequest request; request.kind = kind; request.nativeUi = m_nativeOperationUi;
    request.sources = kind == OperationKind::CreateFolder ? QStringList{} : m_active->selectedPaths();
    request.destination = (kind == OperationKind::Copy || kind == OperationKind::Move) ? (m_active == m_left ? m_right : m_left)->path() : m_active->path();
    if (kind != OperationKind::CreateFolder && request.sources.isEmpty()) return;
    if (kind == OperationKind::Rename) { if (request.sources.size() != 1) return; request.name = QFileInfo(request.sources.first()).fileName(); }
    if (kind == OperationKind::CreateFolder) request.name = tr("Nova pasta");
    runOperation(request);
}
void MainWindow::runOperation(OperationRequest request, std::optional<ClipboardSnapshot> clipboard) {
    if (operationBusy() || m_confirming || m_viewerDialog) return;
    if (request.kind == OperationKind::Move || request.kind == OperationKind::Rename || request.kind == OperationKind::Trash) m_previewWidget->releaseMedia();
    request.nativeUi = m_nativeOperationUi;
    if (request.kind == OperationKind::Copy) request.engine = m_teraCopyAction->isChecked() ? TransferEngine::TeraCopy : TransferEngine::Windows;
    bool accepted = false;
    {
        QScopedValueRollback<bool> confirming(m_confirming, true); updateOperationActions();
        OperationDialog dialog(request, this); accepted = dialog.exec() == QDialog::Accepted;
        if (accepted) {
            request = dialog.request();
            if (request.kind == OperationKind::Copy) m_teraCopyAction->setChecked(request.engine == TransferEngine::TeraCopy);
        }
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
        case OperationState::Delegated: title = tr("Cópia enviada ao TeraCopy"); break;
        }
        const auto summary = result.state == OperationState::Delegated
            ? tr("%1 itens encaminhados. Acompanhe o progresso, cancele e confira a verificação na janela do TeraCopy. O resultado da cópia ainda não foi confirmado pelo DirOpen.").arg(result.items.size())
            : tr("%1 — %2 de %3 itens concluídos.").arg(operationLabel(kind)).arg(result.completedCount()).arg(result.items.size());
        statusBar()->showMessage(title + " — " + summary);
        if (m_closeAfterOperation) { close(); return; }
        auto *message = new QMessageBox((result.state == OperationState::Completed || result.state == OperationState::Delegated) && clipboardError.isEmpty() ? QMessageBox::Information : QMessageBox::Warning,
            title, summary, QMessageBox::Ok, this);
        message->setObjectName("operationResult"); message->setAttribute(Qt::WA_DeleteOnClose);
        QStringList details;
        if (!result.externalRecord.isEmpty()) details.append(tr("Registro do encaminhamento (não comprova conclusão): %1").arg(QDir::toNativeSeparators(result.externalRecord)));
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
            case OperationItemState::Delegated: state = tr("Encaminhado ao TeraCopy / resultado não confirmado"); break;
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
    if(m_queueDialog){event->ignore();m_closeAfterQueue=true;if(!m_queueDialog->requestShutdown())m_closeAfterQueue=false;return;}
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
    state.folderViews = m_folderViews->entries(); state.nativeContextMenu = m_nativeContextMenuAction->isChecked();
    state.teraCopyForCopy = m_teraCopyAction->isChecked(); state.tools = m_tools; state.ui=m_ui;state.layouts=m_layouts;state.tabGroups=m_tabGroups;
    state.independentTrees=m_independentTreesAction->isChecked();
    for(auto &bar:state.ui.advanced.toolbars)if(auto *widget=m_namedToolbars.value(bar.id))bar.visible=!widget->isHidden();
    state.ui.toolbarVisible = !m_commandToolbar->isHidden(); state.ui.statusVisible = !statusBar()->isHidden(); return state;
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
        connect(m_comparisonDialog,&ComparisonDialog::synchronizeRequested,this,[this](const QString &left,const QString &right,const QVector<ComparisonEntry>&entries){
            if(operationBusy()||m_confirming||m_viewerDialog){statusBar()->showMessage(tr("Finalize a transferência ou diálogo atual antes de sincronizar."));return;}
            {QScopedValueRollback<bool> guard(m_confirming,true);updateOperationActions();SynchronizationRequest request;request.left=left;request.right=right;request.entries=entries;request.nativeUi=m_nativeOperationUi;SynchronizationDialog dialog(std::move(request),m_comparisonDialog);dialog.exec();}
            m_left->refresh();m_right->refresh();updateOperationActions();
        });
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
        connect(m_searchDialog, &SearchDialog::openFileRequested, this, [this](const QString &path) { openPath(m_active, path); });
    }
    m_searchDialog->show(); m_searchDialog->raise(); m_searchDialog->activateWindow();
}
void MainWindow::finishClose(QCloseEvent *event) {
    if (m_sessionStore.writable()) {
        const auto error = m_sessionStore.save(session());
        if (!error.isEmpty()) QMessageBox::warning(this, tr("Guardar sessão"), tr("Não foi possível guardar esta sessão.\n\n%1").arg(error));
    }
    if(m_globalHotkeys)m_globalHotkeys->deactivate();
    QMainWindow::closeEvent(event);
}
void MainWindow::showFileContextMenu(const QPoint &position) {
    if (operationBusy() || m_confirming || m_viewerDialog) return;
    updateOperationActions();
    QMenu menu(this); menu.setObjectName("fileContextMenu");
    menu.addActions({m_openAction, m_openTabAction}); menu.addSeparator();
    menu.addActions({m_clipboardCopyAction, m_clipboardCutAction, m_pasteAction});
    auto *between = menu.addMenu(tr("Para o outro painel")); between->addActions({m_copyAction, m_moveAction});
    menu.addSeparator(); menu.addActions({m_renameAction, m_batchRenameAction, m_trashAction}); menu.addSeparator();
    menu.addActions({m_packAction, m_archiveAction}); menu.addSeparator();
    menu.addActions({findChild<QAction *>("calculateFolderSize"), m_folderAction, m_copyPathAction, m_refreshAction});
    const auto selection = m_active->selectedPaths();
    QHash<QString, QAction *> routes{{"rename", m_renameAction}, {"delete", m_trashAction}, {"copy", m_clipboardCopyAction},
                                   {"cut", m_clipboardCutAction}, {"paste", m_pasteAction}, {"newfolder", m_folderAction}, {"refresh", m_refreshAction}};
    // The application's open action accepts one item; Shell can open a batch.
    if (selection.size() == 1) { routes.insert("open", m_openAction); routes.insert("explore", m_openAction); }
    QAction pasteIntoFolder(tr("Colar nesta pasta"), &menu);
    if (selection.size() == 1 && QFileInfo(selection.first()).isDir()) {
        pasteIntoFolder.setEnabled(m_pasteAction->isEnabled());
        connect(&pasteIntoFolder, &QAction::triggered, this, [this, destination = selection.first()] {
            const auto clipboard = m_clipboard->snapshot(); if (!clipboard.transfer.valid()) return;
            OperationRequest request; request.sources = clipboard.transfer.paths; request.kind = clipboard.transfer.kind; request.destination = destination;
            runOperation(request, clipboard);
        });
        routes.insert("paste", &pasteIntoFolder);
    }
    if(m_ui.advanced.contexts.contains("files")){QHash<QString,QAction*>commands;for(const auto &c:m_commands)if(!c.toolbarOnly)commands[c.id]=c.action;arrangeContextMenu(menu,m_ui.advanced.contexts["files"],commands);}
    showContextMenu(position, selection, m_active->path(), menu, routes, m_active);
}
void MainWindow::showTreeContextMenu(const QPoint &position){showTreeContextMenu(m_tree,position);}
void MainWindow::showTreeContextMenu(QTreeView *tree,const QPoint &position) {
    if (operationBusy() || m_confirming || m_viewerDialog) return;
    auto index = position.x() < 0 ? tree->currentIndex() : tree->indexAt(position);
    if (!index.isValid()) return;
    tree->setCurrentIndex(index);
    const auto path = qobject_cast<QFileSystemModel *>(tree->model())->filePath(index);
    QMenu menu(this); menu.setObjectName("treeContextMenu");
    auto *open = menu.addAction(tr("Abrir no painel ativo"), this, [this, path] { m_active->navigate(path); });
    auto *tab=menu.addAction(tr("Abrir em nova aba"), this, [this, path] { m_active->newTab(path); });
    auto *expand=menu.addAction(tree->isExpanded(index) ? tr("Recolher") : tr("Expandir"), this, [tree, index] { tree->setExpanded(index, !tree->isExpanded(index)); });
    menu.addSeparator();
    auto publish = [this, path](OperationKind kind) {
        const auto error = m_clipboard->publish({path}, kind); updateClipboardVisuals();
        if (!error.isEmpty()) statusBar()->showMessage(error);
    };
    auto *copy = menu.addAction(tr("Copiar"), this, [publish] { publish(OperationKind::Copy); });
    auto *cut = menu.addAction(tr("Recortar"), this, [publish] { publish(OperationKind::Move); });
    auto *paste = menu.addAction(tr("Colar nesta pasta"), this, [this, path] {
        const auto clipboard = m_clipboard->snapshot(); if (!clipboard.transfer.valid()) return;
        OperationRequest request; request.kind = clipboard.transfer.kind; request.sources = clipboard.transfer.paths; request.destination = path;
        runOperation(request, clipboard);
    });
    paste->setEnabled(m_clipboard->snapshot().transfer.valid());
    auto *rename = menu.addAction(tr("Renomear…"), this, [this, path] {
        OperationRequest request; request.kind = OperationKind::Rename; request.sources = {path}; request.name = QFileInfo(path).fileName(); runOperation(request);
    });
    auto *trash = menu.addAction(tr("Enviar à Lixeira…"), this, [this, path] {
        OperationRequest request; request.kind = OperationKind::Trash; request.sources = {path}; runOperation(request);
    });
    const bool root = QDir(path).isRoot(); rename->setEnabled(!root); trash->setEnabled(!root); cut->setEnabled(!root);
    auto *create = menu.addAction(tr("Nova pasta…"), this, [this, path] {
        OperationRequest request; request.kind = OperationKind::CreateFolder; request.destination = path; request.name = tr("Nova pasta"); runOperation(request);
    });
    auto *copyPath=menu.addAction(tr("Copiar caminho"), this, [path] { qApp->clipboard()->setText(QDir::toNativeSeparators(path)); });
    if(m_ui.advanced.contexts.contains("tree"))arrangeContextMenu(menu,m_ui.advanced.contexts["tree"],{{"treeOpen",open},{"treeTab",tab},{"treeExpand",expand},{"treeCopy",copy},{"treeCut",cut},{"treePaste",paste},{"treeRename",rename},{"treeTrash",trash},{"treeCreate",create},{"treePath",copyPath}});
    const auto popupPosition = position.x() < 0 ? tree->visualRect(index).center() : position;
    showContextMenu(tree->viewport()->mapToGlobal(popupPosition), {path}, path, menu,
                    {{"open", open}, {"explore", open}, {"copy", copy}, {"cut", cut}, {"paste", paste},
                     {"rename", rename}, {"delete", trash}, {"newfolder", create}, {"refresh", m_refreshAction}});
}
void MainWindow::showContextMenu(const QPoint &position, const QStringList &selection, const QString &folder,
                                 QMenu &menu, const QHash<QString, QAction *> &routes, FilePanel *selectionPanel) {
    if (!m_nativeContextMenuAction->isChecked()) { menu.exec(position); return; }
    QString error;
    ShellMenuResult result;
    {
        QScopedValueRollback<bool> confirming(m_confirming, true);
        auto shell = ShellContextMenu::create(this, selection, folder, QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier), error);
        if (shell && shell->appendApplicationMenu(menu, error)) {
            const auto command = shell->track(position, error);
            if (error.isEmpty()) result = shell->dispatch(command, routes, [this] { m_previewWidget->releaseMedia(); });
        }
    } // Release COM, menu and native message hook before application dialogs.
    updateOperationActions();
    if (!error.isEmpty()) {
        statusBar()->showMessage(tr("Menu do Windows indisponível. %1").arg(error));
        menu.exec(position); return;
    }
    if (!result.error.isEmpty()) statusBar()->showMessage(result.error);
    if (result.invoked) {
        m_left->refresh(); m_right->refresh(); updateClipboardVisuals();
        statusBar()->showMessage(tr("Comando enviado ao Windows. O programa escolhido controla sua execução."));
    }
    if (result.action && result.action->isEnabled()) {
        if (selectionPanel && (m_active != selectionPanel || selectionPanel->path() != folder || selectionPanel->selectedPaths() != selection)) {
            statusBar()->showMessage(tr("A seleção mudou enquanto o menu estava aberto. Abra o menu novamente.")); return;
        }
        result.action->trigger();
    }
}
void MainWindow::prepareClipboard(OperationKind kind) {
    if (operationBusy() || m_confirming || m_viewerDialog) return;
    const auto paths = m_active->selectedPaths(); if (paths.isEmpty()) return;
    const auto error = m_clipboard->publish(paths, kind); updateClipboardVisuals();
    if (!error.isEmpty()) { statusBar()->showMessage(error); return; }
    const auto selection = paths.size() == 1 ? tr("1 item preparado") : tr("%1 itens preparados").arg(paths.size());
    statusBar()->showMessage(selection + (kind == OperationKind::Move ? tr(" para mover. Abra o destino e use Colar.")
        : tr(" para copiar. Abra o destino e use Colar.")));
}
void MainWindow::pasteFiles() {
    if (operationBusy() || m_confirming || m_viewerDialog) return;
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
void MainWindow::initializeCommands() {
    QSet<QAction *> visited;
    auto include = [this, &visited](QAction *a, const QString &category, bool toolbarOnly=false) {
        if (a->objectName().isEmpty() || a->isSeparator() || visited.contains(a)) return;
        visited.insert(a); m_commands.append({a->objectName(),category,a,a->shortcut().toString(QKeySequence::PortableText),toolbarOnly});
        if (!toolbarOnly) addAction(a);
    };
    std::function<void(QMenu *,QString)> walk = [&](QMenu *menu, QString category) {
        for(auto *a:menu->actions()) { if(a->menu())walk(a->menu(),category); else include(a,category); }
    };
    for(auto *a:menuBar()->actions()) if(a->menu()) { auto name=a->text();name.remove('&');walk(a->menu(),name); }
    for(auto *a:findChildren<QAction *>(QString{},Qt::FindDirectChildrenOnly))include(a,tr("Navegação"));
    for(auto *a:m_commandToolbar->actions())if(a->objectName()=="favoritesPopup"){a->setText(tr("Favoritos"));include(a,tr("Pastas"),true);}
    std::sort(m_commands.begin(),m_commands.end(),[](const UiCommand &a,const UiCommand &b){return QString::localeAwareCompare(a.category+commandLabel(a),b.category+commandLabel(b))<0;});
}
void MainWindow::applyCustomization() {
    synchronizeLaunchers();
    if(m_globalHotkeys&&!m_globalHotkeys->bindings().isEmpty()&&m_globalHotkeys->bindings()!=m_ui.advanced.globalKeys){const auto error=m_globalHotkeys->deactivate();if(!error.isEmpty())statusBar()->showMessage(error);}
    for (const auto &c:m_commands) if(!c.toolbarOnly)c.action->setShortcut(QKeySequence::fromString(m_ui.shortcuts.value(c.id,c.defaultKey),QKeySequence::PortableText));
    const auto old=m_commandToolbar->actions(); m_commandToolbar->clear();
    for(auto *a:old)if(a->isSeparator())delete a;
    for(const auto &id:m_ui.toolbar.value_or(m_defaultToolbar)) {
        if(id=="-"){m_commandToolbar->addSeparator();continue;}
        for(const auto &c:m_commands)if(c.id==id)m_commandToolbar->addAction(c.action);
    }
    m_commandToolbar->setVisible(m_ui.toolbarVisible);m_commandToolbar->setMovable(m_ui.toolbarMovable);m_commandToolbar->setFloatable(m_ui.toolbarMovable);
    m_commandToolbar->setIconSize(QSize(m_ui.iconSize,m_ui.iconSize));
    const Qt::ToolButtonStyle styles[]{Qt::ToolButtonTextBesideIcon,Qt::ToolButtonIconOnly,Qt::ToolButtonTextUnderIcon};m_commandToolbar->setToolButtonStyle(styles[m_ui.buttonStyle]);
    // QWidgetAction's button has its own presentation property.
    if(auto *favorites=m_favoritesMenu->parentWidget();qobject_cast<QToolButton *>(favorites))static_cast<QToolButton *>(favorites)->setToolButtonStyle(styles[m_ui.buttonStyle]);
    m_customMenu->clear();
    for(const auto &id:m_ui.menu.value_or(QVector<QString>{})) {
        if(id=="-"){m_customMenu->addSeparator();continue;}
        for(const auto &c:m_commands)if(c.id==id&&!c.toolbarOnly)m_customMenu->addAction(c.action);
    }
    m_customMenu->menuAction()->setVisible(!m_customMenu->actions().isEmpty());
    m_previewWidget->setSettings(m_ui.viewer);applyAdvancedInterface();
    statusBar()->setVisible(m_ui.statusVisible);applyTheme(m_darkAction->isChecked());
}
void MainWindow::showPreferences() {
    if(operationBusy()||m_confirming||m_viewerDialog)return;
    {
        QScopedValueRollback<bool> guard(m_confirming,true);updateOperationActions();
        InterfaceOptions options{m_darkAction->isChecked(),m_dualAction->isChecked(),m_treeAction->isChecked(),!m_preview->isHidden(),dockWidgetArea(m_preview)==Qt::BottomDockWidgetArea,m_nativeContextMenuAction->isChecked(),m_teraCopyAction->isChecked(),m_independentTreesAction->isChecked()};
        PreferencesDialog dialog(session().ui,options,m_commands,m_defaultToolbar,this);
        connect(&dialog,&PreferencesDialog::applyRequested,this,[this,&dialog]{
            m_ui=dialog.customization();const auto o=dialog.options();
            { const QSignalBlocker block(m_darkAction); m_darkAction->setChecked(o.dark); }
            m_dualAction->setChecked(o.dual);m_treeAction->setChecked(o.tree);
            m_preview->setFloating(false);addDockWidget(o.previewBottom?Qt::BottomDockWidgetArea:Qt::RightDockWidgetArea,m_preview);m_preview->setVisible(o.preview);
            m_nativeContextMenuAction->setChecked(o.nativeMenu);m_teraCopyAction->setChecked(o.teraCopy);m_independentTreesAction->setChecked(o.independentTrees);applyTrees();applyCustomization();
        });dialog.exec();
    }updateOperationActions();
}
void MainWindow::showCommands() {
    if(m_confirming||m_viewerDialog)return;
    QAction *selected=nullptr;
    { QScopedValueRollback<bool> guard(m_confirming,true);CommandsDialog dialog(m_commands,this);if(dialog.exec()==QDialog::Accepted)selected=dialog.selectedCommand(); }
    updateOperationActions();
    if(selected&&selected->isEnabled())selected->trigger();
}
void MainWindow::applyTheme(bool dark) {
    const auto palette=uiPalette(m_ui,dark,qApp->style()->standardPalette());
    const QFont font(m_ui.fontFamily,m_ui.fontSize);const auto sheet=uiStyleSheet(m_ui,dark)+componentStyleSheet(m_ui.advanced);
    if(this->palette()!=palette)setPalette(palette);
    auto viewerPalette=palette;const auto viewer=m_ui.advanced.components.value("viewer");
    if(!viewer.background.isEmpty())for(const auto role:{QPalette::Base,QPalette::Window})viewerPalette.setColor(role,QColor(viewer.background));
    if(!viewer.foreground.isEmpty())for(const auto role:{QPalette::Text,QPalette::WindowText})viewerPalette.setColor(role,QColor(viewer.foreground));
    m_previewWidget->setPalette(viewerPalette);
    if(this->font()!=font)setFont(font);
    if(styleSheet()!=sheet)setStyleSheet(sheet);
    m_left->setActive(m_active==m_left);m_right->setActive(m_active==m_right);
}

void MainWindow::applyTrees(){
    const bool visible=m_treeAction->isChecked(),independent=m_independentTreesAction->isChecked();
    m_sidebar->setVisible(visible);m_tree->setVisible(!independent);
    if(auto *layout=qobject_cast<QVBoxLayout *>(m_sidebar->layout()))layout->setStretch(layout->count()-1,independent?1:0);
    if(auto *title=m_sidebar->findChild<QLabel *>("treeSectionTitle"))title->setVisible(!independent);
    for(auto *panel:{m_left,m_right}){panel->setTreeVisible(visible&&independent);if(panel->folderTree())panel->folderTree()->setAnimated(m_ui.advanced.animations);}
}

SavedWorkspace MainWindow::workspace()const{
    const auto state=session();SavedWorkspace value;value.id=QUuid::createUuid().toString(QUuid::WithoutBraces);value.name=tr("Atual");
    value.left=state.left;value.right=state.right;value.ui=state.ui;value.geometry=state.geometry;value.windowState=state.windowState;value.splitter=state.splitterState;
    value.dark=state.dark;value.dual=state.dualPane;value.tree=state.treeVisible;value.independentTrees=state.independentTrees;value.preview=state.previewVisible;value.activePanel=state.activePanel;return value;
}
bool MainWindow::applyWorkspace(const SavedWorkspace &value,bool appearance){
    if(operationBusy()||m_confirming||m_viewerDialog)return false;
    auto error=validateWorkspaces({value},{});if(error.isEmpty()&&appearance)error=validateUiShortcuts(value.ui,m_commands);
    if(!error.isEmpty()){statusBar()->showMessage(error);return false;}
    m_previewWidget->releaseMedia();const int missing=m_left->restoreSession(value.left)+m_right->restoreSession(value.right);
    if(appearance){m_ui=value.ui;const QSignalBlocker block(m_darkAction);m_darkAction->setChecked(value.dark);applyCustomization();}
    m_dualAction->setChecked(value.dual);m_treeAction->setChecked(value.tree);m_independentTreesAction->setChecked(value.independentTrees);applyTrees();
    if(!value.windowState.isEmpty())restoreState(value.windowState,1);if(!value.geometry.isEmpty())restoreGeometry(value.geometry);
    if(!value.splitter.isEmpty())m_splitter->restoreState(value.splitter);m_preview->setVisible(value.preview);
    activate(value.activePanel==1&&value.dual?m_right:m_left);updateOperationActions();
    if(missing)statusBar()->showMessage(tr("Layout restaurado; %1 pasta(s) indisponível(is) preservada(s) nas abas.").arg(missing));return true;
}
void MainWindow::showWorkspaces(){
    if(operationBusy()||m_confirming||m_viewerDialog)return;
    std::optional<SavedWorkspace> chosen;std::optional<SavedTabGroup> group;bool another=false,appearance=true,styleOnly=false;
    {QScopedValueRollback<bool>guard(m_confirming,true);updateOperationActions();WorkspaceDialog dialog(m_layouts,m_tabGroups,workspace(),m_active->session(),this);
        if(dialog.exec()==QDialog::Accepted){m_layouts=dialog.layouts();m_tabGroups=dialog.groups();chosen=dialog.chosenLayout();group=dialog.chosenGroup();another=dialog.newWindow();appearance=dialog.appearance();styleOnly=dialog.appearanceOnly();}}
    updateOperationActions();
    if(group){m_previewWidget->releaseMedia();const int missing=m_active->restoreSession(group->panel);activate(m_active);if(missing)statusBar()->showMessage(tr("Grupo restaurado com %1 pasta(s) indisponível(is), preservada(s) nas abas.").arg(missing));}
    if(chosen){
        if(styleOnly){const auto error=validateUiShortcuts(chosen->ui,m_commands);if(!error.isEmpty()){QMessageBox::information(this,tr("Aparência"),error);return;}m_ui=chosen->ui;{const QSignalBlocker block(m_darkAction);m_darkAction->setChecked(chosen->dark);}applyCustomization();if(!chosen->windowState.isEmpty())restoreState(chosen->windowState,1);updateOperationActions();return;}
        if(another){const auto record=m_sessionPath.isEmpty()?QString{}:m_sessionPath+".windows/"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".json";
            auto *window=new MainWindow({}, {},m_nativeOperationUi,record);window->setAttribute(Qt::WA_DeleteOnClose);window->m_layouts=m_layouts;window->m_tabGroups=m_tabGroups;window->m_tools=m_tools;window->m_favorites=m_favorites;window->refreshFavorites();
            window->m_nativeContextMenuAction->setChecked(m_nativeContextMenuAction->isChecked());window->m_teraCopyAction->setChecked(m_teraCopyAction->isChecked());
            if(!appearance){window->m_ui=session().ui;window->m_darkAction->setChecked(m_darkAction->isChecked());window->applyCustomization();}
            if(!window->applyWorkspace(*chosen,appearance)){delete window;return;}window->show();
        }else applyWorkspace(*chosen,appearance);
    }
}

void MainWindow::showViewerSettings(){
    if(operationBusy()||m_confirming)return;
    {QScopedValueRollback<bool>guard(m_confirming,true);updateOperationActions();const auto value=editViewerSettings(m_ui.viewer,m_viewerDialog?static_cast<QWidget*>(m_viewerDialog.data()):this);if(value){m_ui.viewer=*value;m_previewWidget->setSettings(*value);}}updateOperationActions();
}
void MainWindow::showAnnotations(bool images){
    if(operationBusy()||m_confirming||m_viewerDialog)return;
    auto files=m_active->selectedPaths();if(images){if(files.isEmpty())files=m_active->listedPaths(256);files.erase(std::remove_if(files.begin(),files.end(),[](const QString&path){return !QFileInfo(path).isFile()||!QStringList{"jpg","jpeg","png","bmp","gif","webp","tif","tiff","avif","heic"}.contains(QFileInfo(path).suffix().toLower());}),files.end());}
    if(files.isEmpty()||files.size()>256){QMessageBox::information(this,tr("Classificação"),tr("Selecione de 1 a 256 itens apropriados. A triagem considera até os primeiros 256 itens visíveis da pasta quando não há seleção."));return;}
    {QScopedValueRollback<bool>guard(m_confirming,true);updateOperationActions();AnnotationsDialog dialog(m_sessionPath.isEmpty()?QString{}:m_sessionPath+".annotations.json",files,m_temporaryAnnotations,m_ui.viewer,this);connect(&dialog,&AnnotationsDialog::viewerSettingsChanged,this,[this](const ViewerSettings &value){m_ui.viewer=value;m_previewWidget->setSettings(value);});if(dialog.exec()==QDialog::Accepted&&m_sessionPath.isEmpty())m_temporaryAnnotations=dialog.memoryCatalog();}updateOperationActions();
}

void MainWindow::showOcr(){
    if(operationBusy()||m_confirming)return;const auto result=m_previewWidget->result();if(result.image.isNull()||result.path!=m_active->selectedPath())return;
    {QScopedValueRollback<bool>guard(m_confirming,true);updateOperationActions();OcrDialog dialog(result.image,result.path+(result.kind==PreviewKind::Pdf?tr(" — página %1").arg(result.page+1):QString{}),m_viewerDialog?static_cast<QWidget*>(m_viewerDialog.data()):this);dialog.exec();}updateOperationActions();
}

void MainWindow::showFolderSize() {
    if (m_folderSizeDialog) { m_folderSizeDialog->show(); m_folderSizeDialog->raise(); m_folderSizeDialog->activateWindow(); return; }
    const auto paths = m_active->selectedPaths();
    if (paths.isEmpty()) { statusBar()->showMessage(tr("Selecione as pastas cujo tamanho deseja calcular.")); return; }
    // The worker validates metadata without following links. Do not use QFileInfo
    // to filter away unavailable selections or dereference links in the UI.
    m_folderSizeDialog = new FolderSizeDialog({paths}, this);
    m_folderSizeDialog->show();
}
