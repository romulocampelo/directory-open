#include "mainwindow.h"
#include "filepanel.h"
#include "sessionstore.h"
#include "operationdialog.h"
#include <QAction>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QScreen>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <QApplication>

class SessionTest : public QObject {
    Q_OBJECT
private:
    void write(const QString &path, const QByteArray &bytes) { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), qint64(bytes.size())); }
    QByteArray read(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
    WindowSession state(const QString &path) { WindowSession s; s.left.tabs = {{path, {}}}; s.right.tabs = {{path, {}}}; return s; }
    void openContext(QTreeView *view, const QPoint &point, bool keyboard = false) {
        QContextMenuEvent event(keyboard ? QContextMenuEvent::Keyboard : QContextMenuEvent::Mouse, point, view->viewport()->mapToGlobal(point));
        QApplication::sendEvent(keyboard ? static_cast<QWidget *>(view) : view->viewport(), &event);
    }
private slots:
    void closeAndReopenRestoresTabsFiltersThemeLayoutAndPosition() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto file = fixture.path() + "/config/session.json";
        for (const auto &name : {"left", "right", "other"}) QVERIFY(QDir(fixture.path()).mkdir(name));
        WindowSession expected; QSize size; QPoint position; QList<int> widths;
        {
            MainWindow window(fixture.path() + "/left", fixture.path() + "/right", false, file); window.show();
            window.leftPanel()->setFilter("texto"); window.leftPanel()->newTab(fixture.path() + "/other"); window.leftPanel()->setFilter("foto");
            auto *leftView = window.leftPanel()->findChild<QTreeView *>("files"); leftView->sortByColumn(1, Qt::DescendingOrder);
            window.rightPanel()->newTab(fixture.path()); window.rightPanel()->setFilter("projeto");
            window.findChild<QAction *>("darkTheme")->setChecked(true); window.findChild<QAction *>("treeVisible")->setChecked(false);
            window.findChild<QDockWidget *>("previewDock")->show(); window.resize(1180, 720); window.move(55, 70);
            window.findChild<QSplitter *>("fileSplit")->setSizes({0, 460, 580});
            auto *rightView = window.rightPanel()->findChild<QTreeView *>("files"); rightView->setFocus(); QTRY_VERIFY(rightView->hasFocus());
            QCoreApplication::processEvents(); expected = window.session(); size = window.size(); position = window.pos(); widths = window.findChild<QSplitter *>("fileSplit")->sizes();
            QVERIFY(window.close()); QVERIFY(QFileInfo::exists(file));
        }
        MainWindow reopened({}, {}, false, file); reopened.show(); QCoreApplication::processEvents();
        const auto actual = reopened.session();
        QCOMPARE(reopened.leftPanel()->tabCount(), 2); QCOMPARE(reopened.rightPanel()->tabCount(), 2);
        QCOMPARE(reopened.leftPanel()->path(), fixture.path() + "/other"); QCOMPARE(reopened.rightPanel()->path(), fixture.path());
        QCOMPARE(actual.left.tabs[0].filter, expected.left.tabs[0].filter); QCOMPARE(actual.left.tabs[1].filter, expected.left.tabs[1].filter);
        QCOMPARE(actual.right.tabs[1].filter, expected.right.tabs[1].filter); QCOMPARE(actual.left.sortColumn, 1); QVERIFY(actual.left.descending);
        QVERIFY(actual.dark); QVERIFY(actual.dualPane); QVERIFY(!actual.treeVisible); QVERIFY(actual.previewVisible); QCOMPARE(actual.activePanel, 1);
        const auto available = reopened.screen()->availableGeometry();
        if (available.contains(QRect(position, size))) { QCOMPARE(reopened.size(), size); QVERIFY((reopened.pos() - position).manhattanLength() <= 4); }
        else { // restoreGeometry intentionally keeps the window on a smaller screen.
            const auto bounded = size.boundedTo(available.size()).expandedTo(reopened.minimumSize());
            QVERIFY(qAbs(reopened.width() - bounded.width()) <= 4); QVERIFY(qAbs(reopened.height() - bounded.height()) <= 4);
            QVERIFY(available.intersects(reopened.frameGeometry()));
        }
        const auto actualWidths = reopened.findChild<QSplitter *>("fileSplit")->sizes();
        QCOMPARE(actualWidths.size(), widths.size());
        qint64 oldTotal = 0, newTotal = 0; for (const auto width : widths) oldTotal += width; for (const auto width : actualWidths) newTotal += width;
        QVERIFY(oldTotal > 0 && newTotal > 0);
        // On a smaller screen the panels' minimum widths can constrain the ratio.
        if (reopened.size() == size) for (int i = 0; i < widths.size(); ++i)
            QVERIFY(qAbs(qint64(actualWidths[i]) * oldTotal - qint64(widths[i]) * newTotal) <= oldTotal * newTotal / 50);
        else { QCOMPARE(actualWidths[0], 0); QVERIFY(actualWidths[1] > 0); QVERIFY(actualWidths[2] > 0); }
        QVERIFY(reopened.close());
    }
    void singlePanelAndSelectedTabSurviveRestart() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); QVERIFY(QDir(fixture.path()).mkdir("other")); const auto file = fixture.path() + "/session.json";
        {
            MainWindow window(fixture.path(), fixture.path(), false, file); window.show(); window.leftPanel()->newTab(fixture.path() + "/other");
            window.leftPanel()->findChild<QTabBar *>("folderTabs")->setCurrentIndex(0); window.findChild<QAction *>("dualPane")->setChecked(false);
            QVERIFY(window.close());
        }
        MainWindow reopened({}, {}, false, file); reopened.show(); QCOMPARE(reopened.leftPanel()->path(), fixture.path());
        QCOMPARE(reopened.leftPanel()->tabCount(), 2); QVERIFY(!reopened.session().dualPane); QCOMPARE(reopened.session().activePanel, 0); QVERIFY(reopened.close());
    }
    void explicitStartupPathOverridesOnlyThatPanel() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); QVERIFY(QDir(fixture.path()).mkdir("override")); const auto file = fixture.path() + "/session.json";
        SessionStore store(file); auto saved = state(fixture.path()); QVERIFY(store.save(saved).isEmpty());
        MainWindow window(fixture.path() + "/override", {}, false, file); QCOMPARE(window.leftPanel()->path(), fixture.path() + "/override"); QCOMPARE(window.rightPanel()->path(), fixture.path());
    }
    void missingFolderUsesHomeAndKeepsOriginalReference() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto missing = fixture.path() + "/unavailable";
        auto saved = state(fixture.path()); saved.left.tabs.append({missing, "foto"}); saved.left.currentTab = 1;
        FilePanel panel; QCOMPARE(panel.restoreSession(saved.left), 1); QCOMPARE(panel.path(), QFileInfo(QDir::homePath()).canonicalFilePath());
        QCOMPARE(panel.session().tabs[1].path, missing); QCOMPARE(panel.session().tabs[1].filter, QString("foto"));
        QVERIFY(panel.navigate(fixture.path())); QCOMPARE(panel.session().tabs[1].path, fixture.path());
    }
    void previousRecordBackedUpAndCorruptPrimaryRecovered() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto file = fixture.path() + "/session.json";
        SessionStore store(file); auto first = state(fixture.path()); QVERIFY(store.save(first).isEmpty()); const auto firstBytes = read(file);
        auto second = first; second.dark = true; QVERIFY(store.save(second).isEmpty()); QCOMPARE(read(file + ".bak"), firstBytes);
        write(file, "{incomplete"); SessionStore recovery(file); const auto loaded = recovery.load(); QCOMPARE(loaded.state, SessionLoadState::Recovered); QVERIFY(!loaded.session.dark);
        QVERIFY(recovery.save(loaded.session).isEmpty()); QCOMPARE(read(file + ".bak"), firstBytes);
        const auto preserved = QDir(fixture.path()).entryList({"session.json.preserved-*.json"}, QDir::Files); QCOMPARE(preserved.size(), 1);
        QCOMPARE(read(fixture.path() + '/' + preserved.first()), QByteArray("{incomplete"));
    }
    void unsupportedVersionIsNotOverwritten() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto file = fixture.path() + "/session.json"; const QByteArray bytes("{\"version\":9,\"future\":true}"); write(file, bytes);
        SessionStore store(file); QCOMPARE(store.load().state, SessionLoadState::Unsupported); QVERIFY(!store.writable()); QVERIFY(!store.save(state(fixture.path())).isEmpty()); QCOMPARE(read(file), bytes);
    }
    void malformedRecordPreservedAndConcurrentWriterRefused() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto file = fixture.path() + "/session.json"; write(file, "broken");
        SessionStore first(file); QCOMPARE(first.load().state, SessionLoadState::Invalid); QVERIFY(first.save(state(fixture.path())).isEmpty());
        QCOMPARE(QDir(fixture.path()).entryList({"*.preserved-*.json"}, QDir::Files).size(), 1);
        SessionStore other(file); QCOMPARE(other.load().state, SessionLoadState::Loaded);
        auto changed = state(fixture.path()); changed.dark = true; QVERIFY(first.save(changed).isEmpty()); const auto bytes = read(file);
        QVERIFY(!other.save(state(fixture.path())).isEmpty()); QCOMPARE(read(file), bytes);
    }
    void unknownFieldsPreservedAndIdenticalSaveDoesNotRotateBackup() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto file = fixture.path() + "/session.json";
        SessionStore original(file); QVERIFY(original.save(state(fixture.path())).isEmpty());
        auto doc = QJsonDocument::fromJson(read(file)).object(); doc["userNote"] = "preserved"; write(file, QJsonDocument(doc).toJson());
        SessionStore store(file); auto loaded = store.load(); QVERIFY(store.save(loaded.session).isEmpty()); QVERIFY(!QFileInfo::exists(file + ".bak"));
        loaded.session.dark = true; QVERIFY(store.save(loaded.session).isEmpty()); const auto backup = read(file + ".bak");
        QCOMPARE(QJsonDocument::fromJson(read(file)).object()["userNote"].toString(), QString("preserved"));
        QVERIFY(store.save(loaded.session).isEmpty()); QCOMPARE(read(file + ".bak"), backup);
    }
    void failedBackupDoesNotReplacePrimaryRecord() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto file = fixture.path() + "/session.json";
        SessionStore store(file); QVERIFY(store.save(state(fixture.path())).isEmpty()); const auto bytes = read(file);
        QVERIFY(QDir().mkdir(file + ".bak")); auto changed = state(fixture.path()); changed.dark = true;
        QVERIFY(!store.save(changed).isEmpty()); QCOMPARE(read(file), bytes);
    }
    void invalidFieldTypesAndRelativePathsAreNotRestored() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto validFile = fixture.path() + "/valid.json";
        SessionStore valid(validFile); QVERIFY(valid.save(state(fixture.path())).isEmpty()); const auto base = QJsonDocument::fromJson(read(validFile)).object();
        QVector<QJsonObject> cases;
        auto changed = base; changed["dark"] = "true"; cases.append(changed);
        changed = base; changed["activePanel"] = 1.5; cases.append(changed);
        changed = base; changed["geometry"] = "!invalid-base64"; cases.append(changed);
        changed = base; auto panel = changed["left"].toObject(); panel["currentTab"] = 9; changed["left"] = panel; cases.append(changed);
        changed = base; panel = changed["left"].toObject(); panel["tabs"] = QJsonArray{QJsonObject{{"path", "relative/folder"}, {"filter", ""}}}; changed["left"] = panel; cases.append(changed);
        for (int i = 0; i < cases.size(); ++i) {
            const auto file = fixture.path() + QString("/invalid-%1.json").arg(i); const auto bytes = QJsonDocument(cases[i]).toJson(); write(file, bytes);
            SessionStore invalid(file); QCOMPARE(invalid.load().state, SessionLoadState::Invalid); QCOMPARE(read(file), bytes);
        }
    }
    void oversizedOrNonFileRecordIsPreservedWithoutWriting() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto file = fixture.path() + "/huge.json"; const QByteArray bytes(1024 * 1024 + 1, 'x'); write(file, bytes);
        SessionStore huge(file); QCOMPARE(huge.load().state, SessionLoadState::Invalid); QVERIFY(!huge.writable()); QVERIFY(!huge.save(state(fixture.path())).isEmpty()); QCOMPARE(read(file), bytes);
        const auto folder = fixture.path() + "/not-a-file.json"; QVERIFY(QDir().mkdir(folder));
        SessionStore directory(folder); QCOMPARE(directory.load().state, SessionLoadState::Invalid); QVERIFY(!directory.writable()); QVERIFY(QFileInfo(folder).isDir());
    }
    void contextOnSelectedRowKeepsBatchAndUsesRightPanel() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); QVERIFY(QDir(fixture.path()).mkdir("left")); QVERIFY(QDir(fixture.path()).mkdir("right"));
        write(fixture.path() + "/right/a.txt", "a"); write(fixture.path() + "/right/b.txt", "b");
        MainWindow window(fixture.path() + "/left", fixture.path() + "/right", false); window.show(); QTRY_COMPARE(window.rightPanel()->visibleCount(), 2);
        auto *view = window.rightPanel()->findChild<QTreeView *>("files"); view->selectAll(); bool checked = false;
        const auto capture = qEnvironmentVariable("NAVEGA_CAPTURE_CONTEXT_MENU"); bool captured = true;
        QTimer::singleShot(0, &window, [&] {
            auto *menu = window.findChild<QMenu *>("fileContextMenu");
            if (menu) {
                checked = window.session().activePanel == 1 && window.rightPanel()->selectedPaths().size() == 2 && window.findChild<QAction *>("moveFiles")->isEnabled() && !window.findChild<QAction *>("renameFile")->isEnabled();
                if (!capture.isEmpty()) captured = menu->grab().save(capture);
                window.findChild<QAction *>("copyPath")->trigger(); menu->close();
            }
        });
        openContext(view, view->visualRect(view->model()->index(0, 0, view->rootIndex())).center()); QVERIFY(checked); QVERIFY(captured);
        const auto paths = QApplication::clipboard()->text().split('\n'); QCOMPARE(paths.size(), 2);
        QVERIFY(paths.contains(QDir::toNativeSeparators(fixture.path() + "/right/a.txt"))); QVERIFY(paths.contains(QDir::toNativeSeparators(fixture.path() + "/right/b.txt")));
    }
    void contextOnOtherRowRetargetsAndTrashStillRequiresConfirmation() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); write(fixture.path() + "/a.txt", "a"); write(fixture.path() + "/b.txt", "b");
        MainWindow window(fixture.path(), fixture.path(), false); window.show(); QTRY_COMPARE(window.leftPanel()->visibleCount(), 2);
        auto *view = window.leftPanel()->findChild<QTreeView *>("files");
        view->selectionModel()->select(view->model()->index(0, 0, view->rootIndex()), QItemSelectionModel::Select | QItemSelectionModel::Rows); bool sawDialog = false;
        QTimer::singleShot(0, &window, [&] {
            auto *menu = window.findChild<QMenu *>("fileContextMenu"); if (!menu) return;
            QTimer::singleShot(0, &window, [&] { auto *dialog = window.findChild<OperationDialog *>("operationDialog"); if (dialog) { sawDialog = dialog->request().sources == QStringList{fixture.path() + "/b.txt"}; dialog->reject(); } });
            window.findChild<QAction *>("trashFiles")->trigger(); menu->close();
        });
        openContext(view, view->visualRect(view->model()->index(1, 0, view->rootIndex())).center()); QVERIFY(sawDialog);
        QCOMPARE(read(fixture.path() + "/a.txt"), QByteArray("a")); QCOMPARE(read(fixture.path() + "/b.txt"), QByteArray("b"));
    }
    void emptyContextClearsSelectionAndKeyboardContextKeepsIt() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); write(fixture.path() + "/a.txt", "a");
        MainWindow window(fixture.path(), fixture.path(), false); window.show(); QTRY_COMPARE(window.leftPanel()->visibleCount(), 1);
        auto *view = window.leftPanel()->findChild<QTreeView *>("files"); view->selectAll(); bool empty = false, keyboard = false;
        QTimer::singleShot(0, &window, [&] { auto *menu = window.findChild<QMenu *>("fileContextMenu"); if (menu) { empty = window.leftPanel()->selectedPaths().isEmpty() && !window.findChild<QAction *>("trashFiles")->isEnabled() && window.findChild<QAction *>("createFolder")->isEnabled(); menu->close(); } });
        openContext(view, QPoint(10, view->viewport()->height() - 10)); QVERIFY(empty);
        view->selectAll(); QTimer::singleShot(0, &window, [&] { auto *menu = window.findChild<QMenu *>("fileContextMenu"); if (menu) { keyboard = window.leftPanel()->selectedPaths().size() == 1; menu->close(); } });
        openContext(view, {}, true); QVERIFY(keyboard);
    }
    void contextOpensFolderInNewTab() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); QVERIFY(QDir(fixture.path()).mkdir("folder"));
        MainWindow window(fixture.path(), fixture.path(), false); window.show(); QTRY_COMPARE(window.leftPanel()->visibleCount(), 1);
        auto *view = window.leftPanel()->findChild<QTreeView *>("files");
        QTimer::singleShot(0, &window, [&] { auto *menu = window.findChild<QMenu *>("fileContextMenu"); if (menu) { window.findChild<QAction *>("openFolderInTab")->trigger(); menu->close(); } });
        openContext(view, view->visualRect(view->model()->index(0, 0, view->rootIndex())).center()); QCOMPARE(window.leftPanel()->tabCount(), 2); QCOMPARE(window.leftPanel()->path(), fixture.path() + "/folder");
    }
};
QTEST_MAIN(SessionTest)
#include "session_test.moc"
