#include "mainwindow.h"
#include "filepanel.h"
#include "folderviewsdialog.h"
#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QJsonDocument>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>

class FolderViewTest : public QObject {
    Q_OBJECT
private:
    void write(const QString &path, const QByteArray &bytes) { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), qint64(bytes.size())); }
    QByteArray read(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
    QAction *action(FilePanel *panel, const char *name) { return panel->findChild<QAction *>(name); }
    QTreeView *view(FilePanel *panel) { return panel->findChild<QTreeView *>("files"); }
    WindowSession state(const QString &path) { WindowSession s; s.left.tabs = {{path, {}}}; s.right.tabs = {{path, {}}}; return s; }
private slots:
    void saveMenuSharesColumnsSortAndReopensWithoutTouchingFiles() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto file = fixture.path() + "/session.json";
        QVERIFY(QDir(fixture.path()).mkdir("files")); const auto folder = fixture.path() + "/files";
        write(folder + "/small.txt", "small"); write(folder + "/large.txt", QByteArray(200, 'x'));
        {
            MainWindow window(folder, folder, false, file); window.show();
            auto *left = window.leftPanel(); auto *right = window.rightPanel();
            QTRY_COMPARE(left->visibleCount(), 2); QTRY_COMPARE(right->visibleCount(), 2);
            view(left)->selectAll(); QCOMPARE(left->selectedPaths().size(), 2);
            view(left)->header()->moveSection(3, 1); view(left)->setColumnWidth(1, 133);
            action(left, "viewColumn2")->setChecked(true); action(left, "viewColumn2")->trigger();
            action(left, "viewSort1")->trigger(); action(left, "viewDescending")->trigger();
            action(left, "rememberFolderView")->trigger();
            QCOMPARE(window.session().folderViews->size(), 1);
            QVERIFY(view(right)->isColumnHidden(2)); QCOMPARE(view(right)->header()->logicalIndex(1), 3);
            QCOMPARE(view(right)->columnWidth(1), 133); QCOMPARE(view(right)->header()->sortIndicatorSection(), 1);
            QCOMPARE(view(right)->header()->sortIndicatorOrder(), Qt::DescendingOrder);
            QTRY_COMPARE(view(right)->model()->index(0, 0, view(right)->rootIndex()).data().toString(), QString("large.txt"));
            QCOMPARE(left->selectedPaths().size(), 2); QVERIFY(window.close());
        }
        MainWindow reopened({}, {}, false, file); reopened.show();
        for (auto *panel : {reopened.leftPanel(), reopened.rightPanel()}) {
            QVERIFY(view(panel)->isColumnHidden(2)); QCOMPARE(view(panel)->header()->logicalIndex(1), 3);
            QCOMPARE(view(panel)->columnWidth(1), 133); QCOMPARE(view(panel)->header()->sortIndicatorSection(), 1);
            QCOMPARE(view(panel)->header()->sortIndicatorOrder(), Qt::DescendingOrder);
        }
        QCOMPARE(read(folder + "/small.txt"), QByteArray("small")); QCOMPARE(read(folder + "/large.txt"), QByteArray(200, 'x'));
        QVERIFY(reopened.leftPanel()->findChild<QFileSystemModel *>()->isReadOnly()); QVERIFY(reopened.close());
    }
    void namesModeSurvivesTabsHistoryAndDoesNotLeakToOtherFolders() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); QVERIFY(QDir(fixture.path()).mkdir("other"));
        MainWindow window(fixture.path(), fixture.path(), false); window.show(); auto *panel = window.leftPanel();
        view(panel)->setColumnWidth(1, 177); action(panel, "viewNames")->trigger(); action(panel, "rememberFolderView")->trigger();
        QCOMPARE(window.session().folderViews->first().view.widths[0], 177);
        QVERIFY(panel->navigate(fixture.path() + "/other"));
        for (int i = 1; i < 4; ++i) QVERIFY(!view(panel)->isColumnHidden(i));
        QCOMPARE(view(panel)->header()->sortIndicatorSection(), 0);
        panel->back(); for (int i = 1; i < 4; ++i) QVERIFY(view(panel)->isColumnHidden(i));
        panel->newTab(fixture.path() + "/other"); QVERIFY(!view(panel)->isColumnHidden(1));
        panel->findChild<QTabBar *>("folderTabs")->setCurrentIndex(0); QVERIFY(view(panel)->isColumnHidden(1));
        action(panel, "viewDetails")->trigger(); QCOMPARE(view(panel)->columnWidth(1), 177);
        // Unsaved edits in a preset stay while refreshing, but revisiting uses the saved view.
        panel->refresh(); QVERIFY(!view(panel)->isColumnHidden(1)); panel->forward(); panel->back(); QVERIFY(view(panel)->isColumnHidden(1));
        action(panel, "forgetFolderView")->trigger(); QVERIFY(window.session().folderViews->isEmpty());
        QVERIFY(!view(window.rightPanel())->isColumnHidden(1)); QVERIFY(!view(panel)->isColumnHidden(1));
    }
    void enteringPresetPreservesLastUnsavedPanelChoices() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); QVERIFY(QDir(fixture.path()).mkdir("saved"));
        FolderViewCatalog catalog; FolderView names; names.visible = {false, false, false};
        QVERIFY(catalog.remember(fixture.path() + "/saved", names).isEmpty());
        FilePanel panel(nullptr, &catalog); panel.show(); QVERIFY(panel.navigate(fixture.path()));
        view(&panel)->setColumnWidth(1, 166); view(&panel)->sortByColumn(1, Qt::DescendingOrder);
        QVERIFY(panel.navigate(fixture.path() + "/saved")); QVERIFY(view(&panel)->isColumnHidden(1));
        QVERIFY(panel.navigate(fixture.path())); QVERIFY(!view(&panel)->isColumnHidden(1));
        QCOMPARE(view(&panel)->columnWidth(1), 166); QCOMPARE(view(&panel)->header()->sortIndicatorSection(), 1);
        QCOMPARE(view(&panel)->header()->sortIndicatorOrder(), Qt::DescendingOrder);
    }
    void menuRoutesToRightPanelAndShowsDerivedMode() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); MainWindow window(fixture.path(), fixture.path(), false); window.show();
        auto *panel = window.rightPanel(); auto *menu = panel->findChild<QMenu *>("folderViewMenu");
        bool checked = false; QTimer::singleShot(0, menu, [&] {
            checked = window.session().activePanel == 1 && action(panel, "viewDetails")->isChecked() && !action(panel, "viewNames")->isChecked(); menu->close();
        });
        menu->exec(panel->mapToGlobal(QPoint(20, 20))); QVERIFY(checked);
        action(panel, "viewNames")->trigger(); bool names = false;
        QTimer::singleShot(0, menu, [&] { names = action(panel, "viewNames")->isChecked() && !action(panel, "viewDetails")->isChecked(); menu->close(); });
        menu->exec(panel->mapToGlobal(QPoint(20, 20))); QVERIFY(names);
    }
    void missingReferencesStaySavedWithoutApplyingToFallback() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto file = fixture.path() + "/session.json"; const auto missing = fixture.path() + "/unavailable";
        auto saved = state(fixture.path()); saved.left.tabs = {{missing, {}}}; FolderView names; names.visible = {false, false, false};
        saved.folderViews = QVector<FolderViewEntry>{{missing, names}, {QFileInfo(QDir::homePath()).canonicalFilePath(), names}};
        SessionStore store(file); QVERIFY(store.save(saved).isEmpty());
        MainWindow window({}, {}, false, file); window.show();
        QVERIFY(!view(window.leftPanel())->isColumnHidden(1)); QCOMPARE(window.session().folderViews->first().path, missing);
        action(window.leftPanel(), "rememberFolderView")->trigger(); QCOMPARE(window.session().folderViews->size(), 2);
        QVERIFY(window.close()); SessionStore after(file); QCOMPARE(after.load().session.folderViews->first().path, missing);
    }
    void organizerCancellationAndExplicitRemovalPreserveUnavailableEntries() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); MainWindow window(fixture.path(), fixture.path(), false); window.show();
        action(window.leftPanel(), "rememberFolderView")->trigger();
        bool sawDialog = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<FolderViewsDialog *>("folderViewsDialog"); if (!dialog) return; sawDialog = true;
            dialog->findChild<QListWidget *>("savedFolderViews")->selectAll(); dialog->findChild<QPushButton *>("removeFolderViews")->click(); dialog->reject();
        });
        action(window.leftPanel(), "manageFolderViews")->trigger(); QVERIFY(sawDialog); QCOMPARE(window.session().folderViews->size(), 1);
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<FolderViewsDialog *>("folderViewsDialog"); if (!dialog) return;
            dialog->findChild<QListWidget *>("savedFolderViews")->selectAll(); dialog->findChild<QPushButton *>("removeFolderViews")->click(); dialog->findChild<QPushButton *>("saveFolderViews")->click();
        });
        action(window.leftPanel(), "manageFolderViews")->trigger(); QVERIFY(window.session().folderViews->isEmpty());
    }
    void legacyOmissionEmptyListBackupAndConcurrency() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto file = fixture.path() + "/session.json";
        SessionStore original(file); QVERIFY(original.save(state(fixture.path())).isEmpty()); const auto legacy = read(file);
        SessionStore first(file), second(file); auto loaded = first.load(); QVERIFY(!loaded.session.folderViews); second.load();
        loaded.session.folderViews = QVector<FolderViewEntry>{{fixture.path() + "/missing", FolderView{}}};
        QVERIFY(first.save(loaded.session).isEmpty()); QCOMPARE(read(file + ".bak"), legacy);
        QVERIFY(!second.save(state(fixture.path())).isEmpty()); const auto saved = read(file);
        QVERIFY(first.save(loaded.session).isEmpty()); QCOMPARE(read(file), saved); QCOMPARE(read(file + ".bak"), legacy);
        loaded.session.folderViews = QVector<FolderViewEntry>{}; QVERIFY(first.save(loaded.session).isEmpty());
        SessionStore empty(file); const auto result = empty.load(); QVERIFY(result.session.folderViews); QVERIFY(result.session.folderViews->isEmpty());
    }
    void malformedPresetsFailClosedAndPreserveRecord() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto file = fixture.path() + "/base.json";
        auto saved = state(fixture.path()); saved.folderViews = QVector<FolderViewEntry>{{fixture.path(), FolderView{}}};
        SessionStore base(file); QVERIFY(base.save(saved).isEmpty()); const auto document = QJsonDocument::fromJson(read(file)).object();
        const auto validEntry = document["folderViews"].toArray().first().toObject(); QVector<QJsonValue> cases;
        cases.append("invalid"); cases.append(QJsonArray{true});
        for (const auto &key : {"path", "sortColumn", "descending", "order", "widths", "visible"}) { auto entry = validEntry; entry[key] = QJsonValue::Null; cases.append(QJsonArray{entry}); }
        auto entry = validEntry; entry["path"] = "relative"; cases.append(QJsonArray{entry});
        entry = validEntry; entry["sortColumn"] = 1.5; cases.append(QJsonArray{entry});
        entry = validEntry; entry["order"] = QJsonArray{1, 1, 3}; cases.append(QJsonArray{entry});
        entry = validEntry; entry["widths"] = QJsonArray{90, 0, 145}; cases.append(QJsonArray{entry});
        entry = validEntry; entry["widths"] = QJsonArray{90, 8193, 145}; cases.append(QJsonArray{entry});
        entry = validEntry; entry["visible"] = QJsonArray{true, true}; cases.append(QJsonArray{entry});
        cases.append(QJsonArray{validEntry, validEntry});
        for (int i = 0; i < cases.size(); ++i) {
            auto changed = document; changed["folderViews"] = cases[i]; const auto bytes = QJsonDocument(changed).toJson();
            const auto path = fixture.path() + QString("/invalid-%1.json").arg(i); write(path, bytes);
            SessionStore store(path); QCOMPARE(store.load().state, SessionLoadState::Invalid); QCOMPARE(read(path), bytes);
        }
    }
    void catalogBoundsReplacementAndPathIdentity() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); FolderViewCatalog catalog; QVector<FolderViewEntry> entries;
        for (int i = 0; i < 256; ++i) entries.append({fixture.path() + QString("/folder-%1").arg(i), FolderView{}});
        QVERIFY(catalog.replace(entries).isEmpty()); QVERIFY(!catalog.remember(fixture.path() + "/extra", FolderView{}).isEmpty()); QCOMPARE(catalog.entries().size(), 256);
        FolderView names; names.visible = {false, false, false}; QVERIFY(catalog.remember(entries.first().path, names).isEmpty());
        QCOMPARE(catalog.find(entries.first().path)->visible, names.visible);
        catalog.forget(entries.last().path); QVERIFY(catalog.remember(fixture.path() + "/extra", names).isEmpty());
        auto bad = names; bad.order = {3, 3, 1}; QVERIFY(!catalog.remember(entries.first().path, bad).isEmpty()); QCOMPARE(catalog.find(entries.first().path)->order, names.order);
        FolderViewCatalog identity; QVERIFY(identity.remember(fixture.path() + "/Case", names).isEmpty());
        QVERIFY(identity.remember(fixture.path() + "/case", FolderView{}).isEmpty()); QCOMPARE(identity.entries().size(), 2);
        auto duplicate = identity.entries(); duplicate.append({fixture.path() + "/Case/../Case", names}); QVERIFY(!identity.replace(duplicate).isEmpty()); QCOMPARE(identity.entries().size(), 2);
    }
    void captureNativeViewMenu() {
        const auto capture = qEnvironmentVariable("NAVEGA_VIEW_CAPTURE");
        if (capture.isEmpty()) QSKIP("Captura opcional da janela nativa.");
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); write(fixture.path() + "/Relatório.txt", "exemplo");
        MainWindow window(fixture.path(), fixture.path(), false); window.resize(1400, 820); window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_COMPARE(window.leftPanel()->visibleCount(), 1); QTRY_COMPARE(window.rightPanel()->visibleCount(), 1);
        auto *panel = window.leftPanel(); action(panel, "viewNames")->trigger(); action(panel, "rememberFolderView")->trigger();
        auto *menu = panel->findChild<QMenu *>("folderViewMenu"); bool captured = false;
        QTimer::singleShot(300, menu, [&] { captured = window.grab().save(capture) && menu->grab().save(capture + ".menu.png"); menu->close(); });
        menu->exec(panel->mapToGlobal(QPoint(300, 30))); QVERIFY(captured);
    }
};
QTEST_MAIN(FolderViewTest)
#include "folder_view_test.moc"
