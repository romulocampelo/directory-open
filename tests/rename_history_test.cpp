#include "renamehistory.h"
#include "renamehistorydialog.h"
#include "batchrenamedialog.h"
#include "mainwindow.h"
#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <windows.h>
#include <psapi.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLockFile>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

class RenameHistoryTest final : public QObject {
    Q_OBJECT
    std::atomic_bool cancel{false};
    void write(const QString &path, const QByteArray &bytes = "original") { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(bytes),qint64(bytes.size())); }
    QByteArray bytes(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
    BatchRenamePlan plan(const QStringList &sources) { BatchRenameOptions options; options.pattern = "{name}_novo"; return planBatchRename(sources,options,cancel); }
    QString id(const QString &root) { const auto list = runRenameHistory(root,{},cancel); return list.records.isEmpty() ? QString{} : list.records.first().id; }
    UndoRenamePreview preview(const QString &root, const QString &recordId) {
        RenameHistoryRequest request; request.mode = RenameHistoryMode::Preview; request.id = recordId;
        return runRenameHistory(root,request,cancel).preview;
    }
    RenameHistoryReport undo(const QString &root, const UndoRenamePreview &view, QVector<int> selected = {0}) {
        RenameHistoryRequest request; request.mode = RenameHistoryMode::Undo; request.id = view.id; request.baseline = view.baseline; request.selected = selected;
        return runRenameHistory(root,request,cancel);
    }
private slots:
    void init() { cancel = false; }
    void persistsAcrossJobsAndRestoresOriginalNamesAndBytes() {
        QTemporaryDir dir; const auto source = dir.filePath("olá.txt"), root = dir.filePath("history"); write(source);
        const auto original = bytes(source); const auto rename = plan({source}); QVERIFY(rename.valid());
        BatchRenameJob job(rename,nullptr,root); job.start(); QVERIFY(job.wait(10000)); QCOMPARE(job.result().state,OperationState::Completed);
        const auto record = id(root); QVERIFY(!record.isEmpty()); QVERIFY(QFileInfo::exists(root + "/" + record + ".json.bak"));
        RenameHistoryRequest request; request.mode = RenameHistoryMode::Preview; request.id = record;
        RenameHistoryJob read(root,request); read.start(); QVERIFY(read.wait(10000)); const auto view = read.report().preview;
        QCOMPARE(view.items.size(),1); QCOMPARE(view.items[0].state,UndoRenameState::Ready); QVERIFY(!QFileInfo::exists(source));
        const auto result = undo(root,view); QVERIFY2(result.error.isEmpty(),qPrintable(result.error)); QCOMPARE(result.operation.state,OperationState::Completed);
        QCOMPARE(bytes(source),original); QVERIFY(!QFileInfo::exists(rename.items[0].target));
        QCOMPARE(preview(root,record).items[0].state,UndoRenameState::AlreadyOriginal);
        QVERIFY(!undo(root,preview(root,record)).error.isEmpty()); QCOMPARE(bytes(source),original);
    }
    void occupiedOriginalAndModifiedCurrentAreBlockedButSubsetCanBeUndone() {
        QTemporaryDir dir; const auto root = dir.filePath("history"), a = dir.filePath("a.txt"), b = dir.filePath("b.txt"), c = dir.filePath("c.txt");
        write(a); write(b); write(c); auto rename = plan({a,b,c});
        QCOMPARE(performRecordedBatchRename(rename,root,cancel).completedCount(),3);
        write(a,"different occupant"); write(rename.items[1].target,"modified data");
        const auto view = preview(root,id(root)); QCOMPARE(view.items[0].state,UndoRenameState::Blocked);
        QCOMPARE(view.items[1].state,UndoRenameState::Blocked); QCOMPARE(view.items[2].state,UndoRenameState::Ready);
        QVERIFY(!undo(root,view,{0,2}).error.isEmpty()); QVERIFY(!QFileInfo::exists(c));
        const auto result = undo(root,view,{2}); QCOMPARE(result.operation.state,OperationState::Completed);
        QCOMPARE(bytes(a),QByteArray("different occupant")); QCOMPARE(bytes(rename.items[1].target),QByteArray("modified data")); QCOMPARE(bytes(c),QByteArray("original"));
    }
    void externalCaseOnlyChangeInvalidatesTheApprovedOriginalName() {
        QTemporaryDir dir; const auto source = dir.filePath("antes.txt"), root = dir.filePath("history"); write(source);
        const auto approved = plan({source}); BatchRenameOptions options; options.pattern = "ANTES";
        const auto external = planBatchRename({source},options,cancel);
        QCOMPARE(performBatchRename(external,cancel).state,OperationState::Completed);
        const auto result = performRecordedBatchRename(approved,root,cancel); QCOMPARE(result.state,OperationState::Failed); QCOMPARE(result.completedCount(),0);
        QCOMPARE(inspectRenameItem(source).actualName,QString("ANTES.txt")); QVERIFY(!QFileInfo::exists(approved.items[0].target));
    }
    void revalidatesFilesAndJournalAfterPreview() {
        QTemporaryDir dir; const auto root = dir.filePath("history"), a = dir.filePath("a.txt"); write(a);
        const auto rename = plan({a}); QCOMPARE(performRecordedBatchRename(rename,root,cancel).completedCount(),1);
        const auto view = preview(root,id(root)); write(a,"late occupant"); QVERIFY(!undo(root,view).error.isEmpty()); QCOMPARE(bytes(a),QByteArray("late occupant"));
        QVERIFY(QFile::remove(a)); const auto fresh = preview(root,view.id);
        const auto path = root + "/" + view.id + ".json"; auto doc = QJsonDocument::fromJson(bytes(path)); auto obj = doc.object(); obj["phase"] = "prepared"; write(path,QJsonDocument(obj).toJson());
        QVERIFY(!undo(root,fresh).error.isEmpty()); QVERIFY(!QFileInfo::exists(a)); QVERIFY(QFileInfo::exists(rename.items[0].target));
    }
    void partialCancellationAndInterruptedJournalRecoverOnlyCompletedItems() {
        QTemporaryDir dir; const auto root = dir.filePath("history"), a = dir.filePath("a.txt"), b = dir.filePath("b.txt"); write(a); write(b);
        const auto rename = plan({a,b});
        const auto result = performRecordedBatchRename(rename,root,cancel,[&](quint32,quint32 done,const QString &) { if (done == 1) cancel = true; });
        QCOMPARE(result.state,OperationState::Cancelled); QCOMPARE(result.completedCount(),1); cancel = false;
        const auto record = id(root), path = root + "/" + record + ".json";
        // The backup is the durable pre-mutation intent, as after an interrupted process.
        write(path,bytes(path + ".bak"));
        const auto view = preview(root,record); QCOMPARE(view.items[0].state,UndoRenameState::Ready); QCOMPARE(view.items[1].state,UndoRenameState::AlreadyOriginal);
        QCOMPARE(undo(root,view).operation.state,OperationState::Completed); QCOMPARE(bytes(a),QByteArray("original")); QCOMPARE(bytes(b),QByteArray("original"));
    }
    void caseOnlyRenameUsesActualOnDiskNameAndUndoIsIdempotent() {
        QTemporaryDir dir; const auto source = dir.filePath("antes.txt"), root = dir.filePath("history"); write(source);
        BatchRenameOptions options; options.pattern = "ANTES"; auto rename = planBatchRename({source},options,cancel);
        QVERIFY(rename.valid()); QCOMPARE(performRecordedBatchRename(rename,root,cancel).state,OperationState::Completed);
        QCOMPARE(inspectRenameItem(source).actualName,QString("ANTES.txt")); auto view = preview(root,id(root));
        QCOMPARE(view.items[0].state,UndoRenameState::Ready); QCOMPARE(undo(root,view).operation.state,OperationState::Completed);
        QCOMPARE(inspectRenameItem(source).actualName,QString("antes.txt")); QCOMPARE(preview(root,view.id).items[0].state,UndoRenameState::AlreadyOriginal);
    }
    void cannotMutateWithoutDurableJournalOrWhileAnotherWriterOwnsLock() {
        QTemporaryDir dir; const auto source = dir.filePath("a.txt"), root = dir.filePath("history"); write(source); write(root,"not a folder"); const auto rename = plan({source});
        QCOMPARE(performRecordedBatchRename(rename,root,cancel).state,OperationState::Failed); QVERIFY(QFileInfo::exists(source));
        QVERIFY(QFile::remove(root)); QVERIFY(QDir().mkdir(root)); QLockFile lock(root + "/.lock"); QVERIFY(lock.tryLock());
        QCOMPARE(performRecordedBatchRename(rename,root,cancel).state,OperationState::Failed); QVERIFY(QFileInfo::exists(source));
        lock.unlock(); QCOMPARE(performRecordedBatchRename(rename,root,cancel).state,OperationState::Completed);
    }
    void refusesMalformedUnknownAndOversizedRecordsWithoutChangingThem() {
        QTemporaryDir dir; const auto root = dir.filePath("history"), source = dir.filePath("a.txt"); write(source);
        QCOMPARE(performRecordedBatchRename(plan({source}),root,cancel).state,OperationState::Completed);
        const auto record = id(root), path = root + "/" + record + ".json"; const auto good = bytes(path);
        for (const auto &bad : {QByteArray("{invalid"),QByteArray("{\"version\":99}"),QByteArray(8*1024*1024+1,'x')}) {
            write(path,bad); const auto view = preview(root,record); QVERIFY(!view.error.isEmpty()); QCOMPARE(bytes(path),bad); QVERIFY(!QFileInfo::exists(source));
        }
        write(path,good); auto doc = QJsonDocument::fromJson(good); auto obj = doc.object(); auto items = obj["items"].toArray(); auto item = items[0].toObject(); item["source"] = "../escape"; items[0] = item; obj["items"] = items;
        write(path,QJsonDocument(obj).toJson()); QVERIFY(!preview(root,record).error.isEmpty());
    }
    void identitiesRemainExactBeyondJsonDoublePrecision() {
        QTemporaryDir dir; const auto root = dir.filePath("history"), source = dir.filePath("a.txt"); write(source);
        QCOMPARE(performRecordedBatchRename(plan({source}),root,cancel).state,OperationState::Completed);
        const auto record = id(root), path = root + "/" + record + ".json"; auto obj = QJsonDocument::fromJson(bytes(path)).object();
        auto items = obj["items"].toArray(); auto item = items[0].toObject(); auto after = item["after"].toObject();
        QVERIFY(after["created"].isString()); QVERIFY(after["created"].toString().toULongLong() > 9007199254740992ULL);
        after["modified"] = "18446744073709551615"; item["after"] = after; items[0] = item; obj["items"] = items; write(path,QJsonDocument(obj).toJson());
        const auto view = preview(root,record); QVERIFY(view.error.isEmpty()); QCOMPARE(view.items[0].state,UndoRenameState::Blocked);
    }
    void cancelBeforeApplyAndOwnJournalFolderNeverChangeNames() {
        QTemporaryDir dir; const auto source = dir.filePath("data"), root = source + "/history"; QVERIFY(QDir().mkdir(source));
        const auto rename = plan({source}); QCOMPARE(performRecordedBatchRename(rename,root,cancel).state,OperationState::Failed); QVERIFY(QFileInfo::exists(source));
        const auto file = dir.filePath("a.txt"); write(file); const auto other = plan({file}); cancel = true;
        QCOMPARE(performRecordedBatchRename(other,dir.filePath("history2"),cancel).state,OperationState::Cancelled); QVERIFY(QFileInfo::exists(file)); QCOMPARE(id(dir.filePath("history2")),QString{});
    }
    void dialogRequiresSelectionAndConfirmationAndPersistsUndo() {
        QTemporaryDir dir; const auto root = dir.filePath("history"), source = dir.filePath("a.txt"), b = dir.filePath("b.txt"), c = dir.filePath("c.txt"); write(source); write(b); write(c);
        QCOMPARE(performRecordedBatchRename(plan({source,b,c}),root,cancel).state,OperationState::Completed);
        write(b,"nome ocupado"); write(dir.filePath("c_novo.txt"),"arquivo alterado");
        RenameHistoryDialog dialog(root); dialog.show(); auto *records = dialog.findChild<QTreeWidget *>("renameHistoryRecords");
        auto *rows = dialog.findChild<QTreeWidget *>("renameHistoryItems"); auto *apply = dialog.findChild<QPushButton *>("applyUndoRename"); auto *confirm = dialog.findChild<QCheckBox *>("confirmUndoRename");
        QTRY_COMPARE(records->topLevelItemCount(),1); records->setCurrentItem(records->topLevelItem(0)); QTRY_COMPARE(rows->topLevelItemCount(),3); QTRY_VERIFY(rows->isEnabled());
        QVERIFY(!(rows->topLevelItem(1)->flags() & Qt::ItemIsUserCheckable) || !rows->topLevelItem(1)->data(0,Qt::CheckStateRole).isValid());
        QVERIFY(!apply->isEnabled()); confirm->setChecked(true); QVERIFY(!apply->isEnabled());
        rows->topLevelItem(0)->setCheckState(0,Qt::Checked); QVERIFY(!confirm->isChecked()); confirm->setChecked(true); QVERIFY(apply->isEnabled());
        if (qEnvironmentVariable("QT_QPA_PLATFORM") == "windows" && !qEnvironmentVariable("DIROPEN_CAPTURE_DIR").isEmpty()) {
            records->topLevelItem(0)->setText(1,"C:\\Exemplos\\Documentos"); QVERIFY(QTest::qWaitForWindowExposed(&dialog));
            QVERIFY(dialog.grab().save(qEnvironmentVariable("DIROPEN_CAPTURE_DIR") + "/historico-012.png"));
        }
        apply->click();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(source),10000); QTRY_VERIFY(records->isEnabled()); QVERIFY(!apply->isEnabled()); dialog.close();
    }
    void interruptedUndoCanResumeAndInvalidSelectionsNeverApply() {
        QTemporaryDir dir; const auto root = dir.filePath("history"), a = dir.filePath("a.txt"), b = dir.filePath("b.txt"); write(a); write(b);
        QCOMPARE(performRecordedBatchRename(plan({a,b}),root,cancel).completedCount(),2);
        auto view = preview(root,id(root)); QVERIFY(!undo(root,view,{0,0}).error.isEmpty()); QVERIFY(!undo(root,view,{-1}).error.isEmpty());
        RenameHistoryRequest request; request.mode = RenameHistoryMode::Undo; request.id = view.id; request.baseline = view.baseline; request.selected = {0,1};
        const auto partial = runRenameHistory(root,request,cancel,[&](quint32,quint32 done,const QString &) { if (done == 1) cancel = true; });
        QCOMPARE(partial.operation.state,OperationState::Cancelled); QCOMPARE(partial.operation.completedCount(),1); cancel = false;
        view = preview(root,view.id); QCOMPARE(view.items[0].state,UndoRenameState::AlreadyOriginal); QCOMPARE(view.items[1].state,UndoRenameState::Ready);
        QCOMPARE(undo(root,view,{1}).operation.state,OperationState::Completed); QVERIFY(QFileInfo::exists(a)); QVERIFY(QFileInfo::exists(b));
    }
    void finalJournalFailurePreservesRecoveryMapAndReportsIncomplete() {
        QTemporaryDir dir; const auto root = dir.filePath("history"), source = dir.filePath("a.txt"); write(source);
        QString record;
        const auto result = performRecordedBatchRename(plan({source}),root,cancel,[&](quint32,quint32 done,const QString &) {
            if (done == 0 && record.isEmpty()) { record = id(root); QVERIFY(!record.isEmpty()); QVERIFY(QDir().mkdir(root + "/" + record + ".json.bak")); }
        });
        QCOMPARE(result.state,OperationState::Incomplete); QCOMPARE(result.completedCount(),1); QVERIFY(!result.error.isEmpty());
        auto view = preview(root,record); QCOMPARE(view.items[0].state,UndoRenameState::Ready);
        QVERIFY(QDir().rmdir(root + "/" + record + ".json.bak")); QCOMPARE(undo(root,view).operation.state,OperationState::Completed); QVERIFY(QFileInfo::exists(source));
    }
    void replacementWithSameBytesHasDifferentIdentityAndIsBlocked() {
        QTemporaryDir dir; const auto root = dir.filePath("history"), source = dir.filePath("a.txt"); write(source); const auto rename = plan({source});
        QCOMPARE(performRecordedBatchRename(rename,root,cancel).completedCount(),1);
        // Keep the renamed object alive under a different name, preventing native ID reuse.
        QVERIFY(QFile::rename(rename.items[0].target,dir.filePath("kept.txt"))); write(rename.items[0].target);
        const auto view = preview(root,id(root)); QCOMPARE(view.items[0].state,UndoRenameState::Blocked); QVERIFY(!undo(root,view).error.isEmpty());
        QCOMPARE(bytes(rename.items[0].target),QByteArray("original"));
    }
    void restoresFolderNamesWithoutChangingTheirChildren() {
        QTemporaryDir dir; const auto folder = dir.filePath("documentos"), root = dir.filePath("history"); QVERIFY(QDir().mkdir(folder)); write(folder + "/arquivo.txt","conteúdo preservado");
        const auto rename = plan({folder}); QCOMPARE(performRecordedBatchRename(rename,root,cancel).state,OperationState::Completed);
        QCOMPARE(bytes(rename.items[0].target + "/arquivo.txt"),QByteArray("conteúdo preservado"));
        const auto view = preview(root,id(root)); QCOMPARE(view.items[0].state,UndoRenameState::Ready);
        QCOMPARE(undo(root,view).operation.state,OperationState::Completed); QCOMPARE(bytes(folder + "/arquivo.txt"),QByteArray("conteúdo preservado"));
    }
    void capacityStopsNewBatchesWithoutDeletingOlderRecords() {
        QTemporaryDir dir; const auto root = dir.filePath("history"), source = dir.filePath("a.txt"); QVERIFY(QDir().mkdir(root)); write(source);
        for (int i = 0; i < 1001; ++i) write(root + QString("/00000000-0000-0000-0000-%1.json").arg(i,12,10,QChar('0')),"preserved");
        const auto result = performRecordedBatchRename(plan({source}),root,cancel); QCOMPARE(result.state,OperationState::Failed); QVERIFY(QFileInfo::exists(source));
        const auto list = runRenameHistory(root,{},cancel); QVERIFY(!list.error.isEmpty()); QVERIFY(list.records.isEmpty());
        QCOMPARE(QDir(root).entryList({"*.json"},QDir::Files).size(),1001); QCOMPARE(bytes(root + "/00000000-0000-0000-0000-000000000000.json"),QByteArray("preserved"));
    }
    void measuresCompletedHundredFileWorkflowsWithAndWithoutJournal() {
        auto ticks = [](FILETIME t) { return (quint64(t.dwHighDateTime)<<32)|t.dwLowDateTime; };
        for (int pass = 0; pass < 2; ++pass) for (bool recorded : {false,true}) {
            QTemporaryDir dir; QStringList sources;
            for (int i = 0; i < 100; ++i) { auto path = dir.filePath(QString("item-%1.txt").arg(i)); write(path); sources.append(path); }
            FILETIME created,exited,kb,ub,ka,ua; QVERIFY(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kb,&ub)); QElapsedTimer elapsed; elapsed.start();
            const auto rename = plan(sources); QVERIFY(rename.valid());
            const auto result = performRecordedBatchRename(rename,recorded ? dir.filePath("history") : QString{},cancel);
            const auto ms = elapsed.elapsed(); QVERIFY(GetProcessTimes(GetCurrentProcess(),&created,&exited,&ka,&ua));
            PROCESS_MEMORY_COUNTERS memory{}; memory.cb = sizeof(memory); QVERIFY(GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory)));
            QCOMPARE(result.completedCount(),100); QCOMPARE(result.state,OperationState::Completed);
            qInfo("100-file preview+rename journal=%d pass=%d elapsed=%lldms CPU=%.3fms cumulative process peak=%.3fMiB",
                recorded,pass,ms,double(ticks(ka)+ticks(ua)-ticks(kb)-ticks(ub))/10000,double(memory.PeakWorkingSetSize)/(1024*1024));
        }
    }
    void noSessionDisablesPersistentHistoryAndExplicitSessionEnablesIt() {
        QTemporaryDir dir;
        MainWindow ephemeral(dir.path(),dir.path(),false); QVERIFY(!ephemeral.findChild<QAction *>("renameHistory")->isEnabled());
        MainWindow persistent(dir.path(),dir.path(),false,dir.filePath("session.json")); QVERIFY(persistent.findChild<QAction *>("renameHistory")->isEnabled());
        QVERIFY(!QFileInfo::exists(dir.filePath("session.json.rename-history")));
    }
};
QTEST_MAIN(RenameHistoryTest)
#include "rename_history_test.moc"
