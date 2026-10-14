#include "mainwindow.h"
#include "filepanel.h"
#include "filetransfer.h"
#include "operationdialog.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <QtEndian>
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <thread>
#include <chrono>

class TransferTest : public QObject {
    Q_OBJECT
private:
    std::unique_ptr<QMimeData> originalClipboard;
    void write(const QString &path, const QByteArray &data) { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(data), qint64(data.size())); }
    QByteArray hash(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; QCryptographicHash sha(QCryptographicHash::Sha256); if (!sha.addData(&file)) return {}; return sha.result(); }
    void createFolders(QTemporaryDir &fixture) { QVERIFY(fixture.isValid()); QVERIFY(QDir(fixture.path()).mkdir("source")); QVERIFY(QDir(fixture.path()).mkdir("target")); }
    void confirm(MainWindow &window) {
        QTimer::singleShot(0, &window, [&window] { auto *dialog = window.findChild<OperationDialog *>("operationDialog"); if (dialog) { dialog->findChild<QComboBox *>("operationConflict")->setCurrentIndex(1); dialog->findChild<QPushButton *>("confirmOperation")->click(); } });
    }
    bool sendDrop(QTreeView *view, QMimeData *mime, const QPoint &point, Qt::KeyboardModifiers modifiers = {}, Qt::DropActions actions = Qt::CopyAction | Qt::MoveAction) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
        QDragEnterEvent enter(QPointF(point), actions, mime, Qt::LeftButton, modifiers);
#else
        QDragEnterEvent enter(point, actions, mime, Qt::LeftButton, modifiers);
#endif
        QApplication::sendEvent(view->viewport(), &enter);
        if (!enter.isAccepted()) return false;
        QDropEvent drop(QPointF(point), actions, mime, Qt::LeftButton, modifiers); QApplication::sendEvent(view->viewport(), &drop); return drop.isAccepted();
    }
    void answerQueuedDrop(MainWindow &window, QTimer &timer, bool &sawDialog, bool accept, const QString &destination = {}) {
        timer.setInterval(1);
        connect(&timer, &QTimer::timeout, &window, [&window, &timer, &sawDialog, accept, destination] {
            auto *dialog = window.findChild<OperationDialog *>("operationDialog"); if (!dialog) return;
            sawDialog = destination.isEmpty() || QDir::cleanPath(dialog->request().destination) == QDir::cleanPath(destination);
            timer.stop();
            if (accept) { dialog->findChild<QComboBox *>("operationConflict")->setCurrentIndex(1); dialog->findChild<QPushButton *>("confirmOperation")->click(); }
            else dialog->reject();
        });
        QTimer::singleShot(5000, &window, [&window] { if (auto *dialog = window.findChild<OperationDialog *>("operationDialog")) dialog->reject(); });
        timer.start();
    }
