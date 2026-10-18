#include "mainwindow.h"
#include "filepanel.h"
#include "interfacecommands.h"
#include "globalhotkeys.h"
#include "uilanguage.h"
#include <QKeySequenceEdit>
#include <QSignalSpy>
#include <QTableWidget>
#include <windows.h>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileSystemModel>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include "previewwidget.h"
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QUuid>

class AdvancedInterfaceTest final:public QObject{
    Q_OBJECT
    QString id(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
    void addCommand(QWidget *editor,const QString &id){auto *combo=editor->findChild<QComboBox*>();combo->setCurrentIndex(combo->findData(id));for(auto*b:editor->findChildren<QPushButton*>())if(b->text()=="Adicionar"){b->click();break;}}
private slots:
    void boundaryLimitsAndUnknownFields(){
        AdvancedInterface v;v.toolbars={{id(),"Revisão",{"newTab","-","searchFiles"}}};v.components["files"]={"#fffefe","#112233",12};v.contexts["tree"]={"treePath"};v.extra["future"]=12;
        const auto decoded=decodeAdvancedInterface(encodeAdvancedInterface(v));QVERIFY(decoded);QCOMPARE(decoded->extra["future"].toInt(),12);QCOMPARE(decoded->toolbars.first().commands,v.toolbars.first().commands);
        auto bad=v;bad.toolbars.append(bad.toolbars.first());QVERIFY(!validateAdvancedInterface(bad).isEmpty());bad=v;bad.components["files"].background="red; color:white";QVERIFY(!validateAdvancedInterface(bad).isEmpty());bad=v;bad.contexts["other"]={};QVERIFY(!validateAdvancedInterface(bad).isEmpty());bad=v;bad.extra["tooLarge"]=QString(50000,'x');QVERIFY(!validateAdvancedInterface(bad).isEmpty());
        auto o=encodeAdvancedInterface(v);o["animations"]=1;QVERIFY(!decodeAdvancedInterface(o));UiCustomization ui;ui.advanced=v;QVERIFY(decodeUiCustomization(encodeUiCustomization(ui)));QVERIFY(decodeUiCustomization(QJsonObject{}));
    }
    void editorChangesRealMenusBarsAndPersists(){
        QTemporaryDir root;const auto path=root.filePath("session.json");QString barId;
        {MainWindow w(root.path(),root.path(),false,path);w.show();
            QTimer::singleShot(0,&w,[&]{auto*d=w.findChild<InterfaceStudio*>("interfaceStudio");QVERIFY(d);auto guard=qScopeGuard([d]{if(d->isVisible())d->reject();});d->findChild<QPushButton*>("studioAddToolbar")->click();auto *bar=d->findChild<QComboBox*>("studioToolbars");barId=bar->currentData().toString();auto *name=d->findChild<QLineEdit*>("studioBarName");name->setFocus();name->selectAll();QTest::keyClicks(name,"Revisao");addCommand(d->findChild<QWidget*>("studioBarCommands"),"newTab");d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});w.findChild<QAction*>("interfaceStudio")->trigger();
            auto *bar=w.findChild<QToolBar*>("customToolbar_"+barId);QVERIFY(bar);QCOMPARE(bar->windowTitle(),QString("Revisao"));QCOMPARE(bar->actions().size(),1);bar->actions().first()->trigger();QCOMPARE(w.leftPanel()->tabCount(),2);
            auto state=w.workspace();state.ui.advanced.menus["fileMenu"]={"newTab"};state.ui.advanced.menus["settingsMenu"]={};state.ui.advanced.components["files"]={"#eaf9ed","#183725",11};QVERIFY(w.applyWorkspace(state));QCOMPARE(w.findChild<QMenu*>("fileMenu")->actions().size(),1);QVERIFY(w.findChild<QMenu*>("settingsMenu")->actions().contains(w.findChild<QAction*>("preferences")));QVERIFY(w.styleSheet().contains("#eaf9ed"));bar->hide();QVERIFY(w.close());}
        {MainWindow w({}, {},false,path);w.show();auto *bar=w.findChild<QToolBar*>("customToolbar_"+barId);QVERIFY(bar);QVERIFY(bar->isHidden());QCOMPARE(w.findChild<QMenu*>("fileMenu")->actions().first()->objectName(),QString("newTab"));auto restored=w.workspace();restored.ui=UiCustomization{};QVERIFY(w.applyWorkspace(restored));QVERIFY(!w.findChild<QToolBar*>("customToolbar_"+barId));QVERIFY(w.findChild<QMenu*>("fileMenu")->actions().size()>1);QVERIFY(w.findChild<QMenu*>("viewMenu")->actions().last()->menu());QVERIFY(w.close());}
    }
    void cancellingEditorDoesNotInstallLauncher(){
        QTemporaryDir root;MainWindow w(root.path(),root.path(),false);w.show();
        QTimer::singleShot(0,&w,[&]{auto*d=w.findChild<InterfaceStudio*>("interfaceStudio");QVERIFY(d);auto guard=qScopeGuard([d]{if(d->isVisible())d->reject();});d->findChild<QLineEdit*>("launcherName")->setText("Teste");d->findChild<QLineEdit*>("launcherProgram")->setText(QCoreApplication::applicationFilePath());d->findChild<QPushButton*>("saveLauncher")->click();QCOMPARE(d->value().launchers.size(),1);d->reject();});w.findChild<QAction*>("interfaceStudio")->trigger();QVERIFY(w.session().ui.advanced.launchers.isEmpty());
    }
    void contextCommandsKeepClickedTreeAndTabOwnership(){
        QTemporaryDir root;QDir(root.path()).mkdir("alvo");MainWindow w(root.path(),root.path(),false);w.show();auto state=w.workspace();state.ui.advanced.contexts["tree"]={"treePath"};state.ui.advanced.contexts["tabs"]={"tabDuplicate"};QVERIFY(w.applyWorkspace(state));w.findChild<QAction*>("nativeContextMenu")->setChecked(false);
        auto *tree=w.findChild<QTreeView*>("folderTree");auto *model=qobject_cast<QFileSystemModel*>(tree->model());const auto target=model->index(root.filePath("alvo"));QTRY_VERIFY(target.isValid());tree->setRootIndex(model->index(root.path()));tree->setCurrentIndex(target);tree->scrollTo(target);QTest::qWait(50);
        QTimer::singleShot(0,&w,[&]{auto *menu=w.findChild<QMenu*>("treeContextMenu");QVERIFY(menu);auto guard=qScopeGuard([menu]{menu->close();});QCOMPARE(menu->actions().size(),1);menu->actions().first()->trigger();});QMetaObject::invokeMethod(tree,"customContextMenuRequested",Q_ARG(QPoint,tree->visualRect(target).center()));QCOMPARE(qApp->clipboard()->text(),QDir::toNativeSeparators(root.filePath("alvo")));
        auto *tabs=w.rightPanel()->findChild<QTabBar*>("folderTabs");QTimer::singleShot(0,&w,[&]{auto *menu=w.rightPanel()->findChild<QMenu*>("tabContextMenu");QVERIFY(menu);auto guard=qScopeGuard([menu]{menu->close();});QCOMPARE(menu->actions().size(),1);menu->actions().first()->trigger();});QMetaObject::invokeMethod(tabs,"customContextMenuRequested",Q_ARG(QPoint,tabs->tabRect(0).center()));QCOMPARE(w.rightPanel()->tabCount(),2);QCOMPARE(w.leftPanel()->tabCount(),1);
    }
    void iconsAreBoundedRasterAndFallbackIsExplicit(){
        QTemporaryDir root;QImage image(32,32,QImage::Format_ARGB32);image.fill(Qt::green);const auto path=root.filePath("icon.png");QVERIFY(image.save(path));QString error;QVERIFY(!readCommandIcon(path,&error).isNull());QVERIFY(error.isEmpty());QFile svg(root.filePath("icon.svg"));QVERIFY(svg.open(QIODevice::WriteOnly));svg.write("<svg/>");svg.close();QVERIFY(readCommandIcon(svg.fileName(),&error).isNull());QVERIFY(!error.isEmpty());image=QImage(2049,1,QImage::Format_ARGB32);image.fill(Qt::red);QVERIFY(image.save(path));QVERIFY(readCommandIcon(path,&error).isNull());
    }
    void viewerPaletteAppliesToPaintedCanvasAndResets(){
        QTemporaryDir root;MainWindow w(root.path(),root.path(),false);w.show();auto snapshot=w.workspace();snapshot.ui.advanced.components["viewer"]={"#ddeeff","#112233",12};QVERIFY(w.applyWorkspace(snapshot));auto *preview=w.findChild<PreviewWidget*>("filePreview");QVERIFY(preview);QCOMPARE(preview->palette().color(QPalette::Base),QColor("#ddeeff"));snapshot.ui.advanced.components.clear();QVERIFY(w.applyWorkspace(snapshot));QVERIFY(preview->palette().color(QPalette::Base)!=QColor("#ddeeff"));
    }
    void styleSetAppliesWithoutReplacingFolders(){
        QTemporaryDir root;QDir(root.path()).mkdir("outra");const auto path=root.filePath("session.json");
        {MainWindow w(root.path(),root.path(),false,path);w.show();QTimer::singleShot(0,&w,[&]{auto*d=w.findChild<WorkspaceDialog*>("workspaceDialog");QVERIFY(d);auto guard=qScopeGuard([d]{if(d->isVisible())d->reject();});d->findChild<QLineEdit*>("workspaceName")->setText("Estilo");d->findChild<QPushButton*>("saveWorkspace")->click();d->accept();});w.findChild<QAction*>("manageWorkspaces")->trigger();QVERIFY(w.close());}
        MainWindow w({}, {},false,path);w.show();w.leftPanel()->navigate(root.filePath("outra"));auto workspace=w.workspace();workspace.ui.fontSize=14;QVERIFY(w.applyWorkspace(workspace));
        QTimer::singleShot(0,&w,[&]{auto*d=w.findChild<WorkspaceDialog*>("workspaceDialog");QVERIFY(d);auto guard=qScopeGuard([d]{if(d->isVisible())d->reject();});d->findChild<QListWidget*>("layoutList")->setCurrentRow(0);d->findChild<QPushButton*>("applyWorkspaceStyle")->click();});w.findChild<QAction*>("manageWorkspaces")->trigger();QCOMPARE(w.font().pointSize(),9);QCOMPARE(w.leftPanel()->path(),QFileInfo(root.filePath("outra")).canonicalFilePath());QVERIFY(w.close());
    }
    void languageLoadsRealQtPackAndFallsBack(){
#ifdef DIROPEN_TRANSLATION_FIXTURE
        UiLanguage language;QVERIFY(language.load(QString::fromUtf8(DIROPEN_TRANSLATION_FIXTURE)).isEmpty());QTemporaryDir root;{MainWindow w(root.path(),root.path(),false);QCOMPARE(w.findChild<QAction*>("newTab")->text(),QString("New tab"));}
        QVERIFY(!language.load(root.filePath("missing.qm")).isEmpty());MainWindow restored(root.path(),root.path(),false);QCOMPARE(restored.findChild<QAction*>("newTab")->text(),QString("Nova aba"));
#else
        QSKIP("lrelease não está disponível neste kit.");
#endif
    }
    void globalDefinitionsDoNotRegisterOnImportAndNativeLifecycle(){
        QVERIFY(!parseGlobalHotkey("Ctrl+A").error.isEmpty());QVERIFY(!parseGlobalHotkey("Ctrl+Alt+F12").error.isEmpty());QVERIFY(!parseGlobalHotkey("Meta+Shift+A").error.isEmpty());QVERIFY(parseGlobalHotkey("Ctrl+Alt+Shift+F9").error.isEmpty());
        QTemporaryDir root;MainWindow w(root.path(),root.path(),false);w.show();auto workspace=w.workspace();workspace.ui.advanced.globalKeys["newTab"]="Ctrl+Alt+Shift+F9";QVERIFY(w.applyWorkspace(workspace));auto *runtime=w.findChild<GlobalHotkeys*>("globalHotkeyReceiver");QVERIFY(runtime);QVERIFY(runtime->bindings().isEmpty());
        if(QGuiApplication::platformName()!="windows")QSKIP("Registro global requer o plugin nativo; o teste nativo verifica o ciclo completo.");
        QTimer::singleShot(0,&w,[&]{auto *d=w.findChild<GlobalKeysDialog*>("globalKeysDialog");QVERIFY(d);auto guard=qScopeGuard([d]{if(d->isVisible())d->reject();});d->findChild<QPushButton*>("activateGlobalKeys")->click();});w.findChild<QAction*>("globalKeys")->trigger();QCOMPARE(runtime->bindings().size(),1);
        GlobalHotkeys competitor;QVERIFY(!competitor.activate({{"other","Ctrl+Alt+Shift+F9"}}).isEmpty());QVERIFY(competitor.bindings().isEmpty());QVERIFY(competitor.activate({{"other","Ctrl+Alt+Shift+F8"}}).isEmpty());const auto prior=runtime->bindings();QVERIFY(!runtime->activate({{"blocked","Ctrl+Alt+Shift+F8"}}).isEmpty());QCOMPARE(runtime->bindings(),prior);
        const auto handle=reinterpret_cast<HWND>(runtime->winId());PostMessageW(handle,WM_HOTKEY,1,MAKELPARAM(MOD_CONTROL|MOD_ALT|MOD_SHIFT,VK_F9));QTRY_COMPARE(w.leftPanel()->tabCount(),2);
        PostMessageW(handle,WM_HOTKEY,1,MAKELPARAM(MOD_CONTROL,VK_F8));QTest::qWait(25);QCOMPARE(w.leftPanel()->tabCount(),2);
        QVERIFY(w.close());QVERIFY(runtime->bindings().isEmpty());QVERIFY(competitor.activate({{"other","Ctrl+Alt+Shift+F9"}}).isEmpty());QVERIFY(competitor.deactivate().isEmpty());QCOMPARE(competitor.cleanupPending(),0);
    }
    void launchPreviewCancelsThenDelegatesLiteralArguments(){
        QTemporaryDir root;const auto output=root.filePath("resultado.json");UserLauncher launcher{id(),"Programa de teste",QCoreApplication::applicationFilePath(),{"--launcher-helper",output,"{folder}","texto; $() & literal"}};
        const auto plan=planLauncher(launcher,root.path(),{});QVERIFY(plan.error.isEmpty());auto bad=launcher;bad.arguments={"prefixo{file}"};QVERIFY(!planLauncher(bad,root.path(),{}).error.isEmpty());bad.arguments={"{file}"};QVERIFY(!planLauncher(bad,root.path(),{}).error.isEmpty());
        MainWindow w(root.path(),root.path(),false);w.show();auto workspace=w.workspace();workspace.ui.advanced.launchers={launcher};QVERIFY(w.applyWorkspace(workspace));auto *action=w.findChild<QAction*>("user."+launcher.id);QVERIFY(action);QVERIFY(!QFile::exists(output));
        QTimer::singleShot(0,&w,[&]{auto *d=w.findChild<QMessageBox*>("launcherConfirmation");QVERIFY(d);d->done(QMessageBox::Cancel);});action->trigger();QVERIFY(!QFile::exists(output));
        QTimer::singleShot(0,&w,[&]{auto *d=w.findChild<QMessageBox*>("launcherConfirmation");QVERIFY(d);d->done(QMessageBox::Ok);});action->trigger();QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(output),10000);QFile file(output);QVERIFY(file.open(QIODevice::ReadOnly));const auto object=QJsonDocument::fromJson(file.readAll()).object();QCOMPARE(object["cwd"].toString(),QDir(root.path()).absolutePath());QCOMPARE(object["arguments"].toArray().last().toString(),QString("texto; $() & literal"));
        workspace.ui=UiCustomization{};QVERIFY(w.applyWorkspace(workspace));QVERIFY(!w.findChild<QAction*>("user."+launcher.id));
    }
    void captureStudioPages(){
        const auto out=qEnvironmentVariable("DIROPEN_STUDIO_CAPTURES");if(out.isEmpty())QSKIP("Captura nativa opcional.");QVERIFY(QDir().mkpath(out));QTemporaryDir root(QDir(out).filePath("demonstracao-XXXXXX"));QVERIFY(root.isValid());QDir(root.path()).mkdir("Documentos");QDir(root.path()).mkdir("Imagens");QFile note(root.filePath("Bem-vindo.txt"));QVERIFY(note.open(QIODevice::WriteOnly));note.write("Directory Open - demonstracao");note.close();MainWindow w(root.path(),root.path(),false);w.show();auto *tree=w.findChild<QTreeView*>("folderTree");auto *model=qobject_cast<QFileSystemModel*>(tree->model());tree->setRootIndex(model->index(root.path()));QTRY_COMPARE(w.leftPanel()->visibleCount(),3);
        QTimer::singleShot(0,&w,[&]{auto*d=w.findChild<InterfaceStudio*>("interfaceStudio");QVERIFY(d);auto guard=qScopeGuard([d]{if(d->isVisible())d->reject();});d->findChild<QPushButton*>("studioAddToolbar")->click();addCommand(d->findChild<QWidget*>("studioBarCommands"),"searchFiles");auto *tabs=d->findChild<QTabWidget*>("studioPages");for(int i=0;i<tabs->count();++i){tabs->setCurrentIndex(i);QTest::qWait(100);QVERIFY(d->grab().save(out+QString("/studio-%1.png").arg(i)));}d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});w.findChild<QAction*>("interfaceStudio")->trigger();QTest::qWait(80);QVERIFY(w.grab().save(out+"/studio-workspace.png"));
    }
};
int main(int argc,char **argv){
    QApplication app(argc,argv);app.setStyle("Fusion");const auto args=app.arguments();
    if(args.size()>2&&args[1]=="--launcher-helper"){QJsonArray arguments;for(const auto &s:args.mid(3))arguments.append(s);QFile file(args[2]);if(!file.open(QIODevice::WriteOnly))return 2;file.write(QJsonDocument(QJsonObject{{"cwd",QDir::currentPath()},{"arguments",arguments}}).toJson());return 0;}
    AdvancedInterfaceTest test;return QTest::qExec(&test,argc,argv);
}
#include "advanced_interface_test.moc"
