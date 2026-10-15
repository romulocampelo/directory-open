#include "batchrename.h"
#include "batchrenamedialog.h"
#include "mainwindow.h"
#include "filepanel.h"
#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QDateEdit>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProcess>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <windows.h>
#include <psapi.h>

class BatchRenameTest final : public QObject {
    Q_OBJECT
    void write(const QString &path, QByteArray bytes = "preservado") { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), qint64(bytes.size())); }
    BatchRenamePlan plan(const QStringList &paths, BatchRenameOptions options = {}) { std::atomic_bool cancel{false}; return planBatchRename(paths, options, cancel); }
    OperationResult apply(const BatchRenamePlan &preview) { std::atomic_bool cancel{false}; return performBatchRename(preview, cancel); }
private slots:
    void numberedReplacementDatesAndExtensionsRespectInputOrder() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto a = fixture.filePath("férias-final.txt"), b = fixture.filePath("foto.jpg"); write(a); write(b);
        BatchRenameOptions options; options.pattern = "{date}_{name}_{n}"; options.find = "final"; options.replacement = "revisto"; options.start = 7; options.step = 2; options.digits = 4; options.dateSource = RenameDateSource::Fixed; options.fixedDate = QDate(2027, 1, 3);
        const auto result = plan({b, a}, options); QVERIFY(result.valid()); QCOMPARE(result.items[0].name, QString("2027-01-03_foto_0007.jpg")); QCOMPARE(result.items[1].name, QString("2027-01-03_férias-revisto_0009.txt"));
        QVERIFY(QFileInfo::exists(a)); QVERIFY(!QFileInfo::exists(result.items[0].target));
    }
    void datesUseActualFilesystemCreationAndModificationTimes() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto path=fixture.filePath("arquivo.TXT"); write(path);
        const auto native=QDir::toNativeSeparators(path).toStdWString(); HANDLE file=CreateFileW(native.c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);QVERIFY(file!=INVALID_HANDLE_VALUE);
        auto stamp=[](QDate value){const auto ticks=quint64(QDateTime(value,QTime(12,0)).toMSecsSinceEpoch()+11644473600000LL)*10000;return FILETIME{DWORD(ticks),DWORD(ticks>>32)};};const auto created=stamp(QDate(2024,2,3)),modified=stamp(QDate(2025,4,5));const bool ok=SetFileTime(file,&created,nullptr,&modified);CloseHandle(file);QVERIFY(ok);
        BatchRenameOptions options;options.pattern="{date}";QCOMPARE(plan({path},options).items.first().name,QString("2025-04-05.TXT"));options.dateSource=RenameDateSource::Created;const auto creationPlan=plan({path},options);QCOMPARE(creationPlan.items.first().name,QString("2024-02-03.TXT"));
        file=CreateFileW(native.c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);QVERIFY(file!=INVALID_HANDLE_VALUE);const auto changedCreation=stamp(QDate(2024,3,4));const bool changed=SetFileTime(file,&changedCreation,nullptr,nullptr);CloseHandle(file);QVERIFY(changed);QCOMPARE(apply(creationPlan).state,OperationState::Failed);
        options.pattern="{name}";options.preserveExtension=false;options.find="txt";options.replacement="log";options.caseSensitive=false;QCOMPARE(plan({path},options).items.first().name,QString("arquivo.log"));
    }
    void junctionMissingSourceAndSelectionLimitsFailWithoutMutation() {
        QTemporaryDir fixture;QVERIFY(fixture.isValid());const auto target=fixture.filePath("target"),link=fixture.filePath("link");QVERIFY(QDir().mkdir(target));write(target+"/inside");
        QCOMPARE(QProcess::execute("cmd.exe",{"/d","/c","mklink","/J",QDir::toNativeSeparators(link),QDir::toNativeSeparators(target)}),0);const auto preview=plan({link});QVERIFY(QDir().rmdir(link));QVERIFY(!preview.valid());QVERIFY(QFileInfo::exists(target+"/inside"));QVERIFY(!plan({fixture.filePath("missing")}).valid());
        QStringList excessive;for(int i=0;i<1001;++i)excessive.append(target);QVERIFY(!plan(excessive).valid());
    }
    void insertedNamesAndReplacementAreNeverReparsedAsTokens() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto path = fixture.filePath("arquivo{name}.tar.gz"); write(path);
        BatchRenameOptions options; options.pattern = "{name}_{n}"; const auto result = plan({path}, options); QVERIFY(result.valid()); QCOMPARE(result.items[0].name, QString("arquivo{name}.tar_001.gz"));
        options.find = "arquivo"; options.replacement = "{date}"; QCOMPARE(plan({path}, options).items[0].name, QString("{date}{name}.tar_001.gz"));
    }
    void invalidNamesDuplicateTargetsAndExistingDestinationsBlockWholePlan() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto a = fixture.filePath("a.txt"), b = fixture.filePath("b.txt"); write(a); write(b);
        BatchRenameOptions options; options.pattern = "CON"; QVERIFY(!plan({a}, options).valid()); options.pattern = "{unknown}"; QVERIFY(!plan({a}, options).valid());
        options.pattern = "same"; const auto duplicate = plan({a, b}, options); QVERIFY(!duplicate.valid()); QVERIFY(!duplicate.items[0].error.isEmpty()); QVERIFY(!duplicate.items[1].error.isEmpty());
        options.pattern = "b"; const auto occupied = plan({a}, options); QVERIFY(!occupied.valid()); QCOMPARE(apply(occupied).state, OperationState::Failed); QVERIFY(QFileInfo::exists(a)); QVERIFY(QFileInfo::exists(b));
    }
    void expansionLimitsAreRejectedBeforeLargeNamesAreRetained() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto a = fixture.filePath(QString(200,'a') + ".txt"); write(a);
        BatchRenameOptions options; options.find = "a"; options.replacement = QString(1024,'x'); options.pattern = QString("{name}").repeated(150);
        const auto result = plan({a},options); QVERIFY(!result.valid()); QVERIFY(result.items.first().name.size() <= 255); QVERIFY(result.items.first().error.contains("255"));
    }
    void realNativeRenamePreservesBytesIncludesFoldersAndSupportsCaseOnly() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto a = fixture.filePath("ação.txt"), folder = fixture.filePath("pasta.com.ponto"); write(a, "original"); QVERIFY(QDir().mkdir(folder)); write(folder + "/inside");
        const auto preview = plan({a, folder}); QVERIFY(preview.valid()); const auto done = apply(preview); QVERIFY2(done.state == OperationState::Completed, qPrintable(done.error)); QCOMPARE(done.completedCount(), 2);
        QVERIFY(!QFileInfo::exists(a)); QFile renamed(preview.items[0].target); QVERIFY(renamed.open(QIODevice::ReadOnly)); QCOMPARE(renamed.readAll(), QByteArray("original")); renamed.close(); QVERIFY(QFileInfo::exists(preview.items[1].target + "/inside"));
        BatchRenameOptions upper; upper.pattern = "AÇÃO_001"; const auto casing = plan({preview.items[0].target}, upper); QVERIFY(casing.valid()); QCOMPARE(apply(casing).state, OperationState::Completed);
        QVERIFY(QDir(fixture.path()).entryList(QDir::Files).contains("AÇÃO_001.txt"));
    }
    void previewThenChangedSourceOrNewConflictStopsBeforeFirstWrite() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto a = fixture.filePath("a"), b = fixture.filePath("b"); write(a); write(b);
        const auto first = plan({a,b}); write(b, "mudou de tamanho"); QCOMPARE(apply(first).state, OperationState::Failed); QVERIFY(QFileInfo::exists(a)); QVERIFY(!QFileInfo::exists(first.items[0].target));
        const auto second = plan({a,b}); write(second.items[1].target, "destino preservado"); QCOMPARE(apply(second).state, OperationState::Failed); QVERIFY(QFileInfo::exists(a)); QVERIFY(QFileInfo::exists(b));
    }
    void lateCollisionNeverOverwritesAndReportsPartialWork() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto a = fixture.filePath("a"), b = fixture.filePath("b"); write(a); write(b); const auto preview = plan({a,b}); QVERIFY(preview.valid());
        std::atomic_bool cancel{false}; const auto result = performBatchRename(preview, cancel, [&](quint32, quint32 done, const QString &) { if (done == 1) write(preview.items[1].target, "collision"); });
        QCOMPARE(result.state, OperationState::Incomplete); QCOMPARE(result.completedCount(), 1); QVERIFY(QFileInfo::exists(b)); QFile collision(preview.items[1].target); QVERIFY(collision.open(QIODevice::ReadOnly)); QCOMPARE(collision.readAll(), QByteArray("collision"));
    }
    void cancellationRetainsCompletedNamesAndNoOpDoesNotMutate() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto a = fixture.filePath("a"), b = fixture.filePath("b"); write(a); write(b); const auto preview = plan({a,b}); std::atomic_bool cancel{false};
        const auto result = performBatchRename(preview, cancel, [&](quint32, quint32 done, const QString &) { if (done == 1) cancel = true; });
        QCOMPARE(result.state, OperationState::Cancelled); QCOMPARE(result.completedCount(), 1); QVERIFY(QFileInfo::exists(preview.items[0].target)); QVERIFY(QFileInfo::exists(b));
        BatchRenameOptions unchanged; unchanged.pattern = "{name}"; const auto same = plan({b}, unchanged); QCOMPARE(same.changes(), 0); const auto skipped = apply(same); QCOMPARE(skipped.state, OperationState::Completed); QCOMPARE(skipped.items[0].state, OperationItemState::Skipped);
    }
    void replacedFileWithSameBytesAndTimestampsIsRejectedByIdentity() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto a = fixture.filePath("a"), replacement = fixture.filePath("other"); write(a); write(replacement); const auto preview = plan({a});
        QVERIFY(QFile::rename(a, fixture.filePath("preserved"))); QVERIFY(QFile::rename(replacement, a));
        QCOMPARE(apply(preview).state, OperationState::Failed); QVERIFY(QFileInfo::exists(a)); QVERIFY(!QFileInfo::exists(preview.items[0].target));
    }
    void invalidSelectionAndTamperedPlanAreRejected() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); write(fixture.filePath("a")); QVERIFY(QDir(fixture.path()).mkdir("sub")); write(fixture.filePath("sub/b"));
        QVERIFY(!plan({fixture.filePath("a"),fixture.filePath("sub/b")}).valid()); QVERIFY(!plan({fixture.filePath("a"),fixture.filePath("a")}).valid()); QVERIFY(!plan({}).valid());
        auto tampered = plan({fixture.filePath("a")}); tampered.items[0].target = fixture.filePath("sub/escape"); QCOMPARE(apply(tampered).state, OperationState::Failed); QVERIFY(QFileInfo::exists(fixture.filePath("a")));
        std::atomic_bool cancel{true}; QVERIFY(planBatchRename({fixture.filePath("a")}, {}, cancel).cancelled);
    }
    void dialogInvalidatesConfirmationAndExclusionRenumbersPreview() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto a = fixture.filePath("a.txt"), b = fixture.filePath("b.txt"); write(a); write(b); BatchRenameDialog dialog({b,a}); dialog.show();
        auto *preview = dialog.findChild<QPushButton *>("previewBatchRename"); auto *apply = dialog.findChild<QPushButton *>("applyBatchRename"); auto *rows = dialog.findChild<QTreeWidget *>("batchRenameRows");
        QTRY_COMPARE(rows->topLevelItem(0)->text(2), QString("b_001.txt")); QVERIFY(!apply->isEnabled()); auto *confirm = dialog.findChild<QCheckBox *>("confirmBatchRename"); confirm->setChecked(true); QVERIFY(apply->isEnabled());
        dialog.findChild<QLineEdit *>("renamePattern")->setText("Foto_{n}"); QVERIFY(!confirm->isChecked()); QVERIFY(!apply->isEnabled()); rows->topLevelItem(0)->setCheckState(0, Qt::Unchecked); preview->click(); QTRY_VERIFY(preview->isEnabled()); QCOMPARE(rows->topLevelItem(1)->text(2), QString("Foto_001.txt"));
        const auto capture = qEnvironmentVariable("DIROPEN_BATCH_CAPTURE"); if (!capture.isEmpty()) { QVERIFY(QTest::qWaitForWindowExposed(&dialog)); QVERIFY(dialog.grab().save(capture)); }
        confirm->setChecked(true); apply->click(); QTRY_VERIFY(dialog.applied()); QVERIFY(QFileInfo::exists(fixture.filePath("Foto_001.txt"))); QVERIFY(QFileInfo::exists(b)); QVERIFY(!QFileInfo::exists(a));
    }
    void publicPanelOrderAndBatchActionPreserveOtherOperations() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); write(fixture.filePath("a.txt")); write(fixture.filePath("b.txt")); write(fixture.filePath("c.txt"));
        MainWindow window(fixture.path(),fixture.path(),false);window.show();auto *view=window.leftPanel()->filesView();QTRY_COMPARE(window.leftPanel()->visibleCount(),3);view->model()->sort(0,Qt::DescendingOrder);
        QTRY_COMPARE(view->model()->index(0,0,view->rootIndex()).data().toString(),QString("c.txt"));auto *selection=view->selectionModel();selection->select(view->model()->index(2,0,view->rootIndex()),QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);selection->select(view->model()->index(0,0,view->rootIndex()),QItemSelectionModel::Select|QItemSelectionModel::Rows);
        QCOMPARE(window.leftPanel()->selectedPaths(),QStringList({fixture.filePath("c.txt"),fixture.filePath("a.txt")}));auto *action=window.findChild<QAction *>("batchRename");QVERIFY(action->isEnabled());
        QTimer poll;poll.setInterval(25);bool finished=false;connect(&poll,&QTimer::timeout,&window,[&]{auto *dialog=window.findChild<BatchRenameDialog *>();if(!dialog)return;auto *rows=dialog->findChild<QTreeWidget *>("batchRenameRows");if(rows->topLevelItem(0)->text(2)!="c_001.txt")return;poll.stop();QCOMPARE(rows->topLevelItem(1)->text(2),QString("a_002.txt"));QVERIFY(!window.findChild<QAction *>("copyFiles")->isEnabled());finished=true;dialog->close();});poll.start();
        QTimer::singleShot(10000,&window,[&]{poll.stop();if(auto *dialog=window.findChild<BatchRenameDialog *>())dialog->close();});action->trigger();QVERIFY(finished);QVERIFY(QFileInfo::exists(fixture.filePath("c.txt")));QVERIFY(QFileInfo::exists(fixture.filePath("a.txt")));
    }
    void completedPreviewAndApplyMeasureRepresentativeTask() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); QStringList sources; for (int i=0;i<100;++i) { const auto path = fixture.filePath(QString("original-%1.txt").arg(i)); write(path); sources.append(path); }
        FILETIME created{},exited{},kb{},ub{},ka{},ua{}; GetProcessTimes(GetCurrentProcess(),&created,&exited,&kb,&ub); QElapsedTimer elapsed; elapsed.start();
        const auto preview = plan(sources); QVERIFY(preview.valid()); const auto result = apply(preview); const auto ms = elapsed.elapsed(); GetProcessTimes(GetCurrentProcess(),&created,&exited,&ka,&ua);
        auto ticks=[](const FILETIME &t){return (quint64(t.dwHighDateTime)<<32)|t.dwLowDateTime;}; PROCESS_MEMORY_COUNTERS memory{}; memory.cb=sizeof(memory); GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory));
        qInfo()<<"Batch task 100 files: preview + apply, elapsed ms"<<ms<<"CPU ms"<<double(ticks(ka)+ticks(ua)-ticks(kb)-ticks(ub))/10000<<"cumulative process peak MiB"<<double(memory.PeakWorkingSetSize)/(1024*1024);
        QCOMPARE(result.state,OperationState::Completed); QCOMPARE(result.completedCount(),100);
    }
};
QTEST_MAIN(BatchRenameTest)
#include "batch_rename_test.moc"
