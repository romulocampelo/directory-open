#include "guardedtransfer.h"
#include "operationqueuedialog.h"
#include "operationdialog.h"
#include "mainwindow.h"
#include "filepanel.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QUuid>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
class OperationQueueTest final:public QObject {
    Q_OBJECT
    void put(const QString &p,const QByteArray &bytes="original"){QFile f(p);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write(bytes),bytes.size());}
    QByteArray read(const QString &p){QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray{};}
    void add(OperationQueueDialog *dialog,const QStringList &sources,const QString &destination,TransferEngine engine=TransferEngine::Windows){
        dialog->setDraft(sources,destination,engine);QTimer::singleShot(0,dialog,[dialog]{auto *confirmation=dialog->findChild<OperationDialog*>("operationDialog");QVERIFY(confirmation);auto close=qScopeGuard([confirmation]{if(confirmation->isVisible())confirmation->reject();});if(auto *conflict=confirmation->findChild<QComboBox*>("operationConflict"))conflict->setCurrentIndex(1);confirmation->findChild<QPushButton*>("confirmOperation")->click();});dialog->findChild<QPushButton*>("queueAdd")->click();
    }
    OperationQueueDialog *queue(QWidget *parent=nullptr){auto *dialog=new OperationQueueDialog(false,[]{return true;},parent);dialog->show();return dialog;}
