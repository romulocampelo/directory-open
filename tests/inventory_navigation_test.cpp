#include "mainwindow.h"
#include "filepanel.h"
#include "folderpresentation.h"
#include "filelistview.h"
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QItemSelectionModel>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QSignalSpy>
#include <QTreeView>
#include <QFileSystemModel>
#include <QSplitter>
#include <QPushButton>
#include <QListWidget>
#include <QLineEdit>
#include <QTimer>
#include <QUuid>
#include <QDir>
#include <QScopeGuard>
#include <QDialogButtonBox>
#include <QLabel>
class InventoryNavigationTest final:public QObject{
    Q_OBJECT
    void file(const QString&p,int n){QFile f(p);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write(QByteArray(n,'x')),qint64(n));}
private slots:
    void rulesAreStrictAndComposed(){
        QTemporaryDir root;file(root.filePath("foto.JPG"),123);file(root.filePath("texto.txt"),300);QDir(root.path()).mkdir("pasta");
        FolderRules r;r.patterns="*.jpg;*.png";r.minimumBytes=100;r.maximumBytes=200;r.modifiedFrom=QDate::currentDate();r.modifiedTo=r.modifiedFrom;
        QVERIFY(matchesFolderRules(QFileInfo(root.filePath("foto.JPG")),r));QVERIFY(!matchesFolderRules(QFileInfo(root.filePath("texto.txt")),r));QVERIFY(matchesFolderRules(QFileInfo(root.filePath("pasta")),r));r.folders=false;QVERIFY(!matchesFolderRules(QFileInfo(root.filePath("pasta")),r));
        QCOMPARE(*decodeFolderRules(encodeFolderRules(r)),r);auto o=encodeFolderRules(r);o["minimumBytes"]="9223372036854775808";QVERIFY(!decodeFolderRules(o));o=encodeFolderRules(r);o["modifiedTo"]="2026-02-30";QVERIFY(!decodeFolderRules(o));r.maximumBytes=20;QVERIFY(!validateFolderRules(r).isEmpty());
    }
    void groupedRowsKeepRealSelectionAndThumbnailsResetGrouping(){
        QTemporaryDir root;file(root.filePath("a.jpg"),2);file(root.filePath("b.txt"),3);file(root.filePath("c.jpg"),4);
        FilePanel panel;panel.show();QVERIFY(panel.navigate(root.path()));QTRY_COMPARE(panel.visibleCount(),3);panel.setGrouping(1);
        auto *view=panel.findChild<QTreeView *>("files");auto *model=view->model();int headers=0;
        for(int row=0;row<3;++row){const auto index=model->index(row,0,view->rootIndex());if(!index.data(GroupHeaderRole).toString().isEmpty())++headers;}
        QCOMPARE(headers,2);const auto index=model->index(0,0,view->rootIndex());view->selectionModel()->setCurrentIndex(index,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
        const auto selected=panel.selectedPaths();QCOMPARE(selected.size(),1);QVERIFY(QFileInfo(selected.first()).isFile());
        const auto rectangle=view->visualRect(index);QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(rectangle.left()+8,rectangle.top()+2));QCOMPARE(panel.selectedPaths(),selected);
        const QPoint header(rectangle.left()+8,rectangle.top()+2);
        auto *transfer=static_cast<FileListView*>(view);QCOMPARE(transfer->destinationAt(header),panel.path());
        QSignalSpy context(&panel,&FilePanel::contextMenuRequested);
        QMetaObject::invokeMethod(view,"customContextMenuRequested",Q_ARG(QPoint,header));QCOMPARE(context.size(),1);QVERIFY(panel.selectedPaths().isEmpty());
        panel.findChild<QAction *>("viewThumbnails")->trigger();QCOMPARE(panel.session().view->grouping,0);
        FolderRules rules;rules.folders=false;rules.patterns="*.txt";QVERIFY(panel.applyRules(rules).isEmpty());QTRY_COMPARE(panel.visibleCount(),1);
    }
    void lockedTabsDuplicateMoveAndPersist(){
        QTemporaryDir root;QDir(root.path()).mkdir("outro");FilePanel panel;QVERIFY(panel.navigate(root.path()));panel.setFilter("foto");FolderRules r;r.patterns="*.jpg";QVERIFY(panel.applyRules(r).isEmpty());panel.setTabLocked(true);
        panel.closeTab(0);QCOMPARE(panel.tabCount(),1);QVERIFY(panel.navigate(root.filePath("outro")));QCOMPARE(panel.tabCount(),2);QVERIFY(!panel.tabLocked());
        auto *tabs=panel.findChild<QTabBar *>("folderTabs");tabs->setCurrentIndex(0);panel.duplicateTab();QCOMPARE(panel.tabCount(),3);QCOMPARE(panel.rules(),r);QVERIFY(!panel.tabLocked());tabs->moveTab(2,0);QCOMPARE(panel.path(),QFileInfo(root.path()).canonicalFilePath());
        const auto state=panel.session();FilePanel restored;restored.restoreSession(state);QCOMPARE(restored.session().tabs[1].locked,true);QCOMPARE(restored.rules(),r);
    }
    void independentTreesNavigateOwnPanelAndSurviveRestart(){
        QTemporaryDir root;QDir(root.path()).mkdir("left");QDir(root.path()).mkdir("right");const auto session=root.filePath("session.json");
        {MainWindow w(root.filePath("left"),root.filePath("right"),false,session);w.show();w.findChild<QAction *>("independentTrees")->setChecked(true);auto *lt=w.leftPanel()->folderTree(),*rt=w.rightPanel()->folderTree();QVERIFY(lt);QVERIFY(rt);QVERIFY(lt!=rt);QVERIFY(lt->model()!=rt->model());
            auto *model=qobject_cast<QFileSystemModel *>(rt->model());const auto target=model->index(root.path());QTRY_VERIFY(target.isValid());QMetaObject::invokeMethod(rt,"clicked",Q_ARG(QModelIndex,target));QCOMPARE(w.rightPanel()->path(),QFileInfo(root.path()).canonicalFilePath());QCOMPARE(w.leftPanel()->path(),QFileInfo(root.filePath("left")).canonicalFilePath());QVERIFY(w.close());}
        {MainWindow w({}, {},false,session);w.show();QVERIFY(w.session().independentTrees);QVERIFY(w.leftPanel()->folderTree()->isVisible());QVERIFY(w.rightPanel()->folderTree()->isVisible());QVERIFY(w.close());}
    }
    void workspacesAndGroupsSharePanelCodecAndRestore(){
        QTemporaryDir root;QDir(root.path()).mkdir("outro");MainWindow window(root.path(),root.path(),false);window.show();window.leftPanel()->setTabLocked(true);window.leftPanel()->duplicateTab();window.leftPanel()->setGrouping(1);
        auto snapshot=window.workspace();snapshot.name="Revisão";snapshot.dark=true;snapshot.ui.fontSize=13;
        auto decoded=decodeWorkspaces(encodeWorkspaces({snapshot}));QVERIFY(decoded);QCOMPARE(decoded->first().left.tabs.first().locked,true);QCOMPARE(decoded->first().left.view->grouping,1);
        window.leftPanel()->navigate(root.filePath("outro"));QVERIFY(window.applyWorkspace(decoded->first()));QCOMPARE(window.leftPanel()->path(),QFileInfo(root.path()).canonicalFilePath());QCOMPARE(window.font().pointSize(),13);
        MainWindow second(root.path(),root.path(),false);second.show();QCOMPARE(second.font().pointSize(),9);QCOMPARE(window.font().pointSize(),13);QVERIFY(window.session().dark);QVERIFY(!second.session().dark);
        auto invalid=encodeWorkspaces({snapshot}).first().toObject();invalid["left"]=QJsonObject{};QVERIFY(!decodeWorkspaces(QJsonArray{invalid}));
        auto other=snapshot;other.id=QUuid::createUuid().toString(QUuid::WithoutBraces);other.name=" revisão ";QVERIFY(!validateWorkspaces({snapshot,other},{}).isEmpty());
        SavedTabGroup group{QUuid::createUuid().toString(QUuid::WithoutBraces),"Fotos",snapshot.left};QVERIFY(decodeTabGroups(encodeTabGroups({group})));
    }
    void workspaceManagerPersistsCatalogAndOpensOwnedSecondWindow(){
        QTemporaryDir root;const auto path=root.filePath("session.json");MainWindow window(root.path(),root.path(),false,path);window.show();
        QTimer::singleShot(0,&window,[&]{auto*d=window.findChild<WorkspaceDialog *>("workspaceDialog");QVERIFY(d);auto cleanup=qScopeGuard([d]{if(d->isVisible())d->reject();});QVERIFY(d->findChild<QLabel *>()->palette().color(QPalette::WindowText).lightness()<160);d->findChild<QLineEdit *>("workspaceName")->setText("Trabalho");d->findChild<QPushButton *>("saveWorkspace")->click();QCOMPARE(d->layouts().size(),1);d->accept();});window.findChild<QAction *>("manageWorkspaces")->trigger();QCOMPARE(window.session().layouts.size(),1);
        QTimer::singleShot(0,&window,[&]{auto*d=window.findChild<WorkspaceDialog *>("workspaceDialog");QVERIFY(d);d->findChild<QListWidget *>("layoutList")->setCurrentRow(0);d->findChild<QPushButton *>("openWorkspaceWindow")->click();});window.findChild<QAction *>("manageWorkspaces")->trigger();
        MainWindow *other=nullptr;for(auto *w:QApplication::topLevelWidgets())if(auto *m=qobject_cast<MainWindow *>(w);m&&m!=&window)other=m;QVERIFY(other);QCOMPARE(other->leftPanel()->path(),window.leftPanel()->path());QVERIFY(other->close());QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(window.close());
        MainWindow restored({}, {},false,path);QCOMPARE(restored.session().layouts.size(),1);QCOMPARE(restored.session().layouts.first().name,QString("Trabalho"));QVERIFY(restored.close());
    }
    void captureInventoryNavigation(){
        const auto out=qEnvironmentVariable("DIROPEN_INVENTORY_CAPTURE_DIR");if(out.isEmpty())QSKIP("Captura nativa opcional do inventário.");QVERIFY(QDir().mkpath(out));QTemporaryDir root(QDir(out).filePath("demonstracao-XXXXXX"));QVERIFY(root.isValid());
        for(const auto &name:{"Documentos","Imagens","Projetos"})QDir(root.path()).mkdir(name);file(root.filePath("foto-01.jpg"),2048);file(root.filePath("foto-02.jpg"),4096);file(root.filePath("notas.txt"),200);file(root.filePath("relatorio.pdf"),1000);
        MainWindow window(root.path(),root.filePath("Documentos"),false);window.show();window.findChild<QAction *>("independentTrees")->setChecked(true);window.leftPanel()->setGrouping(1);window.leftPanel()->setTabLocked(true);window.leftPanel()->duplicateTab();QTRY_COMPARE(window.leftPanel()->visibleCount(),7);
        for(auto *panel:{window.leftPanel(),window.rightPanel()}){auto *tree=panel->folderTree();auto *model=qobject_cast<QFileSystemModel *>(tree->model());tree->setRootIndex(model->index(root.path()));tree->expandAll();}
        QTest::qWait(250);QVERIFY(window.grab().save(out+"/navigation-groups-trees.png"));
        QTimer::singleShot(0,&window,[&]{auto*d=window.findChild<QDialog *>("folderRulesDialog");QVERIFY(d);auto cleanup=qScopeGuard([d]{if(d->isVisible())d->reject();});d->findChild<QLineEdit *>("rulePatterns")->setText("*.jpg;*.png");QTest::qWait(80);QVERIFY(d->grab().save(out+"/combined-filter.png"));d->reject();});window.findChild<QAction *>("advancedFolderFilter")->trigger();
        auto snapshot=window.workspace();snapshot.name="Revisão de arquivos";WorkspaceDialog dialog({snapshot},{{QUuid::createUuid().toString(QUuid::WithoutBraces),"Fotos e documentos",snapshot.left}},snapshot,snapshot.left,&window);dialog.show();QTest::qWait(100);QVERIFY(dialog.grab().save(out+"/saved-workspaces.png"));dialog.reject();
    }
};
QTEST_MAIN(InventoryNavigationTest)
#include "inventory_navigation_test.moc"