private slots:
    void initTestCase() {
        // Retain clipboard formats in memory only; restore them after this suite.
        originalClipboard = std::make_unique<QMimeData>(); const auto *mime = qApp->clipboard()->mimeData();
        if (mime) for (const auto &format : mime->formats()) originalClipboard->setData(format, mime->data(format));
    }
    void cleanupTestCase() { qApp->clipboard()->setMimeData(originalClipboard.release()); }
    void init() { qApp->clipboard()->clear(); }
    void copyShortcutAndPasteUseActiveDestinationWithoutConsumingCopy() {
        QTemporaryDir fixture; createFolders(fixture); const auto file = fixture.path() + "/source/ação.txt"; write(file, "copy content"); const auto before = hash(file);
        MainWindow window(fixture.path() + "/source", fixture.path() + "/target", false); window.show(); QTRY_COMPARE(window.leftPanel()->visibleCount(), 1);
        auto *source = window.leftPanel()->findChild<QTreeView *>("files"); source->setFocus(); QTRY_VERIFY(source->hasFocus()); source->selectAll(); QTest::keyClick(source, Qt::Key_C, Qt::ControlModifier);
        auto prepared = readFileTransfer(qApp->clipboard()->mimeData()); QVERIFY(prepared.valid()); QCOMPARE(prepared.kind, OperationKind::Copy); QCOMPARE(prepared.paths, QStringList{file});
        auto *target = window.rightPanel()->findChild<QTreeView *>("files"); target->setFocus(); QTRY_VERIFY(target->hasFocus()); confirm(window); QTest::keyClick(target, Qt::Key_V, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QMessageBox *>("operationResult"), 10000);
        QCOMPARE(hash(fixture.path() + "/target/ação.txt"), before); QCOMPARE(hash(file), before); QCOMPARE(readFileTransfer(qApp->clipboard()->mimeData()).token, prepared.token);
    }
    void cutOnlyStagesThenPasteMovesAndClearsClipboard() {
        QTemporaryDir fixture; createFolders(fixture); const auto file = fixture.path() + "/source/one.txt"; write(file, "move content"); const auto before = hash(file);
        MainWindow window(fixture.path() + "/source", fixture.path() + "/target", false); window.show(); QTRY_COMPARE(window.leftPanel()->visibleCount(), 1);
        auto *source = window.leftPanel()->findChild<QTreeView *>("files"); source->setFocus(); QTRY_VERIFY(source->hasFocus()); source->selectAll(); QTest::keyClick(source, Qt::Key_X, Qt::ControlModifier);
        QVERIFY(QFileInfo::exists(file)); QVERIFY(QDir(fixture.path() + "/target").isEmpty()); QCOMPARE(readFileTransfer(qApp->clipboard()->mimeData()).kind, OperationKind::Move);
        QTRY_VERIFY(source->model()->index(0, 0, source->rootIndex()).data(Qt::ForegroundRole).canConvert<QColor>());
        const auto capture = qEnvironmentVariable("NAVEGA_CAPTURE_CUT");
        if (!capture.isEmpty()) { source->clearSelection(); QVERIFY(window.grab().save(capture)); }
        auto *target = window.rightPanel()->findChild<QTreeView *>("files"); target->setFocus(); QTRY_VERIFY(target->hasFocus()); confirm(window); QTest::keyClick(target, Qt::Key_V, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QMessageBox *>("operationResult"), 10000); QVERIFY(!QFileInfo::exists(file)); QCOMPARE(hash(fixture.path() + "/target/one.txt"), before);
        QVERIFY(!readFileTransfer(qApp->clipboard()->mimeData()).valid()); QVERIFY(!window.findChild<QAction *>("pasteFiles")->isEnabled());
    }
    void cancellingPastePreservesCutAndDisablesTransfersDuringConfirmation() {
        QTemporaryDir fixture; createFolders(fixture); const auto file = fixture.path() + "/source/one.txt"; write(file, "preserved");
        FileClipboard clipboard; clipboard.publish({file}, OperationKind::Move); const auto token = clipboard.snapshot().transfer.token;
        MainWindow window(fixture.path() + "/source", fixture.path() + "/target", false); window.show(); bool disabled = false;
        window.rightPanel()->findChild<QTreeView *>("files")->setFocus();
        QTimer::singleShot(0, &window, [&] { auto *dialog = window.findChild<OperationDialog *>("operationDialog"); if (dialog) { disabled = !window.findChild<QAction *>("pasteFiles")->isEnabled() && !window.findChild<QAction *>("createFolder")->isEnabled(); dialog->reject(); } });
        window.findChild<QAction *>("pasteFiles")->trigger(); QVERIFY(disabled); QVERIFY(QFileInfo::exists(file)); QVERIFY(QDir(fixture.path() + "/target").isEmpty()); QCOMPARE(clipboard.snapshot().transfer.token, token);
    }
    void clipboardReplacementDuringConfirmedMoveIsPreserved() {
        QTemporaryDir fixture; createFolders(fixture); const auto file = fixture.path() + "/source/one.txt"; write(file, "preserved");
        FileClipboard clipboard; clipboard.publish({file}, OperationKind::Move);
        MainWindow window(fixture.path() + "/target", fixture.path() + "/source", false); window.show();
        QTimer::singleShot(0, &window, [&] { auto *dialog = window.findChild<OperationDialog *>("operationDialog"); if (dialog) { qApp->clipboard()->setText("novo conteúdo"); dialog->findChild<QPushButton *>("confirmOperation")->click(); } });
        window.findChild<QAction *>("pasteFiles")->trigger(); QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QMessageBox *>("operationResult"), 10000);
        QCOMPARE(qApp->clipboard()->text(), QString("novo conteúdo")); QVERIFY(!QFileInfo::exists(file)); QVERIFY(QFileInfo::exists(fixture.path() + "/target/one.txt"));
    }
    void partiallyMovedBatchLeavesOnlyPendingClipboardItems() {
        QTemporaryDir fixture; createFolders(fixture); const auto a = fixture.path() + "/source/a.txt", b = fixture.path() + "/source/b.txt"; write(a, "a"); write(b, "b");
        FileClipboard clipboard; clipboard.publish({a, b}, OperationKind::Move); const auto before = clipboard.snapshot();
        OperationRequest request; request.kind = OperationKind::Move; request.sources = {a, b}; request.destination = fixture.path() + "/target"; request.nativeUi = false;
        std::atomic_bool cancel{false}; const auto result = performOperation(request, cancel, [&](quint32, quint32, const QString &item) { if (item.endsWith("/b.txt")) cancel.store(true); });
        QCOMPARE(result.state, OperationState::Cancelled); QCOMPARE(result.completedCount(), 1); clipboard.finishMove(before, result);
        QCOMPARE(clipboard.snapshot().transfer.paths, QStringList{b}); QVERIFY(!QFileInfo::exists(a)); QVERIFY(QFileInfo::exists(b));
    }
    void identicalPathsWithNewClipboardIdentityAreNotCleared() {
        QTemporaryDir fixture; createFolders(fixture); const auto file = fixture.path() + "/source/one.txt"; write(file, "preserved");
        FileClipboard clipboard; clipboard.publish({file}, OperationKind::Move); const auto old = clipboard.snapshot(); clipboard.publish({file}, OperationKind::Move); const auto replacement = clipboard.snapshot();
        QVERIFY(old.transfer.token != replacement.transfer.token);
        OperationRequest request; request.kind = OperationKind::Move; request.sources = {file}; request.destination = fixture.path() + "/target"; request.nativeUi = false;
        std::atomic_bool cancel{false}; const auto result = performOperation(request, cancel); QCOMPARE(result.state, OperationState::Completed); clipboard.finishMove(old, result);
        QCOMPARE(clipboard.snapshot().transfer.token, replacement.transfer.token); QCOMPARE(clipboard.snapshot().transfer.paths, QStringList{file});
    }
    void missingSourceShowsErrorAndDoesNotCopyOtherItems() {
        QTemporaryDir fixture; createFolders(fixture); const auto file = fixture.path() + "/source/one.txt"; write(file, "preserved");
        FileClipboard clipboard; clipboard.publish({file, fixture.path() + "/source/missing.txt"}, OperationKind::Copy);
        MainWindow window(fixture.path() + "/target", fixture.path() + "/source", false); window.show(); bool sawError = false;
        QTimer::singleShot(0, &window, [&] { auto *dialog = window.findChild<OperationDialog *>("operationDialog"); if (dialog) { dialog->findChild<QPushButton *>("confirmOperation")->click(); sawError = !dialog->findChild<QLabel *>("operationError")->text().isEmpty(); dialog->reject(); } });
        window.findChild<QAction *>("pasteFiles")->trigger(); QVERIFY(sawError); QVERIFY(QFileInfo::exists(file)); QVERIFY(QDir(fixture.path() + "/target").isEmpty());
    }
    void textEditingKeepsStandardCopyCutAndPaste() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); write(fixture.path() + "/one.txt", "unchanged");
        MainWindow window(fixture.path(), fixture.path(), false); window.show(); QTRY_COMPARE(window.leftPanel()->visibleCount(), 1); window.leftPanel()->findChild<QTreeView *>("files")->selectAll();
        auto *address = window.leftPanel()->findChild<QLineEdit *>("address"); address->setFocus(); QTRY_VERIFY(address->hasFocus()); address->setText("abcd"); address->setSelection(1, 2);
        QTest::keyClick(address, Qt::Key_C, Qt::ControlModifier); QCOMPARE(qApp->clipboard()->text(), QString("bc")); QVERIFY(!readFileTransfer(qApp->clipboard()->mimeData()).valid());
        QTest::keyClick(address, Qt::Key_X, Qt::ControlModifier); QCOMPARE(address->text(), QString("ad")); qApp->clipboard()->setText("novo"); address->selectAll(); QTest::keyClick(address, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(address->text(), QString("novo")); QCOMPARE(hash(fixture.path() + "/one.txt"), QCryptographicHash::hash("unchanged", QCryptographicHash::Sha256));
    }
    void fileFormatsRoundTripIncludingNativeWindowsClipboard() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto first = fixture.path() + "/ação.txt", second = fixture.path() + "/two.txt"; write(first, "one"); write(second, "two");
        FileClipboard clipboard; clipboard.publish({first, second}, OperationKind::Move); const auto data = clipboard.snapshot().transfer; QCOMPARE(data.paths, QStringList({first, second})); QCOMPARE(data.kind, OperationKind::Move);
        if (QApplication::platformName() == "windows") {
            QVERIFY(OpenClipboard(nullptr)); struct Close { ~Close() { CloseClipboard(); } } close;
            QVERIFY(IsClipboardFormatAvailable(CF_HDROP)); auto drop = static_cast<HDROP>(GetClipboardData(CF_HDROP)); QVERIFY(drop);
            QCOMPARE(DragQueryFileW(drop, 0xffffffff, nullptr, 0), UINT(2));
            wchar_t path[32768]; QVERIFY(DragQueryFileW(drop, 0, path, 32768)); QCOMPARE(QString::fromWCharArray(path), QDir::toNativeSeparators(first));
            const auto format = RegisterClipboardFormatW(L"Preferred DropEffect"); const auto value = GetClipboardData(format); QVERIFY(value);
            auto *effect = static_cast<DWORD *>(GlobalLock(value)); QVERIFY(effect); const auto copy = *effect; GlobalUnlock(value); QCOMPARE(copy, DWORD(DROPEFFECT_MOVE));
            qInfo("Native CF_HDROP and Preferred DropEffect verified.");
        }
    }
    void rejectsRemoteMixedMalformedAndTextPayloads() {
        QMimeData text; text.setText("C:\\somewhere\\file.txt"); QVERIFY(!readFileTransfer(&text).valid());
        QMimeData mixed; mixed.setUrls({QUrl::fromLocalFile("C:/local.txt"), QUrl("https://example.com/file.txt")}); QVERIFY(!readFileTransfer(&mixed).valid());
        auto link = makeFileTransfer({"C:/local.txt"}, OperationKind::Copy); QByteArray value(4, '\0'); qToLittleEndian<quint32>(4, value.data()); link->setData(preferredDropEffectFormat(), value); QVERIFY(!readFileTransfer(link.get()).valid());
        link->setData(preferredDropEffectFormat(), "bad"); QVERIFY(!readFileTransfer(link.get()).valid());
        QMimeData duplicate; duplicate.setUrls({QUrl::fromLocalFile("C:/folder/file.txt"), QUrl::fromLocalFile("C:/FOLDER/file.txt")}); QCOMPARE(readFileTransfer(&duplicate).paths.size(), 1);
    }
    void internalDragCopiesOnlyAfterConfirmation() {
        QTemporaryDir fixture; createFolders(fixture); const auto file = fixture.path() + "/source/one.txt"; write(file, "drag copy");
        MainWindow window(fixture.path() + "/source", fixture.path() + "/target", false); window.show(); FileDragData mime({file}); QVERIFY(!mime.hasUrls());
        auto *target = window.rightPanel()->findChild<QTreeView *>("files"); QTimer answer; bool saw = false; answerQueuedDrop(window, answer, saw, true, fixture.path() + "/target");
        QVERIFY(sendDrop(target, &mime, QPoint(10, 100))); QVERIFY(QFileInfo::exists(file)); QVERIFY(QDir(fixture.path() + "/target").isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QMessageBox *>("operationResult"), 10000); QVERIFY(saw); QCOMPARE(hash(file), hash(fixture.path() + "/target/one.txt"));
    }
    void shiftDragMovesAndDropOnFolderUsesThatDestination() {
        QTemporaryDir fixture; createFolders(fixture); const auto file = fixture.path() + "/source/one.txt"; write(file, "drag move"); const auto before = hash(file); QVERIFY(QDir(fixture.path() + "/target").mkdir("nested"));
        MainWindow window(fixture.path() + "/source", fixture.path() + "/target", false); window.show(); QTRY_COMPARE(window.rightPanel()->visibleCount(), 1);
        auto *target = window.rightPanel()->findChild<QTreeView *>("files"); FileDragData mime({file}); QTimer answer; bool saw = false; answerQueuedDrop(window, answer, saw, true, fixture.path() + "/target/nested");
        QVERIFY(sendDrop(target, &mime, target->visualRect(target->model()->index(0, 0, target->rootIndex())).center(), Qt::ShiftModifier)); QVERIFY(QFileInfo::exists(file));
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QMessageBox *>("operationResult"), 10000); QVERIFY(saw); QVERIFY(!QFileInfo::exists(file)); QCOMPARE(hash(fixture.path() + "/target/nested/one.txt"), before);
    }
    void rejectedDropConfirmationLeavesAllSourcesAndClipboardUntouched() {
        QTemporaryDir fixture; createFolders(fixture); const auto file = fixture.path() + "/source/one.txt"; write(file, "preserved"); qApp->clipboard()->setText("retained clipboard");
        MainWindow window(fixture.path() + "/source", fixture.path() + "/target", false); window.show(); FileDragData mime({file}); QTimer answer; bool saw = false; answerQueuedDrop(window, answer, saw, false);
        QVERIFY(sendDrop(window.rightPanel()->findChild<QTreeView *>("files"), &mime, QPoint(10, 100), Qt::ShiftModifier)); QTRY_VERIFY_WITH_TIMEOUT(saw, 10000);
        QVERIFY(QFileInfo::exists(file)); QVERIFY(QDir(fixture.path() + "/target").isEmpty()); QCOMPARE(qApp->clipboard()->text(), QString("retained clipboard"));
    }
    void externalDragRequestsAreCopiedAndMoveOnlyOffersRejected() {
        QTemporaryDir fixture; createFolders(fixture); const auto file = fixture.path() + "/source/one.txt"; write(file, "external copy");
        MainWindow window(fixture.path() + "/source", fixture.path() + "/target", false); window.show(); auto mime = makeFileTransfer({file}, OperationKind::Move); auto *target = window.rightPanel()->findChild<QTreeView *>("files");
        QVERIFY(!sendDrop(target, mime.get(), QPoint(10, 100), Qt::ShiftModifier, Qt::MoveAction));
        QTimer answer; bool saw = false; answerQueuedDrop(window, answer, saw, true); QVERIFY(sendDrop(target, mime.get(), QPoint(10, 100), Qt::ShiftModifier));
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QMessageBox *>("operationResult"), 10000); QVERIFY(saw); QVERIFY(QFileInfo::exists(file)); QCOMPARE(hash(file), hash(fixture.path() + "/target/one.txt"));
    }
    void invalidDropNeverStartsAnOperation() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); MainWindow window(fixture.path(), fixture.path(), false); window.show(); QMimeData mime; mime.setUrls({QUrl("https://example.com/file.txt")});
        QVERIFY(!sendDrop(window.leftPanel()->findChild<QTreeView *>("files"), &mime, QPoint(10, 100))); QCoreApplication::processEvents(); QVERIFY(!window.findChild<OperationDialog *>("operationDialog")); QVERIFY(QDir(fixture.path()).isEmpty());
    }
    void nativeClipboardLockIsReportedWithoutReplacingPreviousContent() {
        if (QApplication::platformName() != "windows") QSKIP("Requires the native Windows clipboard; exercised in the Windows run.");
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto file = fixture.path() + "/one.txt"; write(file, "preserved");
        qApp->clipboard()->setText("conteúdo anterior"); QTRY_COMPARE(qApp->clipboard()->text(), QString("conteúdo anterior"));
        std::atomic_bool started{false}, opened{false}, release{false};
        std::thread holder([&] {
            const auto owner = CreateWindowExW(0, L"STATIC", L"Navega clipboard test", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            opened.store(owner && OpenClipboard(owner) != FALSE); started.store(true);
            while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (opened.load()) CloseClipboard();
            if (owner) DestroyWindow(owner);
        });
        while (!started.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        FileClipboard clipboard; const auto error = opened.load() ? clipboard.publish({file}, OperationKind::Move) : QString{};
        release.store(true); holder.join();
        QVERIFY(opened.load()); QVERIFY(!error.isEmpty()); QCOMPARE(qApp->clipboard()->text(), QString("conteúdo anterior"));
        QCOMPARE(hash(file), QCryptographicHash::hash("preserved", QCryptographicHash::Sha256));
    }
};
QTEST_MAIN(TransferTest)
#include "transfer_test.moc"
