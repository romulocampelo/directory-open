#include "synchronization.h"
#include "synchronizationdialog.h"
#include "comparisondialog.h"
#include "mainwindow.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
class SynchronizationTest final:public QObject {
    Q_OBJECT
    void put(const QString &p,const QByteArray &data="original"){QVERIFY(QDir().mkpath(QFileInfo(p).absolutePath()));QFile f(p);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write(data),data.size());}
    QByteArray read(const QString &p){QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray{};}
    SynchronizationRequest request(const QString &root){ComparisonRequest compare;compare.left=root+"/left";compare.right=root+"/right";std::atomic_bool cancel=false;const auto report=compareFolders(compare,cancel);SynchronizationRequest value;value.left=compare.left;value.right=compare.right;value.entries=report.entries;value.nativeUi=false;return value;}
private slots:
    void missingFolderCoversChildrenAndExtraDestinationFilesArePreserved(){
        QTemporaryDir root;put(root.filePath("left/a.txt"));put(root.filePath("left/folder/deep/b.txt"),"nested");put(root.filePath("right/extra.txt"),"preserved");std::atomic_bool cancel=false;const auto plan=planSynchronization(request(root.path()),cancel);QVERIFY2(plan.valid(),qPrintable(plan.error));QCOMPARE(plan.changes(),2);QVERIFY(!QFileInfo::exists(root.filePath("right/a.txt")));const auto result=applySynchronization(plan,cancel);QVERIFY2(result.state==OperationState::Completed,qPrintable(result.error));QCOMPARE(read(root.filePath("right/a.txt")),QByteArray("original"));QCOMPARE(read(root.filePath("right/folder/deep/b.txt")),QByteArray("nested"));QCOMPARE(read(root.filePath("right/extra.txt")),QByteArray("preserved"));QCOMPARE(read(root.filePath("left/a.txt")),QByteArray("original"));
    }
    void rightToLeftAndExistingConflictsRequireExplicitPolicy(){
        QTemporaryDir root;put(root.filePath("left/a.txt"),"left version");put(root.filePath("right/a.txt"),"right");put(root.filePath("right/new.txt"),"reverse");std::atomic_bool cancel=false;auto r=request(root.path());r.toRight=false;const auto missing=planSynchronization(r,cancel);QVERIFY(missing.valid());QCOMPARE(missing.changes(),1);QCOMPARE(applySynchronization(missing,cancel).state,OperationState::Completed);QCOMPARE(read(root.filePath("left/new.txt")),QByteArray("reverse"));QCOMPARE(read(root.filePath("left/a.txt")),QByteArray("left version"));
        r=request(root.path());r.includeConflicts=true;const auto conflicts=planSynchronization(r,cancel);QVERIFY(conflicts.valid());QCOMPARE(conflicts.changes(),1);QCOMPARE(applySynchronization(conflicts,cancel).state,OperationState::Completed);QCOMPARE(read(root.filePath("right/a.txt")),QByteArray("right"));bool copied=false;for(const auto &name:QDir(root.filePath("right")).entryList(QDir::Files))if(read(root.filePath("right/"+name))==QByteArray("left version"))copied=true;QVERIFY(copied);
    }
    void changedSourcesUnexpectedTargetsAndRootReplacementRefuseMutation(){
        QTemporaryDir root;put(root.filePath("left/a.txt"));QDir().mkpath(root.filePath("right"));std::atomic_bool cancel=false;auto plan=planSynchronization(request(root.path()),cancel);QVERIFY(plan.valid());put(root.filePath("left/a.txt"),"changed");QCOMPARE(applySynchronization(plan,cancel).state,OperationState::Failed);QVERIFY(!QFileInfo::exists(root.filePath("right/a.txt")));
        plan=planSynchronization(request(root.path()),cancel);QVERIFY(plan.valid());put(root.filePath("right/a.txt"),"new destination");QCOMPARE(applySynchronization(plan,cancel).state,OperationState::Failed);QCOMPARE(read(root.filePath("right/a.txt")),QByteArray("new destination"));QVERIFY(QFile::remove(root.filePath("right/a.txt")));plan=planSynchronization(request(root.path()),cancel);QVERIFY(plan.valid());QVERIFY(QDir().rename(root.filePath("right"),root.filePath("old-right")));QVERIFY(QDir().mkdir(root.filePath("right")));QCOMPARE(applySynchronization(plan,cancel).state,OperationState::Failed);QVERIFY(QDir(root.filePath("right")).isEmpty());
    }
    void cancellationPreservesCompletedCopiesAndRefusesUnsafeRelativeValues(){
        QTemporaryDir root;put(root.filePath("left/a.txt"));put(root.filePath("left/b.txt"));QDir().mkpath(root.filePath("right"));std::atomic_bool cancel=false;auto r=request(root.path());const auto plan=planSynchronization(r,cancel);QVERIFY(plan.valid());const auto partial=applySynchronization(plan,cancel,[&](quint32,quint32 done,const QString &path){if(done==1&&!path.isEmpty())cancel=true;});QCOMPARE(partial.state,OperationState::Cancelled);QCOMPARE(partial.completedCount(),1);QCOMPARE(read(root.filePath("right/a.txt")),QByteArray("original"));QVERIFY(!QFileInfo::exists(root.filePath("right/b.txt")));cancel=false;r.entries.first().relativePath="../outside.txt";QVERIFY(!planSynchronization(r,cancel).valid());
    }
    void dialogRegeneratesApprovalAfterDirectionOrSelectionChanges(){
        QTemporaryDir root;put(root.filePath("left/a.txt"));QDir().mkpath(root.filePath("right"));SynchronizationDialog dialog(request(root.path()));dialog.show();auto *preview=dialog.findChild<QPushButton*>("syncPreview");QTRY_VERIFY(preview->isEnabled());auto *confirm=dialog.findChild<QCheckBox*>("syncConfirm");auto *apply=dialog.findChild<QPushButton*>("syncApply");confirm->setChecked(true);QVERIFY(apply->isEnabled());dialog.findChild<QComboBox*>("syncDirection")->setCurrentIndex(1);QVERIFY(!confirm->isChecked());QVERIFY(!apply->isEnabled());dialog.findChild<QComboBox*>("syncDirection")->setCurrentIndex(0);preview->click();QTRY_VERIFY(preview->isEnabled());confirm->setChecked(true);QVERIFY(apply->isEnabled());apply->click();QTRY_VERIFY_WITH_TIMEOUT(dialog.findChild<QLabel*>("syncStatus")->text().contains("Cópias concluídas"),10000);QCOMPARE(read(root.filePath("right/a.txt")),QByteArray("original"));QVERIFY(!apply->isEnabled());QMetaObject::invokeMethod(&dialog,"reject");
    }
    void comparisonRouteUsesCompleteTypedScopeAndInvalidatesAfterUse(){
        QTemporaryDir root;put(root.filePath("left/a.txt"));QDir().mkpath(root.filePath("right"));MainWindow window(root.filePath("left"),root.filePath("right"),false);window.show();window.findChild<QAction*>("compareFolders")->trigger();auto *comparison=window.findChild<ComparisonDialog*>("comparisonDialog");QVERIFY(comparison);auto *sync=comparison->findChild<QPushButton*>("prepareSynchronization");QVERIFY(!sync->isEnabled());comparison->findChild<QPushButton*>("startComparison")->click();QTRY_VERIFY_WITH_TIMEOUT(sync->isEnabled(),10000);
        QTimer::singleShot(0,&window,[&]{auto *dialog=window.findChild<SynchronizationDialog*>("synchronizationDialog");QVERIFY(dialog);auto close=qScopeGuard([dialog]{QMetaObject::invokeMethod(dialog,"reject");});auto *preview=dialog->findChild<QPushButton*>("syncPreview");QTRY_VERIFY(preview->isEnabled());QVERIFY(!window.findChild<QAction*>("copyFiles")->isEnabled());const auto out=qEnvironmentVariable("DIROPEN_SYNC_CAPTURES");if(!out.isEmpty()){QVERIFY(QDir().mkpath(out));QVERIFY(dialog->grab().save(out+"/synchronization-preview.png"));}QVERIFY(!QFileInfo::exists(root.filePath("right/a.txt")));});sync->click();QVERIFY(!sync->isEnabled());comparison->findChild<QLineEdit*>("comparisonLeft")->setText(root.path());QVERIFY(!sync->isEnabled());comparison->findChild<QPushButton*>("closeComparison")->click();QVERIFY(window.close());
    }
};
int main(int argc,char**argv){QApplication app(argc,argv);app.setStyle("Fusion");SynchronizationTest test;return QTest::qExec(&test,argc,argv);}
#include "synchronization_test.moc"
