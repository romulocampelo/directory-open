#include "foldercomparison.h"
#include "windowspath.h"
#include "comparisondialog.h"
#include "mainwindow.h"
#include "filepanel.h"
#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <windows.h>
#include <aclapi.h>
#include <psapi.h>
#include <cstdlib>

class ComparisonTest final : public QObject {
    Q_OBJECT
private:
    void write(const QString &path, const QByteArray &bytes = "preservado") {
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), qint64(bytes.size()));
    }
    void timestamp(const QString &path) {
        const auto native = QDir::toNativeSeparators(path).toStdWString();
        HANDLE file = CreateFileW(native.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(file != INVALID_HANDLE_VALUE); FILETIME stamp{0x12340000, 0x01dc0000}; const bool ok = SetFileTime(file, nullptr, nullptr, &stamp); CloseHandle(file); QVERIFY(ok);
    }
    ComparisonReport compare(const QString &left, const QString &right, bool content = false, bool recursive = true) {
        std::atomic_bool cancel{false}; return compareFolders({left, right, recursive, content}, cancel);
    }
    const ComparisonEntry &entry(const ComparisonReport &report, const QString &relative) {
        for (const auto &result : report.entries) if (result.relativePath == relative) return result;
        qFatal("Missing comparison entry: %s", qPrintable(relative));
        std::abort();
    }
    QPair<QString, QString> roots(const QString &base) {
        const auto left = base + "/left", right = base + "/right";
        if (!QDir().mkpath(left) || !QDir().mkpath(right)) qFatal("Could not create owned fixture roots");
        return {left, right};
    }
    quint64 ticks(const FILETIME &time) { return (quint64(time.dwHighDateTime) << 32) | time.dwLowDateTime; }
private slots:
    void filesystemPathBoundarySeparatesNativeAndDisplayRepresentations() {
        QCOMPARE(WindowsPath::forFileApi("C:/Fotos/../Férias/ação.txt"), QString("\\\\?\\C:\\Férias\\ação.txt"));
        QCOMPARE(WindowsPath::fromFileApi(WindowsPath::forFileApi("C:/Férias/ação.txt")), QString("C:/Férias/ação.txt"));
        QCOMPARE(WindowsPath::forFileApi("//servidor/acervo/Férias"), QString("\\\\?\\UNC\\servidor\\acervo\\Férias"));
        QCOMPARE(WindowsPath::fromFileApi(WindowsPath::forFileApi("//servidor/acervo/Férias")), QString("//servidor/acervo/Férias"));
        QCOMPARE(WindowsPath::fromFileApi("C:\\sem-prefixo\\ação.txt"), QString("C:/sem-prefixo/ação.txt"));
    }
    void metadataNeverConfirmsContentEvenWithSameSizeAndTime() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto [left, right] = roots(fixture.path());
        write(left + "/same.txt", "abc"); write(right + "/same.txt", "xyz"); timestamp(left + "/same.txt"); timestamp(right + "/same.txt");
        const auto metadata = compare(left, right); QCOMPARE(metadata.state, ComparisonState::Completed); QCOMPARE(metadata.bytesRead, quint64(0));
        QCOMPARE(entry(metadata, "same.txt").kind, ComparisonKind::MetadataMatch); QVERIFY(entry(metadata, "same.txt").detail.contains("não foi conferido"));
        const auto bytes = compare(left, right, true); QCOMPARE(bytes.state, ComparisonState::Completed);
        QCOMPARE(entry(bytes, "same.txt").kind, ComparisonKind::ContentDifferent); QCOMPARE(bytes.bytesRead, quint64(6));
    }
    void contentsCanMatchWithDifferentDatesAndEmptyFilesAreConfirmed() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto [left, right] = roots(fixture.path());
        write(left + "/a", "abc"); write(right + "/a", "abc"); timestamp(left + "/a");
        write(left + "/empty", {}); write(right + "/empty", {});
        QCOMPARE(entry(compare(left, right), "a").kind, ComparisonKind::MetadataDifferent);
        const auto report = compare(left, right, true); QCOMPARE(report.state, ComparisonState::Completed);
        QCOMPARE(entry(report, "a").kind, ComparisonKind::ContentEqual); QCOMPARE(entry(report, "empty").kind, ComparisonKind::ContentEqual); QCOMPARE(report.bytesRead, quint64(6));
    }
    void differentSizesDoNotReadBytesAndLateDifferencesAreFound() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto [left, right] = roots(fixture.path());
        QByteArray a(600000, 'a'), b = a; b[b.size() - 1] = 'b'; write(left + "/large", a); write(right + "/large", b);
        write(left + "/size", "1"); write(right + "/size", "22");
        const auto report = compare(left, right, true); QCOMPARE(report.state, ComparisonState::Completed);
        QCOMPARE(entry(report, "large").kind, ComparisonKind::ContentDifferent); QCOMPARE(entry(report, "size").kind, ComparisonKind::ContentDifferent); QCOMPARE(report.bytesRead, quint64(1200000));
        QFile preserved(left + "/large"); QVERIFY(preserved.open(QIODevice::ReadOnly)); QCOMPARE(preserved.readAll(), a);
    }
    void recursiveScopeIncludesHiddenUnicodeAndReportsTypeConflicts() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto [left, right] = roots(fixture.path());
        QVERIFY(QDir(left).mkpath("Férias/sub")); QVERIFY(QDir(right).mkpath("Férias/sub"));
        write(left + "/Férias/sub/relatório.txt"); write(right + "/Férias/sub/relatório.txt"); write(left + "/.oculto");
        const auto hidden = QDir::toNativeSeparators(left + "/.oculto").toStdWString(); QVERIFY(SetFileAttributesW(hidden.c_str(), FILE_ATTRIBUTE_HIDDEN));
        write(left + "/conflict"); QVERIFY(QDir(right).mkdir("conflict")); write(right + "/right.txt");
        const auto report = compare(left, right, true); QCOMPARE(report.state, ComparisonState::Completed);
        QCOMPARE(entry(report, "Férias").kind, ComparisonKind::Folders); QCOMPARE(entry(report, "Férias/sub/relatório.txt").kind, ComparisonKind::ContentEqual);
        QCOMPARE(entry(report, ".oculto").kind, ComparisonKind::LeftOnly); QCOMPARE(entry(report, "right.txt").kind, ComparisonKind::RightOnly); QCOMPARE(entry(report, "conflict").kind, ComparisonKind::TypeConflict);
        const auto shallow = compare(left, right, false, false); QCOMPARE(shallow.state, ComparisonState::Completed); QCOMPARE(shallow.entries.size(), 4);
    }
    void namesMatchExactlyAndLongWindowsPathsRemainUsable() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto [left, right] = roots(fixture.path());
        write(left + "/Case.txt"); write(right + "/case.txt");
        QString deep; for (int i = 0; i < 8; ++i) deep += "pasta-longa-para-comparar-arquivos/";
        QVERIFY(QDir(left).mkpath(deep)); QVERIFY(QDir(right).mkpath(deep)); QVERIFY((left + '/' + deep).size() > 260);
        write(left + '/' + deep + "ação.txt"); write(right + '/' + deep + "ação.txt");
        const auto report = compare(left, right, true); QCOMPARE(report.state, ComparisonState::Completed);
        QCOMPARE(entry(report, "Case.txt").kind, ComparisonKind::LeftOnly); QCOMPARE(entry(report, "case.txt").kind, ComparisonKind::RightOnly);
        QCOMPARE(entry(report, deep + "ação.txt").kind, ComparisonKind::ContentEqual);
    }
    void emptyFoldersAndUnavailableRootsAreDifferentOutcomes() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto [left, right] = roots(fixture.path());
        const auto empty = compare(left, right); QCOMPARE(empty.state, ComparisonState::Completed); QVERIFY(empty.entries.isEmpty());
        const auto missing = compare(left, right + "/missing"); QCOMPARE(missing.state, ComparisonState::Failed); QVERIFY(missing.errors); QVERIFY(!missing.details.isEmpty());
        QCOMPARE(compare("", right).state, ComparisonState::Failed); QCOMPARE(compare("relative", right).state, ComparisonState::Failed);
    }
    void limitsDoNotInventExclusiveItemsAndDepthIsExplicit() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto [left, right] = roots(fixture.path());
        for (int i = 0; i < 8; ++i) { write(left + QString("/%1").arg(i)); write(right + QString("/%1").arg(i)); }
        std::atomic_bool cancel{false}; ComparisonRequest request{left, right}; request.maxEntriesPerSide = 2;
        const auto limited = compareFolders(request, cancel); QCOMPARE(limited.state, ComparisonState::Limited); QVERIFY(limited.entries.size() <= 4);
        for (const auto &result : limited.entries) QVERIFY(result.kind != ComparisonKind::LeftOnly && result.kind != ComparisonKind::RightOnly);
        QVERIFY(QDir(left).mkpath("sub/deep")); request.maxEntriesPerSide = 5000; request.maxDepth = 1;
        const auto depth = compareFolders(request, cancel); QCOMPARE(depth.state, ComparisonState::Limited); QCOMPARE(entry(depth, "sub").kind, ComparisonKind::LeftOnly);
    }
    void junctionIsSkippedAndDoesNotProveAbsenceBelowIt() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto [left, right] = roots(fixture.path()); const auto target = fixture.path() + "/target";
        QVERIFY(QDir().mkpath(target)); write(target + "/preserved.txt"); QVERIFY(QDir(right).mkpath("linked")); write(right + "/linked/visible.txt");
        const auto link = left + "/linked";
        QCOMPARE(QProcess::execute("cmd.exe", {"/d", "/c", "mklink", "/J", QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)}), 0);
        const auto report = compare(left, right, true); const auto invalidRoot = compare(link, right);
        QVERIFY(QDir().rmdir(link));
        QCOMPARE(report.state, ComparisonState::Completed); QCOMPARE(report.skippedLinks, quint64(1));
        QCOMPARE(entry(report, "linked").kind, ComparisonKind::IgnoredLink); QCOMPARE(entry(report, "linked/visible.txt").kind, ComparisonKind::Unknown);
        QCOMPARE(invalidRoot.state, ComparisonState::Failed); QVERIFY(QFileInfo::exists(target + "/preserved.txt"));
    }
    void folderReplacedWithJunctionBeforeTraversalIsNotFollowed() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto [left, right] = roots(fixture.path()); const auto target = fixture.path() + "/target";
        QVERIFY(QDir(left).mkdir("changing")); QVERIFY(QDir().mkpath(target)); write(target + "/outside.txt");
        const auto link = left + "/changing"; std::atomic_bool cancel{false}; bool replaced = false;
        const auto report = compareFolders({left, right}, cancel, [&](quint64 examined, quint64) {
            if (examined == 1 && !replaced) {
                replaced = true; QVERIFY(QDir().rmdir(link));
                QCOMPARE(QProcess::execute("cmd.exe", {"/d", "/c", "mklink", "/J", QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)}), 0);
            }
        });
        QVERIFY(QDir().rmdir(link)); QVERIFY(replaced); QCOMPARE(report.state, ComparisonState::Partial);
        QCOMPARE(report.entries.size(), 1); QVERIFY(QFileInfo::exists(target + "/outside.txt"));
    }
    void unreadableFolderCannotTurnOtherChildrenIntoExclusiveFiles() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto [left, right] = roots(fixture.path());
        QVERIFY(QDir(left).mkdir("restricted")); QVERIFY(QDir(right).mkdir("restricted")); write(left + "/restricted/a"); write(right + "/restricted/a"); write(left + "/visible");
        auto native = QDir::toNativeSeparators(left + "/restricted").toStdWString(); PACL original = nullptr; PSECURITY_DESCRIPTOR descriptor = nullptr;
        QCOMPARE(GetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &original, nullptr, &descriptor), DWORD(ERROR_SUCCESS));
        ACL deny{}; QVERIFY(InitializeAcl(&deny, sizeof(deny), ACL_REVISION));
        const auto denied = SetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, &deny, nullptr);
        if (denied != ERROR_SUCCESS) { LocalFree(descriptor); QFAIL("Could not prepare owned ACL fixture"); }
        const auto report = compare(left, right, true);
        const auto restored = SetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, original, nullptr); LocalFree(descriptor);
        QCOMPARE(restored, DWORD(ERROR_SUCCESS)); QCOMPARE(report.state, ComparisonState::Partial); QVERIFY(report.errors);
        QCOMPARE(entry(report, "restricted/a").kind, ComparisonKind::Unknown); QCOMPARE(entry(report, "visible").kind, ComparisonKind::LeftOnly);
    }
    void openWriterPreventsFalseContentConfirmationAndReleasesHandles() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto [left, right] = roots(fixture.path()); write(left + "/a"); write(right + "/a");
        const auto native = QDir::toNativeSeparators(left + "/a").toStdWString();
        HANDLE writer = CreateFileW(native.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(writer != INVALID_HANDLE_VALUE); const auto report = compare(left, right, true); CloseHandle(writer);
        QCOMPARE(report.state, ComparisonState::Partial); QCOMPARE(entry(report, "a").kind, ComparisonKind::Unknown);
        QCOMPARE(entry(compare(left, right, true), "a").kind, ComparisonKind::ContentEqual); QVERIFY(QFile::rename(left + "/a", left + "/renamed"));
    }
    void changesBetweenInventoryAndContentRemainUnknown() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto [left, right] = roots(fixture.path()); write(left + "/a"); write(right + "/a");
        std::atomic_bool cancel{false}; bool changed = false;
        const auto report = compareFolders({left, right, true, true}, cancel, [&](quint64 examined, quint64) {
            if (examined == 2 && !changed) { changed = true; write(left + "/a", "new size"); }
        });
        QVERIFY(changed); QCOMPARE(report.state, ComparisonState::Partial); QCOMPARE(entry(report, "a").kind, ComparisonKind::Unknown);
    }
    void cancellationBeforeScanAndDuringContentRetainsOnlyProvenResults() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto [left, right] = roots(fixture.path());
        write(left + "/a", QByteArray(1024 * 1024, 'a')); write(right + "/a", QByteArray(1024 * 1024, 'a'));
        std::atomic_bool cancel{true}; const auto before = compareFolders({left, right, true, true}, cancel); QCOMPARE(before.state, ComparisonState::Cancelled); QCOMPARE(before.examined, quint64(0));
        cancel = false; const auto during = compareFolders({left, right, true, true}, cancel, [&](quint64, quint64 bytes) { if (bytes) cancel = true; });
        QCOMPARE(during.state, ComparisonState::Cancelled); QCOMPARE(entry(during, "a").kind, ComparisonKind::Unknown); QVERIFY(during.bytesRead > 0 && during.bytesRead < 2 * 1024 * 1024);
        QVERIFY(QFile::remove(left + "/a")); QVERIFY(QFile::remove(right + "/a"));
    }
    void workerOwnsItsResultAndDestructorJoinsOutstandingWork() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto [left, right] = roots(fixture.path()); write(left + "/a"); write(right + "/a");
        ComparisonJob job({left, right, true, true}); QSignalSpy finished(&job, &QThread::finished); job.start(); QTRY_COMPARE(finished.count(), 1); QVERIFY(job.wait(1000));
        auto report = job.takeReport(); QCOMPARE(report.state, ComparisonState::Completed); QCOMPARE(report.entries.size(), 1); QCOMPARE(job.examined(), quint64(2));
        { ComparisonJob running({left, right, true, true}); running.start(); } // Cancels and joins even before finished is delivered.
    }
    void publicUiOpensCorrectPanelFiltersAndClosesWhileBusy() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto [left, right] = roots(fixture.path());
        write(left + "/igual.txt", "abc"); write(right + "/igual.txt", "abc"); write(right + "/exclusivo.txt");
        write(left + "/alterado.txt", "antes"); write(right + "/alterado.txt", "agora"); write(left + "/só-esquerda.txt");
        QVERIFY(QDir(left).mkdir("Férias")); QVERIFY(QDir(right).mkdir("Férias")); write(left + "/Férias/relatório.txt"); write(right + "/Férias/relatório.txt");
        MainWindow window(left, right, false); window.show(); auto *action = window.findChild<QAction *>("compareFolders"); QVERIFY(action);
        if (QGuiApplication::platformName() == "windows") {
            QVERIFY(QTest::qWaitForWindowExposed(&window)); window.raise(); window.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&window));
            window.leftPanel()->filesView()->setFocus(); QTRY_VERIFY(window.leftPanel()->filesView()->hasFocus());
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
        } else action->trigger();
        QPointer<ComparisonDialog> dialog = window.findChild<ComparisonDialog *>(); QVERIFY(dialog);
        QCOMPARE(QDir::fromNativeSeparators(dialog->findChild<QLineEdit *>("comparisonLeft")->text()), left);
        dialog->findChild<QCheckBox *>("comparisonContent")->setChecked(true); auto *start = dialog->findChild<QPushButton *>("startComparison"); start->click(); QTRY_VERIFY(start->isEnabled());
        auto *results = dialog->findChild<QTreeWidget *>("comparisonResults"); QCOMPARE(results->topLevelItemCount(), 6);
        const auto capture = qEnvironmentVariable("DIROPEN_COMPARISON_CAPTURE");
        if (!capture.isEmpty()) {
            // Documentation uses illustrative addresses; paths in the result data remain the owned fixtures.
            auto *leftEdit = dialog->findChild<QLineEdit *>("comparisonLeft"); auto *rightEdit = dialog->findChild<QLineEdit *>("comparisonRight");
            const auto actualLeft = leftEdit->text(), actualRight = rightEdit->text();
            leftEdit->setText("C:\\Exemplos\\Origem"); rightEdit->setText("C:\\Exemplos\\Destino");
            QVERIFY(QTest::qWaitForWindowExposed(dialog)); QVERIFY(dialog->grab().save(capture));
            leftEdit->setText(actualLeft); rightEdit->setText(actualRight);
        }
        dialog->findChild<QCheckBox *>("comparisonDifferences")->setChecked(true);
        int visible = 0; QTreeWidgetItem *only = nullptr;
        for (int i = 0; i < results->topLevelItemCount(); ++i) if (!results->topLevelItem(i)->isHidden()) { ++visible; if (results->topLevelItem(i)->text(0) == "exclusivo.txt") only = results->topLevelItem(i); }
        QCOMPARE(visible, 3); QVERIFY(only); QCOMPARE(only->text(0), QString("exclusivo.txt")); results->setCurrentItem(only);
        QVERIFY(!dialog->findChild<QPushButton *>("openComparisonLeft")->isEnabled()); dialog->findChild<QPushButton *>("openComparisonRight")->click();
        QCOMPARE(window.leftPanel()->tabCount(), 1); QCOMPARE(window.rightPanel()->tabCount(), 2); QCOMPARE(window.rightPanel()->path(), right);
        dialog->close(); QTRY_VERIFY(dialog.isNull()); action->trigger(); dialog = window.findChild<ComparisonDialog *>(); QVERIFY(dialog);
        dialog->findChild<QPushButton *>("startComparison")->click(); dialog->close(); QTRY_VERIFY(dialog.isNull());
    }
    void representativeCompletedTasksMeasureFirstAndRepeatedRuns() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto [left, right] = roots(fixture.path());
        const QByteArray bytes(16384, 'x'); for (int i = 0; i < 200; ++i) { write(left + QString("/%1.bin").arg(i), bytes); write(right + QString("/%1.bin").arg(i), bytes); }
        for (bool content : {false, true}) for (int pass = 0; pass < 2; ++pass) {
            FILETIME created{}, exited{}, kernelBefore{}, userBefore{}, kernelAfter{}, userAfter{};
            QVERIFY(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernelBefore, &userBefore)); QElapsedTimer timer; timer.start();
            const auto report = compare(left, right, content); const auto elapsed = timer.elapsed();
            QVERIFY(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernelAfter, &userAfter)); PROCESS_MEMORY_COUNTERS memory{}; memory.cb = sizeof(memory);
            QVERIFY(GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)));
            qInfo() << "Comparison benchmark; 200 pairs x 16 KiB; content" << content << "pass" << pass
                << "elapsed ms" << elapsed << "CPU ms" << double(ticks(kernelAfter) + ticks(userAfter) - ticks(kernelBefore) - ticks(userBefore)) / 10000
                << "cumulative process peak MiB" << double(memory.PeakWorkingSetSize) / (1024 * 1024) << "bytes read" << report.bytesRead;
            QCOMPARE(report.state, ComparisonState::Completed); QCOMPARE(report.entries.size(), 200);
            QCOMPARE(report.bytesRead, content ? quint64(200 * 16384 * 2) : quint64(0));
        }
    }
};
QTEST_MAIN(ComparisonTest)
#include "comparison_test.moc"
