#include "filepanel.h"
#include "mainwindow.h"
#include "operationdialog.h"
#include <QAction>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileSystemModel>
#include <QLineEdit>
#include <QLabel>
#include <QPlainTextEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>
#include <QTimer>

class NavigationTest : public QObject {
    Q_OBJECT
private:
    void writeFile(const QString &path, const QByteArray &bytes) {
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), qint64(bytes.size()));
    }
    QByteArray contents(const QString &path) {
        QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll();
    }
    void confirmForm(MainWindow &window, const QString &name = {}) {
        QTimer::singleShot(0, &window, [&window, name] {
            auto *dialog = window.findChild<OperationDialog *>("operationDialog");
            if (!dialog) return;
            if (auto *field = dialog->findChild<QLineEdit *>("operationName")) field->setText(name);
            if (auto *conflict = dialog->findChild<QComboBox *>("operationConflict")) conflict->setCurrentIndex(1);
            dialog->findChild<QPushButton *>("confirmOperation")->click();
        });
    }
private slots:
    void invalidPathPreservesFolderAndHistory() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        QVERIFY(QDir(fixture.path()).mkdir("one"));
        FilePanel panel; QVERIFY(panel.navigate(fixture.path()));
        QVERIFY(panel.navigate(fixture.path() + "/one"));
        const auto current = panel.path();
        QVERIFY(!panel.navigate(fixture.path() + "/missing")); QCOMPARE(panel.path(), current);
        panel.back(); QCOMPARE(panel.path(), fixture.path());
        panel.forward(); QCOMPARE(panel.path(), current);
    }
    void historyBranchDiscardsForwardEntry() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        QDir dir(fixture.path()); QVERIFY(dir.mkdir("one")); QVERIFY(dir.mkdir("two"));
        FilePanel panel; QVERIFY(panel.navigate(fixture.path()));
        QVERIFY(panel.navigate(fixture.path() + "/one")); panel.back();
        QVERIFY(panel.navigate(fixture.path() + "/two")); panel.forward();
        QCOMPARE(panel.path(), fixture.path() + "/two");
    }
    void tabsHaveIndependentPathsAndHistory() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); QVERIFY(QDir(fixture.path()).mkdir("one"));
        FilePanel panel; QVERIFY(panel.navigate(fixture.path())); panel.newTab(fixture.path() + "/one");
        QCOMPARE(panel.tabCount(), 2); QCOMPARE(panel.path(), fixture.path() + "/one");
        panel.back(); QCOMPARE(panel.path(), fixture.path() + "/one");
        panel.closeTab(1); QCOMPARE(panel.path(), fixture.path());
        panel.closeTab(0); QCOMPARE(panel.tabCount(), 1);
    }
    void filterWorksOnRootAndClearsWithoutLosingFolder() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        for (const auto &name : {"Alpha.txt", "Beta.txt"}) {
            QFile file(fixture.path() + '/' + name); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("hello");
        }
        FilePanel panel; panel.show(); QVERIFY(panel.navigate(fixture.path()));
        QTRY_COMPARE_WITH_TIMEOUT(panel.visibleCount(), 2, 10000);
        panel.setFilter("ALPHA"); QTRY_COMPARE(panel.visibleCount(), 1);
        panel.setFilter("missing"); QTRY_COMPARE(panel.visibleCount(), 0);
        QCOMPARE(panel.path(), fixture.path()); panel.setFilter({}); QTRY_COMPARE(panel.visibleCount(), 2);
    }
    void navigatingAndFilteringDoNotModifyFiles() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString name = fixture.path() + "/original.txt";
        QFile original(name); QVERIFY(original.open(QIODevice::WriteOnly)); original.write("preserved\r\n"); original.close();
        QVERIFY(original.open(QIODevice::ReadOnly)); const auto before = original.readAll(); original.close();
        FilePanel panel; QVERIFY(panel.navigate(fixture.path())); panel.setFilter("original"); panel.newTab(); panel.closeTab(1);
        const auto *model = panel.findChild<QFileSystemModel *>(); QVERIFY(model && model->isReadOnly());
        const auto *view = panel.findChild<QTreeView *>("files");
        QCOMPARE(view->editTriggers(), QAbstractItemView::NoEditTriggers); QVERIFY(view->acceptDrops());
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), before);
        QCOMPARE(QDir(fixture.path()).entryList(QDir::Files), QStringList{"original.txt"});
    }
    void addressAndShortcutRouteToActivePanel() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); QVERIFY(QDir(fixture.path()).mkdir("right"));
        MainWindow window(fixture.path(), fixture.path() + "/right"); window.show();
        auto *address = window.rightPanel()->findChild<QLineEdit *>("address");
        address->setFocus(); QTRY_VERIFY(address->hasFocus());
        window.findChild<QAction *>("newTab")->trigger();
        QCOMPARE(window.rightPanel()->tabCount(), 2); QCOMPARE(window.leftPanel()->tabCount(), 1);
        window.findChild<QAction *>("dualPane")->setChecked(false);
        window.findChild<QAction *>("newTab")->trigger(); QCOMPARE(window.leftPanel()->tabCount(), 2);
    }
    void previewShowsHtmlAsText() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        QFile file(fixture.path() + "/sample.html"); QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray content("<h1>hello</h1><script>throw 'not executed';</script>");
        file.write(content); file.close();
        MainWindow window(fixture.path(), fixture.path()); window.show();
        auto *dock = window.findChild<QDockWidget *>("previewDock"); dock->show();
        auto *view = window.leftPanel()->findChild<QTreeView *>("files");
        QTRY_COMPARE_WITH_TIMEOUT(window.leftPanel()->visibleCount(), 1, 10000);
        view->selectionModel()->select(view->model()->index(0, 0, view->rootIndex()), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        QTRY_COMPARE(dock->findChild<QPlainTextEdit *>()->toPlainText(), QString::fromUtf8(content));
        QVERIFY(dock->findChild<QPlainTextEdit *>()->isReadOnly());
    }
    void sizeSortUsesNumbers() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        for (const auto &name : {"a.txt", "b.txt"}) {
            QFile file(fixture.path() + '/' + name); QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QByteArray(QString(name) == "a.txt" ? 20 : 100, 'x'));
        }
        FilePanel panel; panel.show(); QVERIFY(panel.navigate(fixture.path()));
        QTRY_COMPARE_WITH_TIMEOUT(panel.visibleCount(), 2, 10000);
        auto *view = panel.findChild<QTreeView *>("files"); view->sortByColumn(1, Qt::DescendingOrder);
        QTRY_COMPARE(view->model()->index(0, 0, view->rootIndex()).data().toString(), QString("b.txt"));
    }
    void copyDialogCancellationDoesNotWrite() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        QVERIFY(QDir(fixture.path()).mkdir("source")); QVERIFY(QDir(fixture.path()).mkdir("target"));
        QFile file(fixture.path() + "/source/one.txt"); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("source"); file.close();
        MainWindow window(fixture.path() + "/source", fixture.path() + "/target", false); window.show();
        QTRY_COMPARE(window.leftPanel()->visibleCount(), 1);
        auto *view = window.leftPanel()->findChild<QTreeView *>("files");
        view->selectionModel()->select(view->model()->index(0, 0, view->rootIndex()), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        auto *copy = window.findChild<QAction *>("copyFiles"); QVERIFY(copy->isEnabled());
        bool confirmedDestination = false;
        bool captured = true;
        const auto capture = qEnvironmentVariable("NAVEGA_CAPTURE_COPY_DIALOG");
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<OperationDialog *>("operationDialog");
            if (dialog) {
                confirmedDestination = QDir::fromNativeSeparators(dialog->findChild<QLineEdit *>("operationDestination")->text()) == fixture.path() + "/target";
                if (!capture.isEmpty()) captured = dialog->grab().save(capture);
                dialog->reject();
            }
        });
        copy->trigger(); QVERIFY(confirmedDestination); QVERIFY(captured); QVERIFY(QDir(fixture.path() + "/target").isEmpty());
    }
    void copyFromActiveRightPanelAndUpdateLeftListing() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        QVERIFY(QDir(fixture.path()).mkdir("left")); QVERIFY(QDir(fixture.path()).mkdir("right"));
        for (const auto &name : {"one.txt", "two.txt"}) {
            QFile file(fixture.path() + "/right/" + name); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(name); file.close();
        }
        MainWindow window(fixture.path() + "/left", fixture.path() + "/right", false); window.show();
        auto *view = window.rightPanel()->findChild<QTreeView *>("files"); view->setFocus(); QTRY_VERIFY(view->hasFocus());
        QTRY_COMPARE(window.rightPanel()->visibleCount(), 2);
        view->selectAll(); QCOMPARE(window.rightPanel()->selectedPaths().size(), 2);
        bool destinationCorrect = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<OperationDialog *>("operationDialog");
            if (dialog) {
                destinationCorrect = QDir::fromNativeSeparators(dialog->findChild<QLineEdit *>("operationDestination")->text()) == fixture.path() + "/left";
                dialog->findChild<QComboBox *>("operationConflict")->setCurrentIndex(1);
                dialog->findChild<QPushButton *>("confirmOperation")->click();
            }
        });
        window.findChild<QAction *>("copyFiles")->trigger(); QVERIFY(destinationCorrect);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(fixture.path() + "/left/one.txt"), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(fixture.path() + "/left/two.txt"), 15000);
        QTRY_COMPARE_WITH_TIMEOUT(window.leftPanel()->visibleCount(), 2, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QMessageBox *>("operationResult") != nullptr, 10000);
        QVERIFY(!window.findChild<QAction *>("cancelOperation")->isEnabled());
        for (const auto &name : {"one.txt", "two.txt"}) {
            QFile source(fixture.path() + "/right/" + name), copied(fixture.path() + "/left/" + name);
            QVERIFY(source.open(QIODevice::ReadOnly)); QVERIFY(copied.open(QIODevice::ReadOnly)); QCOMPARE(copied.readAll(), source.readAll());
        }
    }
    void copyFinishingWhileCloseQuestionIsOpen() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        QVERIFY(QDir(fixture.path()).mkdir("source")); QVERIFY(QDir(fixture.path()).mkdir("target"));
        QFile file(fixture.path() + "/source/one.txt"); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("original"); file.close();
        MainWindow window(fixture.path() + "/source", fixture.path() + "/target", false); window.show();
        QTRY_COMPARE(window.leftPanel()->visibleCount(), 1);
        auto *view = window.leftPanel()->findChild<QTreeView *>("files"); view->selectAll();
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<OperationDialog *>("operationDialog");
            if (dialog) { dialog->findChild<QComboBox *>("operationConflict")->setCurrentIndex(1); dialog->findChild<QPushButton *>("confirmOperation")->click(); }
        });
        window.findChild<QAction *>("copyFiles")->trigger();
        QVERIFY(window.findChild<QAction *>("cancelOperation")->isEnabled());
        bool answeredAfterCompletion = false;
        QTimer answer; answer.setInterval(10);
        connect(&answer, &QTimer::timeout, &window, [&] {
            if (window.findChild<QAction *>("cancelOperation")->isEnabled()) return;
            for (auto *message : window.findChildren<QMessageBox *>()) {
                if (message->text().contains("Cancelar a operação e fechar")) {
                    answeredAfterCompletion = true; answer.stop(); message->button(QMessageBox::Yes)->click(); return;
                }
            }
        });
        answer.start();
        QTimer::singleShot(8000, &window, [&] {
            for (auto *message : window.findChildren<QMessageBox *>()) if (message->button(QMessageBox::No)) message->button(QMessageBox::No)->click();
        });
        window.close(); QVERIFY(answeredAfterCompletion); QVERIFY(!window.isVisible());
        QVERIFY(QFileInfo::exists(fixture.path() + "/target/one.txt")); QVERIFY(QFileInfo::exists(fixture.path() + "/source/one.txt"));
    }
    void moveFromRightPanelUpdatesBothLists() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        QVERIFY(QDir(fixture.path()).mkdir("left")); QVERIFY(QDir(fixture.path()).mkdir("right"));
        writeFile(fixture.path() + "/right/one.txt", "moved content");
        MainWindow window(fixture.path() + "/left", fixture.path() + "/right", false); window.show();
        auto *view = window.rightPanel()->findChild<QTreeView *>("files"); view->setFocus(); QTRY_VERIFY(view->hasFocus());
        QTRY_COMPARE(window.rightPanel()->visibleCount(), 1); view->selectAll();
        confirmForm(window); window.findChild<QAction *>("moveFiles")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QMessageBox *>("operationResult") != nullptr, 10000);
        QTRY_COMPARE(window.rightPanel()->visibleCount(), 0); QTRY_COMPARE(window.leftPanel()->visibleCount(), 1);
        QCOMPARE(contents(fixture.path() + "/left/one.txt"), QByteArray("moved content"));
        QVERIFY(!QFileInfo::exists(fixture.path() + "/right/one.txt"));
    }
    void renameWithF2AndCreateFolderWithShortcut() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); writeFile(fixture.path() + "/one.txt", "preserved");
        MainWindow window(fixture.path(), fixture.path(), false); window.show();
        auto *view = window.leftPanel()->findChild<QTreeView *>("files"); view->setFocus(); QTRY_VERIFY(view->hasFocus());
        QTRY_COMPARE(window.leftPanel()->visibleCount(), 1); view->selectAll();
        confirmForm(window, "ação.txt"); QTest::keyClick(view, Qt::Key_F2);
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QMessageBox *>("operationResult") != nullptr, 10000);
        QCOMPARE(contents(fixture.path() + "/ação.txt"), QByteArray("preserved")); QVERIFY(!QFileInfo::exists(fixture.path() + "/one.txt"));
        window.findChild<QMessageBox *>("operationResult")->close(); QTRY_VERIFY(window.findChild<QMessageBox *>("operationResult") == nullptr);
        window.activateWindow(); view->setFocus(); QTRY_VERIFY(view->hasFocus()); confirmForm(window, "Fotos"); QTest::keyClick(view, Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QMessageBox *>("operationResult") != nullptr, 10000);
        QVERIFY(QFileInfo(fixture.path() + "/Fotos").isDir()); QTRY_COMPARE(window.leftPanel()->visibleCount(), 2);
    }
    void invalidRenameShowsErrorAndAllowsCorrection() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); writeFile(fixture.path() + "/one.txt", "one"); writeFile(fixture.path() + "/two.txt", "two");
        OperationRequest request; request.sources = {fixture.path() + "/one.txt"}; request.kind = OperationKind::Rename; request.name = "one.txt"; request.nativeUi = false;
        OperationDialog dialog(request); dialog.show();
        auto *name = dialog.findChild<QLineEdit *>("operationName"); name->setText("two.txt");
        dialog.findChild<QPushButton *>("confirmOperation")->click();
        QVERIFY(dialog.isVisible()); QVERIFY(!dialog.findChild<QLabel *>("operationError")->text().isEmpty());
        QCOMPARE(contents(fixture.path() + "/one.txt"), QByteArray("one")); QCOMPARE(contents(fixture.path() + "/two.txt"), QByteArray("two"));
        name->setText("new.txt"); dialog.findChild<QPushButton *>("confirmOperation")->click(); QCOMPARE(dialog.result(), int(QDialog::Accepted));
    }
    void deleteConfirmsAndEnterCancelsWithoutWriting() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); writeFile(fixture.path() + "/one.txt", "preserved");
        MainWindow window(fixture.path(), fixture.path(), false); window.show();
        auto *view = window.leftPanel()->findChild<QTreeView *>("files"); view->setFocus(); QTRY_VERIFY(view->hasFocus());
        QTRY_COMPARE(window.leftPanel()->visibleCount(), 1); view->selectAll();
        bool sawConfirmation = false, captured = true;
        const auto capture = qEnvironmentVariable("NAVEGA_CAPTURE_TRASH_DIALOG");
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<OperationDialog *>("operationDialog");
            if (dialog) {
                sawConfirmation = dialog->windowTitle() == "Enviar à Lixeira";
                if (!capture.isEmpty()) captured = dialog->grab().save(capture);
                QTest::keyClick(dialog, Qt::Key_Return);
            }
        });
        QTest::keyClick(view, Qt::Key_Delete); QVERIFY(sawConfirmation); QVERIFY(captured);
        QCOMPARE(contents(fixture.path() + "/one.txt"), QByteArray("preserved")); QVERIFY(window.findChild<QMessageBox *>("operationResult") == nullptr);
    }
    void deleteWhileEditingAddressDoesNotOpenTrash() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); writeFile(fixture.path() + "/one.txt", "preserved");
        MainWindow window(fixture.path(), fixture.path(), false); window.show(); QTRY_COMPARE(window.leftPanel()->visibleCount(), 1);
        auto *view = window.leftPanel()->findChild<QTreeView *>("files"); view->selectAll();
        auto *address = window.leftPanel()->findChild<QLineEdit *>("address"); address->setFocus(); QTRY_VERIFY(address->hasFocus());
        address->setText("abcd"); address->setCursorPosition(1); QTest::keyClick(address, Qt::Key_Delete);
        QCOMPARE(address->text(), QString("acd")); QVERIFY(window.findChild<OperationDialog *>("operationDialog") == nullptr);
        QCOMPARE(contents(fixture.path() + "/one.txt"), QByteArray("preserved"));
    }
    void mutationActionsFollowSelectionSize() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); writeFile(fixture.path() + "/one.txt", "one"); writeFile(fixture.path() + "/two.txt", "two");
        MainWindow window(fixture.path(), fixture.path(), false); window.show(); QTRY_COMPARE(window.leftPanel()->visibleCount(), 2);
        QVERIFY(!window.findChild<QAction *>("renameFile")->isEnabled()); QVERIFY(window.findChild<QAction *>("createFolder")->isEnabled());
        auto *view = window.leftPanel()->findChild<QTreeView *>("files");
        view->selectionModel()->select(view->model()->index(0, 0, view->rootIndex()), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        QVERIFY(window.findChild<QAction *>("renameFile")->isEnabled()); view->selectAll();
        QVERIFY(!window.findChild<QAction *>("renameFile")->isEnabled()); QVERIFY(window.findChild<QAction *>("moveFiles")->isEnabled()); QVERIFY(window.findChild<QAction *>("trashFiles")->isEnabled());
    }
};
QTEST_MAIN(NavigationTest)
#include "navigation_test.moc"