private slots:
    void changedFilesAndReplacedDestinationFailBeforeMutation(){
        QTemporaryDir root;QDir(root.path()).mkdir("destination");const auto source=root.filePath("a.txt");put(source);OperationRequest request;request.sources={source};request.destination=root.filePath("destination");request.nativeUi=false;auto approved=approveTransfer(request);QVERIFY(approved.error.isEmpty());put(root.filePath("destination/unrelated.txt"));QVERIFY(revalidateTransfer(approved).isEmpty());put(source,"changed");QVERIFY(!revalidateTransfer(approved).isEmpty());std::atomic_bool cancel=false;QCOMPARE(performApprovedTransfer(approved,cancel).state,OperationState::Failed);QVERIFY(!QFileInfo::exists(root.filePath("destination/a.txt")));
        approved=approveTransfer(request);QVERIFY(approved.error.isEmpty());QVERIFY(QDir().rename(request.destination,root.filePath("old-destination")));QVERIFY(QDir().mkdir(request.destination));QVERIFY(!revalidateTransfer(approved).isEmpty());QVERIFY(!QFileInfo::exists(root.filePath("destination/a.txt")));
    }
    void pendingReorderRemovalAndSequentialCopiesStayPausedUntilStart(){
        QTemporaryDir root;for(const auto *name:{"one","two","three"})QVERIFY(QDir(root.path()).mkdir(name));const auto source=root.filePath("a.txt");put(source);QPointer<OperationQueueDialog> dialog=queue();add(dialog,{source},root.filePath("one"));add(dialog,{source},root.filePath("two"));add(dialog,{source},root.filePath("three"));QCOMPARE(dialog->pendingCount(),3);QVERIFY(!dialog->busy());QVERIFY(!QFileInfo::exists(root.filePath("one/a.txt")));auto *list=dialog->findChild<QListWidget*>("queuePending");list->setCurrentRow(1);dialog->findChild<QPushButton*>("queueUp")->click();QVERIFY(list->item(0)->text().contains("two"));list->setCurrentRow(2);dialog->findChild<QPushButton*>("queueRemove")->click();QCOMPARE(dialog->pendingCount(),2);dialog->findChild<QPushButton*>("queueStart")->click();QTRY_VERIFY_WITH_TIMEOUT(!dialog->busy()&&dialog->pendingCount()==0,10000);QCOMPARE(read(root.filePath("one/a.txt")),QByteArray("original"));QCOMPARE(read(root.filePath("two/a.txt")),QByteArray("original"));QVERIFY(!QFileInfo::exists(root.filePath("three/a.txt")));auto *results=dialog->findChild<QTreeWidget*>("queueResults");QCOMPARE(results->topLevelItemCount(),2);QVERIFY(results->topLevelItem(0)->text(2).contains("two"));QCOMPARE(results->topLevelItem(1)->text(1),QString("Concluída"));QCOMPARE(read(source),QByteArray("original"));QVERIFY(dialog->requestShutdown());
    }
    void failurePausesPendingAndRequiresExplicitContinue(){
        QTemporaryDir root;QDir(root.path()).mkdir("out");const auto a=root.filePath("a.txt"),b=root.filePath("b.txt");put(a);put(b);QPointer<OperationQueueDialog> dialog=queue();add(dialog,{a},root.filePath("out"));add(dialog,{b},root.filePath("out"));put(a,"changed since approval");dialog->findChild<QPushButton*>("queueStart")->click();QTRY_VERIFY_WITH_TIMEOUT(!dialog->busy(),10000);QCOMPARE(dialog->pendingCount(),1);auto *results=dialog->findChild<QTreeWidget*>("queueResults");QCOMPARE(results->topLevelItem(0)->text(1),QString("Erro"));QVERIFY(!QFileInfo::exists(root.filePath("out/a.txt")));QVERIFY(!QFileInfo::exists(root.filePath("out/b.txt")));dialog->findChild<QPushButton*>("queueStart")->click();QTRY_VERIFY_WITH_TIMEOUT(!dialog->busy()&&dialog->pendingCount()==0,10000);QCOMPARE(read(root.filePath("out/b.txt")),QByteArray("original"));QVERIFY(dialog->requestShutdown());
    }
    void pauseAffectsNextTaskAndCancelledCurrentDoesNotRunPending(){
        QTemporaryDir root;QDir(root.path()).mkdir("out");const auto a=root.filePath("a.txt"),b=root.filePath("b.txt");put(a);put(b);QPointer<OperationQueueDialog> dialog=queue();add(dialog,{a},root.filePath("out"));add(dialog,{b},root.filePath("out"));auto pause=connect(dialog,&OperationQueueDialog::busyChanged,dialog,[dialog]{if(dialog->busy())dialog->findChild<QPushButton*>("queuePause")->click();});dialog->findChild<QPushButton*>("queueStart")->click();QTRY_VERIFY_WITH_TIMEOUT(!dialog->busy(),10000);disconnect(pause);QCOMPARE(dialog->pendingCount(),1);QCOMPARE(read(root.filePath("out/a.txt")),QByteArray("original"));QVERIFY(!QFileInfo::exists(root.filePath("out/b.txt")));auto cancel=connect(dialog,&OperationQueueDialog::busyChanged,dialog,[dialog]{if(dialog->busy())dialog->findChild<QPushButton*>("queueCancel")->click();});dialog->findChild<QPushButton*>("queueStart")->click();QTRY_VERIFY_WITH_TIMEOUT(!dialog->busy(),10000);disconnect(cancel);QVERIFY(!QFileInfo::exists(root.filePath("out/b.txt")));QCOMPARE(dialog->findChild<QTreeWidget*>("queueResults")->topLevelItem(1)->text(1),QString("Cancelada"));QVERIFY(dialog->requestShutdown());
    }
    void mainWindowRoutesSelectionAndCloseRequiresDiscard(){
        QTemporaryDir root;QDir(root.path()).mkdir("out");put(root.filePath("a.txt"));MainWindow window(root.path(),root.filePath("out"),false);window.show();QTRY_COMPARE(window.leftPanel()->visibleCount(),2);window.leftPanel()->filesView()->selectAll();window.findChild<QAction*>("operationQueue")->trigger();auto *dialog=window.findChild<OperationQueueDialog*>("operationQueueDialog");QVERIFY(dialog);QVERIFY(dialog->findChild<QPlainTextEdit*>("queueSources")->toPlainText().contains("a.txt"));add(dialog,{root.filePath("a.txt")},root.filePath("out"));
        const auto capture=qEnvironmentVariable("DIROPEN_QUEUE_CAPTURES");if(!capture.isEmpty()){QVERIFY(QDir().mkpath(capture));QVERIFY(dialog->grab().save(capture+"/operation-queue.png"));}
        QTimer::singleShot(0,&window,[&]{auto *question=dialog->findChild<QMessageBox*>();QVERIFY(question);question->button(QMessageBox::No)->click();});QVERIFY(!window.close());QVERIFY(window.isVisible());QCOMPARE(dialog->pendingCount(),1);QTimer::singleShot(0,&window,[&]{auto *question=dialog->findChild<QMessageBox*>();QVERIFY(question);question->button(QMessageBox::Yes)->click();});window.close();QTRY_VERIFY(!window.isVisible());QVERIFY(!QFileInfo::exists(root.filePath("out/a.txt")));
    }
    void delegatedCopyPausesFollowingWindowsTaskWithoutClaimingCompletion(){
        QTemporaryDir root;QVERIFY(QDir(root.path()).mkdir("out"));QVERIFY(QDir(root.path()).mkdir("TeraCopy"));
        const auto probe=root.filePath("TeraCopy/TeraCopy.exe");QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),probe));
        const QStringList keys{"ProgramW6432","ProgramFiles","ProgramFiles(x86)","PATH"};QMap<QString,QByteArray> prior;QSet<QString> existed;
        for(const auto &key:keys){const auto name=key.toUtf8();if(qEnvironmentVariableIsSet(name.constData()))existed.insert(key);prior[key]=qgetenv(name.constData());qputenv(name.constData(),key=="PATH"?QDir::toNativeSeparators(QCoreApplication::applicationDirPath()).toUtf8()+";"+prior[key]:root.path().toUtf8());}
        const auto application=QCoreApplication::applicationName();QCoreApplication::setApplicationName("DirOpen-QueueFixture-"+QUuid::createUuid().toString(QUuid::WithoutBraces));QStandardPaths::setTestModeEnabled(true);
        const auto restore=qScopeGuard([&]{for(const auto &key:keys){const auto name=key.toUtf8();if(existed.contains(key))qputenv(name.constData(),prior[key]);else qunsetenv(name.constData());}QStandardPaths::setTestModeEnabled(false);QCoreApplication::setApplicationName(application);});
        const auto records=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/transfers/teracopy";
        QPointer<OperationQueueDialog> dialog=queue();
        const auto cleanup=qScopeGuard([&]{
            // Close/wait even after an assertion fails. This is our unique probe,
            // never the installed/shared TeraCopy process. Windows holds its image
            // until exit; keep its list/receipt until that image can be removed.
            if(dialog){QTimer::singleShot(0,dialog,[dialog]{if(dialog)if(auto *q=dialog->findChild<QMessageBox*>())q->button(QMessageBox::Yes)->click();});dialog->requestShutdown();if(dialog)delete dialog.data();}
            QElapsedTimer timer;timer.start();while(QFileInfo::exists(probe)&&!QFile::remove(probe)&&timer.elapsed()<30000)QTest::qWait(50);
            if(QFileInfo::exists(probe)){root.setAutoRemove(false);QTest::qFail(qPrintable("Executável de prova ainda em uso; arquivos preservados: "+root.path()),__FILE__,__LINE__);return;}
            bool cleaned=true;for(const auto &name:QDir(records).entryList({"copy-*.txt"},QDir::Files)){const auto list=QDir(records).filePath(name);for(const auto &file:QStringList{list+".ack",list+".json",list})if(QFileInfo::exists(file)&&!QFile::remove(file))cleaned=false;}
            QDir().rmdir(records);QDir().rmdir(QFileInfo(records).absolutePath());QDir().rmdir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));if(!cleaned)QTest::qFail(qPrintable("Limpeza dos registros próprios não confirmada: "+records),__FILE__,__LINE__);
        });
        const auto a=root.filePath("a.txt"),b=root.filePath("b.txt");put(a);put(b);add(dialog,{a},root.filePath("out"),TransferEngine::TeraCopy);add(dialog,{b},root.filePath("out"));dialog->findChild<QPushButton*>("queueStart")->click();
        // A freshly copied executable took 12.25 seconds to start on this host.
        // Use a bounded 30-second startup allowance, keeping all outcome checks.
        QTRY_VERIFY_WITH_TIMEOUT(!dialog->busy(),30000);QCOMPARE(dialog->pendingCount(),1);QVERIFY(dialog->findChild<QTreeWidget*>("queueResults")->topLevelItem(0)->text(1).contains("Encaminhada"));QVERIFY(!QFileInfo::exists(root.filePath("out/a.txt")));QVERIFY(!QFileInfo::exists(root.filePath("out/b.txt")));
        const auto lists=QDir(records).entryList({"copy-*.txt"},QDir::Files);QCOMPARE(lists.size(),1);QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(QDir(records).filePath(lists.first())+".ack"),30000);
    }
};
int main(int argc,char**argv){if(argc>2&&QByteArray(argv[1])=="Copy"){QCoreApplication app(argc,argv);QFile ack(app.arguments()[2].mid(1)+".ack");if(!ack.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 2;ack.write("received");return 0;}QApplication app(argc,argv);app.setStyle("Fusion");OperationQueueTest test;return QTest::qExec(&test,argc,argv);}
#include "operation_queue_test.moc"
