#include "mainwindow.h"
#include "filepanel.h"
#include "preferencesdialog.h"
#include "sessionstore.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QMenu>
#include <QLockFile>

class InterfaceCustomizationTest final : public QObject {
    Q_OBJECT
    PreferencesDialog *preferences(MainWindow &window) { return window.findChild<PreferencesDialog *>("preferencesDialog"); }
    void button(PreferencesDialog *dialog, QDialogButtonBox::StandardButton b) { dialog->findChild<QDialogButtonBox *>()->button(b)->click(); }
private slots:
    void profileIsVersionedAtomicBoundedAndBackedUp() {
        QTemporaryDir root;const auto path=root.filePath("profile.json");UiCustomization v;v.fontSize=11;
        QVERIFY(saveUiProfile(path,v,true).isEmpty());const auto first=readUiProfile(path);QVERIFY(first.error.isEmpty());QCOMPARE(first.value.fontSize,11);QVERIFY(first.dark);QVERIFY(!QFile::exists(path+".bak"));
        v.fontSize=14;QVERIFY(saveUiProfile(path,v,false).isEmpty());const auto prior=readUiProfile(path+".bak");QVERIFY(prior.error.isEmpty());QCOMPARE(prior.value.fontSize,11);
        QVERIFY(saveUiProfile(path,v,false).isEmpty());QCOMPARE(readUiProfile(path+".bak").value.fontSize,11);
        {QLockFile lock(path+".lock");QVERIFY(lock.tryLock(0));v.fontSize=15;QVERIFY(!saveUiProfile(path,v,false).isEmpty());}QCOMPARE(readUiProfile(path).value.fontSize,14);
        QFile f(path);QVERIFY(f.open(QIODevice::ReadOnly));auto o=QJsonDocument::fromJson(f.readAll()).object();f.close();o["version"]=2;QVERIFY(f.open(QIODevice::WriteOnly|QIODevice::Truncate));f.write(QJsonDocument(o).toJson());f.close();QVERIFY(!readUiProfile(path).error.isEmpty());
        QVERIFY(f.open(QIODevice::WriteOnly|QIODevice::Truncate));f.write(QByteArray(65537,'x'));f.close();QVERIFY(!readUiProfile(path).error.isEmpty());QVERIFY(!saveUiProfile(path,v,false).isEmpty());
    }
    void codecRejectsMalformedAndKeepsOmission() {
        UiCustomization v; QVERIFY(decodeUiCustomization(QJsonObject{})); QVERIFY(!v.toolbar);
        v.toolbar=QVector<QString>{};v.shortcuts["copyFiles"]="Ctrl+Alt+C";v.fontSize=14;v.extra["future"]="preserved";
        const auto decoded=decodeUiCustomization(encodeUiCustomization(v));QVERIFY(decoded);QVERIFY(decoded->toolbar);QVERIFY(decoded->toolbar->isEmpty());QCOMPARE(decoded->fontSize,14);QCOMPARE(decoded->extra["future"].toString(),QString("preserved"));
        for(const auto &o:QList<QJsonObject>{{{"fontSize",9.5}},{{"fontSize",100}},{{"toolbarVisible","yes"}},{{"accent","red; color:white"}},{{"shortcuts",QJsonObject{{"x","NoSuchKey"}}}},{{"toolbar",QJsonArray{"open","open"}}},{{"menu",QJsonArray{1}}}})QVERIFY(!decodeUiCustomization(o));
        v.toolbar=QVector<QString>(129,"-");QVERIFY(!validateUiCustomization(v).isEmpty());
    }
    void shortcutsRejectCollisionsPrefixesAndRecoveryOverrides() {
        QAction a("Busca",this),b("Cópia",this),c("Preferências",this);
        QVector<UiCommand> commands{{"search","Ferramentas",&a,"Ctrl+Shift+F"},{"copy","Editar",&b,"F8"},{"preferences","Configurações",&c,"Ctrl+,"}};
        UiCustomization v;QVERIFY(validateUiShortcuts(v,commands).isEmpty());v.shortcuts["copy"]="Ctrl+Shift+F";QVERIFY(!validateUiShortcuts(v,commands).isEmpty());
        v.shortcuts["copy"]="Ctrl+Shift+F, A";QVERIFY(!validateUiShortcuts(v,commands).isEmpty());v.shortcuts["copy"]="Ctrl+C";QVERIFY(!validateUiShortcuts(v,commands).isEmpty());
        v.shortcuts.clear();v.shortcuts["preferences"]="Ctrl+Alt+P";QVERIFY(!validateUiShortcuts(v,commands).isEmpty());v.shortcuts.clear();v.shortcuts["search"]="";QVERIFY(validateUiShortcuts(v,commands).isEmpty());
    }
    void commandAvailabilityFollowsTheOwner() {
        QAction a("Disponível",this);a.setEnabled(false);CommandsDialog dialog({{"action","Teste",&a,{}}},nullptr);
        auto *item=dialog.findChild<QListWidget *>("commandList")->item(0);QVERIFY(!(item->flags()&Qt::ItemIsEnabled));a.setEnabled(true);QVERIFY(item->flags()&Qt::ItemIsEnabled);a.setEnabled(false);QVERIFY(!(item->flags()&Qt::ItemIsEnabled));
    }
    void cancelAndApplyHaveDifferentEffects() {
        QTemporaryDir root;MainWindow window(root.path(),root.path(),false);window.show();
        QTimer::singleShot(0,&window,[&]{auto *d=preferences(window);QVERIFY(d);d->findChild<QSpinBox *>("uiFontSize")->setValue(16);button(d,QDialogButtonBox::Cancel);});
        window.findChild<QAction *>("preferences")->trigger();QCOMPARE(window.session().ui.fontSize,9);
        QTimer::singleShot(0,&window,[&]{auto *d=preferences(window);QVERIFY(d);d->findChild<QSpinBox *>("uiFontSize")->setValue(12);d->findChild<QComboBox *>("uiTheme")->setCurrentIndex(1);button(d,QDialogButtonBox::Apply);QCOMPARE(window.session().ui.fontSize,12);d->findChild<QSpinBox *>("uiFontSize")->setValue(18);button(d,QDialogButtonBox::Cancel);});
        window.findChild<QAction *>("preferences")->trigger();QCOMPARE(window.session().ui.fontSize,12);QVERIFY(window.session().dark);QCOMPARE(window.font().pointSize(),12);
    }
    void preferencesPersistRealActionsAndLegacySessions() {
        QTemporaryDir root;const auto path=root.filePath("session.json");
        {
            MainWindow window(root.path(),root.path(),false,path);window.show();
            QTimer::singleShot(0,&window,[&]{auto *d=preferences(window);QVERIFY(d);d->findChild<QCheckBox *>("uiDual")->setChecked(false);d->findChild<QCheckBox *>("uiTree")->setChecked(false);d->findChild<QCheckBox *>("uiPreview")->setChecked(true);d->findChild<QCheckBox *>("uiPreviewBottom")->setChecked(true);d->findChild<QCheckBox *>("uiStatus")->setChecked(false);d->findChild<QSpinBox *>("uiIconSize")->setValue(32);d->findChild<QComboBox *>("uiButtonStyle")->setCurrentIndex(1);
                auto *toolbar=d->findChild<QListWidget *>("uiToolbarCommands");toolbar->clear();auto *item=new QListWidgetItem("Busca",toolbar);item->setData(Qt::UserRole,"searchFiles");
                auto *menu=d->findChild<QListWidget *>("uiMenuCommands");auto *copy=new QListWidgetItem("Copiar",menu);copy->setData(Qt::UserRole,"copyFiles");
                auto *keys=d->findChild<QTableWidget *>("uiShortcuts");for(int r=0;r<keys->rowCount();++r)if(keys->item(r,0)->data(Qt::UserRole)=="searchFiles")static_cast<QKeySequenceEdit *>(keys->cellWidget(r,1))->setKeySequence(QKeySequence("Ctrl+Alt+S"));
                button(d,QDialogButtonBox::Ok);
            });window.findChild<QAction *>("preferences")->trigger();
            auto *bar=window.findChild<QToolBar *>("commands");QCOMPARE(bar->actions().size(),1);QCOMPARE(bar->actions().first(),window.findChild<QAction *>("searchFiles"));
            auto *menu=window.findChild<QMenu *>("customCommandsMenu");QCOMPARE(menu->actions().first(),window.findChild<QAction *>("copyFiles"));QVERIFY(!menu->actions().first()->isEnabled());QVERIFY(window.close());
        }
        {MainWindow window({}, {},false,path);window.show();QVERIFY(!window.session().dualPane);QVERIFY(!window.session().treeVisible);QVERIFY(window.session().previewVisible);QVERIFY(window.statusBar()->isHidden());QCOMPARE(window.dockWidgetArea(window.findChild<QDockWidget *>("previewDock")),Qt::BottomDockWidgetArea);QCOMPARE(window.findChild<QAction *>("searchFiles")->shortcut(),QKeySequence("Ctrl+Alt+S"));QCOMPARE(window.findChild<QToolBar *>("commands")->iconSize(),QSize(32,32));QVERIFY(window.close());}
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));auto json=QJsonDocument::fromJson(file.readAll()).object();file.close();json.remove("interface");QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));file.write(QJsonDocument(json).toJson());file.close();
        {MainWindow legacy({}, {},false,path);QCOMPARE(legacy.session().ui.fontSize,9);QCOMPARE(legacy.findChild<QAction *>("searchFiles")->shortcut(),QKeySequence("Ctrl+Shift+F"));QVERIFY(legacy.close());}QVERIFY(QFile::exists(path+".bak"));
    }
    void conflictDoesNotApplyAndCommandsUseExistingRoutes() {
        QTemporaryDir root;MainWindow window(root.path(),root.path(),false);window.show();
        QTimer::singleShot(0,&window,[&]{auto *d=preferences(window);QVERIFY(d);auto *keys=d->findChild<QTableWidget *>("uiShortcuts");for(int r=0;r<keys->rowCount();++r)if(keys->item(r,0)->data(Qt::UserRole)=="searchFiles")static_cast<QKeySequenceEdit *>(keys->cellWidget(r,1))->setKeySequence(QKeySequence("Ctrl+Shift+P"));button(d,QDialogButtonBox::Apply);QVERIFY(!d->findChild<QLabel *>("preferencesError")->text().isEmpty());QCOMPARE(window.findChild<QAction *>("searchFiles")->shortcut(),QKeySequence("Ctrl+Shift+F"));button(d,QDialogButtonBox::Cancel);});window.findChild<QAction *>("preferences")->trigger();
        QTimer::singleShot(0,&window,[&]{auto *d=window.findChild<CommandsDialog *>("commandsDialog");QVERIFY(d);auto *list=d->findChild<QListWidget *>("commandList");auto *search=d->findChild<QLineEdit *>("commandSearch");search->setText("Nova aba");bool found=false;for(int r=0;r<list->count();++r){auto *i=list->item(r);if(i->data(Qt::UserRole)=="newTab"){QVERIFY(!i->isHidden());list->setCurrentItem(i);found=true;}if(i->data(Qt::UserRole)=="trashFiles")QVERIFY(!(i->flags()&Qt::ItemIsEnabled));}QVERIFY(found);d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Open)->click();});
        window.findChild<QAction *>("commandCenter")->trigger();QCOMPARE(window.leftPanel()->tabCount(),2);
    }
    void captureNativeScreens() {
        const auto out=qEnvironmentVariable("DIROPEN_INTERFACE_CAPTURES");if(out.isEmpty())QSKIP("Capturas nativas opcionais.");QVERIFY(QDir().mkpath(out));QTemporaryDir root;
        QDir(root.path()).mkdir("Documentos");QDir(root.path()).mkdir("Imagens");QFile file(root.filePath("Bem-vindo.txt"));QVERIFY(file.open(QIODevice::WriteOnly));file.write("Directory Open - demonstracao");file.close();
        MainWindow window(root.path(),root.path(),false);window.show();QTest::qWait(200);
        QTimer::singleShot(0,&window,[&]{auto *d=preferences(window);QVERIFY(d);auto *pages=d->findChild<QListWidget *>("preferencesPages");for(int i=0;i<pages->count();++i){pages->setCurrentRow(i);QTest::qWait(90);QVERIFY(d->grab().save(out+QString("/preferences-%1.png").arg(i)));}d->findChild<QComboBox *>("uiTheme")->setCurrentIndex(1);button(d,QDialogButtonBox::Apply);pages->setCurrentRow(0);QTest::qWait(120);QVERIFY(d->grab().save(out+"/preferences-dark.png"));button(d,QDialogButtonBox::Ok);});window.findChild<QAction *>("preferences")->trigger();
        QTest::qWait(120);QVERIFY(window.grab().save(out+"/workspace-customization.png"));
        QTimer::singleShot(0,&window,[&]{auto *d=window.findChild<CommandsDialog *>("commandsDialog");QVERIFY(d);QTest::qWait(120);QVERIFY(d->grab().save(out+"/command-center.png"));d->reject();});window.findChild<QAction *>("commandCenter")->trigger();
    }
};
QTEST_MAIN(InterfaceCustomizationTest)
#include "interface_customization_test.moc"
