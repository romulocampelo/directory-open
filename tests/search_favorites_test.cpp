#include "filesearch.h"
#include "searchdialog.h"
#include "favoritesdialog.h"
#include "mainwindow.h"
#include "filepanel.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <QUuid>
#include <windows.h>
#include <aclapi.h>

class SearchFavoritesTest final : public QObject {
    Q_OBJECT
private:
    void write(const QString &path, QByteArray content = "conteúdo preservado") {
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(content), qint64(content.size()));
    }
    FavoriteEntry favorite(const QString &name, const QString &path) { return {QUuid::createUuid().toString(QUuid::WithoutBraces), name, path}; }
    WindowSession session(const QString &path) { WindowSession state; state.left.tabs = {{path, {}}}; state.right.tabs = {{path, {}}}; return state; }
    QVector<SearchEntry> find(const SearchRequest &request, SearchSummary &summary) {
        std::atomic_bool cancel{false}; QVector<SearchEntry> entries;
        summary = searchFiles(request, cancel, [&](const QVector<SearchEntry> &batch, const SearchSummary &) { entries += batch; }); return entries;
    }
    QStringList paths(const QVector<SearchEntry> &entries) { QStringList result; for (const auto &entry : entries) result.append(entry.path); result.sort(); return result; }
private slots:
    void literalSearchIncludesUnicodeHiddenFilesAndFoldersWithoutChangingContents() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); QVERIFY(QDir(fixture.path()).mkpath("Férias/sub"));
        write(fixture.path() + "/Férias/sub/FÉRIAS.txt"); write(fixture.path() + "/.férias-ocultas"); write(fixture.path() + "/outro.txt");
        const auto hidden = QDir::toNativeSeparators(fixture.path() + "/.férias-ocultas").toStdWString();
        QVERIFY(SetFileAttributesW(hidden.c_str(), FILE_ATTRIBUTE_HIDDEN));
        SearchSummary summary; const auto entries = find({fixture.path(), "férias"}, summary);
        QCOMPARE(summary.state, SearchState::Completed); QCOMPARE(summary.matches, quint64(3)); QCOMPARE(summary.visited, quint64(5));
        QVERIFY(paths(entries).contains(fixture.path() + "/.férias-ocultas"));
        QFile file(fixture.path() + "/Férias/sub/FÉRIAS.txt"); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("conteúdo preservado"));
    }
    void wildcardIsExplicitCaseInsensitiveAndMatchesWholeName() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); write(fixture.path() + "/a.PDF"); write(fixture.path() + "/b.pdf.bak"); write(fixture.path() + "/literal[1].txt");
        SearchSummary summary; const auto entries = find({fixture.path(), "*.pdf", true}, summary);
        QCOMPARE(summary.state, SearchState::Completed); QCOMPARE(paths(entries), QStringList{fixture.path() + "/a.PDF"});
        QCOMPARE(paths(find({fixture.path(), "[1]"}, summary)), QStringList{fixture.path() + "/literal[1].txt"});
    }
    void namesBeyondLegacyWindowsPathLengthRemainSearchable() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); QString deep = fixture.path();
        for (int i = 0; i < 8; ++i) deep += "/pasta-com-nome-longo-para-arquivo";
        QVERIFY(deep.size() > 260); QVERIFY(QDir().mkpath(deep)); write(deep + "/relatório.txt");
        SearchSummary summary; const auto entries = find({fixture.path(), "relatório"}, summary);
        QCOMPARE(summary.state, SearchState::Completed); QCOMPARE(paths(entries), QStringList{deep + "/relatório.txt"});
    }
    void unavailableRootAndEmptyNameAreFailuresNotEmptySuccesses() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); SearchSummary summary;
        QVERIFY(find({fixture.path() + "/missing", "x"}, summary).isEmpty()); QCOMPARE(summary.state, SearchState::Failed); QCOMPARE(summary.errors, quint64(1));
        QVERIFY(find({fixture.path(), " "}, summary).isEmpty()); QCOMPARE(summary.state, SearchState::Failed); QVERIFY(!summary.details.isEmpty());
    }
    void resultAndTraversalLimitsRetainPartialResults() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); for (int i = 0; i < 8; ++i) write(fixture.path() + QString("/item%1.txt").arg(i));
        SearchSummary summary; SearchRequest request{fixture.path(), "item"}; request.maxResults = 2;
        QCOMPARE(find(request, summary).size(), 2); QCOMPARE(summary.state, SearchState::Limited); QCOMPARE(summary.matches, quint64(2));
        request.maxResults = 10000; request.maxVisited = 1; QCOMPARE(find(request, summary).size(), 1); QCOMPARE(summary.state, SearchState::Limited); QCOMPARE(summary.visited, quint64(1));
    }
    void cancellationRetainsDeliveredMatchesAndCanStopBeforeStarting() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); for (int i = 0; i < 150; ++i) write(fixture.path() + QString("/item%1.txt").arg(i));
        std::atomic_bool cancel{false}; QVector<SearchEntry> entries;
        const auto summary = searchFiles({fixture.path(), "item"}, cancel, [&](const QVector<SearchEntry> &batch, const SearchSummary &) { entries += batch; cancel = true; });
        QCOMPARE(summary.state, SearchState::Cancelled); QVERIFY(!entries.isEmpty()); QVERIFY(entries.size() < 150); QCOMPARE(summary.matches, quint64(entries.size()));
        const auto stopped = searchFiles({fixture.path(), "item"}, cancel, {}); QCOMPARE(stopped.state, SearchState::Cancelled); QCOMPARE(stopped.visited, quint64(0));
    }
    void junctionIsSkippedAndItsTargetIsPreserved() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto root = fixture.path() + "/root"; const auto target = fixture.path() + "/target";
        QVERIFY(QDir().mkpath(root)); QVERIFY(QDir().mkpath(target)); write(root + "/ordinary.txt"); write(target + "/private.txt");
        const auto link = root + "/junction";
        QCOMPARE(QProcess::execute("cmd.exe", {"/d", "/c", "mklink", "/J", QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)}), 0);
        SearchSummary summary; const auto entries = find({root, "*.txt", true}, summary);
        QVERIFY(QDir().rmdir(link)); // Owned junction removed before fixture cleanup; never follow its target.
        QCOMPARE(summary.state, SearchState::Completed); QCOMPARE(summary.skippedLinks, quint64(1)); QCOMPARE(paths(entries), QStringList{root + "/ordinary.txt"}); QVERIFY(QFileInfo::exists(target + "/private.txt"));
    }
    void inaccessibleChildProducesPartialResultsAndRestoresItsPermissions() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto restricted = fixture.path() + "/restricted";
        QVERIFY(QDir().mkpath(restricted)); write(fixture.path() + "/visible.txt"); write(restricted + "/private.txt");
        auto native = QDir::toNativeSeparators(restricted).toStdWString(); PACL originalAcl = nullptr; PSECURITY_DESCRIPTOR descriptor = nullptr;
        QCOMPARE(GetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &originalAcl, nullptr, &descriptor), DWORD(ERROR_SUCCESS));
        ACL denyAll{}; QVERIFY(InitializeAcl(&denyAll, sizeof(denyAll), ACL_REVISION));
        const auto denied = SetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, &denyAll, nullptr);
        if (denied != ERROR_SUCCESS) { LocalFree(descriptor); QFAIL("Não foi possível preparar as permissões da pasta de teste."); }
        SearchSummary summary; const auto entries = find({fixture.path(), "*.txt", true}, summary);
        const auto restored = SetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, originalAcl, nullptr); LocalFree(descriptor);
        QCOMPARE(restored, DWORD(ERROR_SUCCESS)); QCOMPARE(summary.state, SearchState::Partial); QCOMPARE(summary.errors, quint64(1)); QCOMPARE(paths(entries), QStringList{fixture.path() + "/visible.txt"}); QVERIFY(QFileInfo::exists(restricted + "/private.txt"));
    }
    void representativeSearchMeasuresOneThousandNames() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); for (int folder = 0; folder < 10; ++folder) {
            const auto path = fixture.path() + QString("/pasta%1").arg(folder); QVERIFY(QDir().mkpath(path));
            for (int i = 0; i < 100; ++i) write(path + QString("/relatório-%1.txt").arg(i));
        }
        QElapsedTimer timer; timer.start(); SearchSummary summary; const auto entries = find({fixture.path(), "relatório"}, summary);
        qInfo() << "Busca representativa: 1000 nomes, 10 pastas, milissegundos:" << timer.elapsed();
        QCOMPARE(summary.state, SearchState::Completed); QCOMPARE(entries.size(), 1000); QCOMPARE(summary.visited, quint64(1010));
    }
    void jobPublishesBatchesFromWorkerAndFinishesBeforeSummaryIsRead() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); write(fixture.path() + "/match.txt");
        SearchJob job({fixture.path(), "match"}); QSignalSpy updates(&job, &SearchJob::updated), finished(&job, &QThread::finished);
        job.start(); QTRY_COMPARE(finished.count(), 1); QVERIFY(job.wait(1000)); QCOMPARE(job.summary().state, SearchState::Completed); QCOMPARE(job.summary().matches, quint64(1)); QCOMPARE(updates.count(), 1);
    }
    void legacySessionSeedsFavoritesButExplicitEmptyStaysEmpty() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto file = fixture.path() + "/session.json";
        SessionStore legacy(file); QVERIFY(legacy.save(session(fixture.path())).isEmpty());
        MainWindow seeded({}, {}, false, file); QVERIFY(seeded.session().favorites.has_value()); QVERIFY(!seeded.session().favorites->isEmpty()); QVERIFY(seeded.close());
        SessionStore empty(file); QCOMPARE(empty.load().state, SessionLoadState::Loaded); auto saved = session(fixture.path()); saved.favorites = QVector<FavoriteEntry>{}; QVERIFY(empty.save(saved).isEmpty());
        MainWindow reopened({}, {}, false, file); QVERIFY(reopened.session().favorites.has_value()); QVERIFY(reopened.session().favorites->isEmpty()); QCOMPARE(reopened.findChild<QListWidget *>("favoritesList")->count(), 0); QVERIFY(reopened.close());
    }
    void favoritesRoundTripPreservesIdentityOrderUnicodeAndUnavailablePaths() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto file = fixture.path() + "/session.json";
        auto saved = session(fixture.path()); saved.favorites = QVector<FavoriteEntry>{favorite("Fotos & férias", fixture.path() + "/offline"), favorite("Projeto", fixture.path())};
        SessionStore store(file); QVERIFY(store.save(saved).isEmpty()); SessionStore reader(file); const auto loaded = reader.load();
        QCOMPARE(loaded.state, SessionLoadState::Loaded); QCOMPARE(loaded.session.favorites->size(), 2); QCOMPARE(loaded.session.favorites->at(0).id, saved.favorites->at(0).id); QCOMPARE(loaded.session.favorites->at(0).path, fixture.path() + "/offline"); QCOMPARE(loaded.session.favorites->at(0).label, QString("Fotos & férias"));
        MainWindow window({}, {}, false, file); const auto original = window.leftPanel()->path(); auto *menu = window.findChild<QMenu *>("favoritesMenu"); QVERIFY(menu); menu->actions().first()->trigger();
        QCOMPARE(window.leftPanel()->path(), original); QCOMPARE(window.session().favorites->size(), 2); QVERIFY(window.statusBar()->currentMessage().contains("indisponível"));
    }
    void invalidFavoriteCannotOverwriteGoodSession() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto file = fixture.path() + "/session.json"; auto good = session(fixture.path()); good.favorites = QVector<FavoriteEntry>{favorite("Válido", fixture.path())};
        SessionStore store(file); QVERIFY(store.save(good).isEmpty()); QFile original(file); QVERIFY(original.open(QIODevice::ReadOnly)); const auto bytes = original.readAll(); original.close();
        auto invalid = good; invalid.favorites->append(invalid.favorites->first()); QVERIFY(!store.save(invalid).isEmpty());
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), bytes);
    }
    void editorReordersRenamesRemovesAndAddsWithoutTouchingFolders() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); QVERIFY(QDir(fixture.path()).mkdir("first")); QVERIFY(QDir(fixture.path()).mkdir("second"));
        QVector<FavoriteEntry> entries{favorite("Primeiro", fixture.path() + "/first"), favorite("Segundo", fixture.path() + "/second")};
        FavoritesDialog dialog(entries, fixture.path()); dialog.show(); auto *list = dialog.findChild<QTreeWidget *>("favoriteEntries");
        list->setCurrentItem(list->topLevelItem(1)); dialog.findChild<QPushButton *>("favoriteUp")->click(); list->topLevelItem(0)->setText(0, "Renomeado");
        QCOMPARE(dialog.favorites().first().id, entries[1].id); QCOMPARE(dialog.favorites().first().label, QString("Renomeado"));
        list->setCurrentItem(list->topLevelItem(1)); dialog.findChild<QPushButton *>("removeFavorite")->click(); dialog.findChild<QPushButton *>("addCurrentFavorite")->click(); dialog.findChild<QPushButton *>("saveFavorites")->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted)); QCOMPARE(dialog.favorites().size(), 2); QCOMPARE(dialog.favorites().last().path, fixture.path()); QVERIFY(QFileInfo(fixture.path() + "/first").isDir()); QVERIFY(QFileInfo(fixture.path() + "/second").isDir());
    }
    void favoriteEditorNativePreview() {
        const auto capture = qEnvironmentVariable("NAVEGA_FAVORITES_CAPTURE");
        if (capture.isEmpty()) QSKIP("Captura opcional da janela nativa, feita na execução Windows.");
        FavoritesDialog dialog({favorite("Documentos", QDir::homePath() + "/Documents"), favorite("Downloads", QDir::homePath() + "/Downloads"), favorite("Projeto Navega", QDir::currentPath() + "/opus-open")}, QDir::currentPath());
        dialog.show(); QVERIFY(QTest::qWaitForWindowExposed(&dialog)); QVERIFY(dialog.grab().save(capture));
    }
    void editorCancelPreservesOwnerAndSaveSurvivesRestart() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto file = fixture.path() + "/session.json"; auto state = session(fixture.path()); state.favorites = QVector<FavoriteEntry>{favorite("Original", fixture.path())}; SessionStore store(file); QVERIFY(store.save(state).isEmpty());
        MainWindow window({}, {}, false, file); auto *action = window.findChild<QAction *>("manageFavorites");
        QTimer::singleShot(0, &window, [&] { auto *dialog = window.findChild<FavoritesDialog *>(); QVERIFY(dialog); dialog->findChild<QTreeWidget *>("favoriteEntries")->topLevelItem(0)->setText(0, "Descartar"); dialog->reject(); });
        action->trigger(); QCOMPARE(window.session().favorites->first().label, QString("Original"));
        QTimer::singleShot(0, &window, [&] { auto *dialog = window.findChild<FavoritesDialog *>(); QVERIFY(dialog); dialog->findChild<QTreeWidget *>("favoriteEntries")->topLevelItem(0)->setText(0, "Guardado"); dialog->findChild<QPushButton *>("saveFavorites")->click(); });
        action->trigger(); QCOMPARE(window.session().favorites->first().label, QString("Guardado")); QVERIFY(window.close());
        MainWindow reopened({}, {}, false, file); QCOMPARE(reopened.session().favorites->first().label, QString("Guardado")); QVERIFY(reopened.close());
    }
    void searchUiOpensResultFolderInNewTabAndCanBeClosedWhileRunning() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); QVERIFY(QDir(fixture.path()).mkpath("Documentos/2026")); write(fixture.path() + "/Documentos/2026/Relatório.pdf"); write(fixture.path() + "/Documentos/2026/Planejamento.pdf");
        MainWindow window(fixture.path(), fixture.path(), false); window.show(); window.findChild<QAction *>("searchFiles")->trigger();
        QPointer<SearchDialog> dialog = window.findChild<SearchDialog *>(); QVERIFY(dialog); dialog->findChild<QLineEdit *>("searchName")->setText("*.pdf"); dialog->findChild<QCheckBox *>("searchWildcard")->setChecked(true);
        auto *engine = dialog->findChild<QComboBox *>("searchEngine"); engine->setCurrentIndex(engine->findData(int(SearchEngine::Direct)));
        auto *start = dialog->findChild<QPushButton *>("startSearch"); start->click(); QTRY_VERIFY(start->isEnabled()); auto *results = dialog->findChild<QTreeWidget *>("searchResults"); QCOMPARE(results->topLevelItemCount(), 2);
        const auto capture = qEnvironmentVariable("NAVEGA_SEARCH_CAPTURE");
        if (!capture.isEmpty()) { QVERIFY(QTest::qWaitForWindowExposed(dialog)); QVERIFY(dialog->grab().save(capture)); }
        results->setCurrentItem(results->topLevelItem(0)); dialog->findChild<QPushButton *>("openSearchFolder")->click(); QCOMPARE(window.leftPanel()->tabCount(), 2); QCOMPARE(window.leftPanel()->path(), fixture.path() + "/Documentos/2026");
        dialog->close(); QTRY_VERIFY(dialog.isNull()); window.findChild<QAction *>("searchFiles")->trigger(); dialog = window.findChild<SearchDialog *>(); QVERIFY(dialog);
        dialog->findChild<QLineEdit *>("searchName")->setText("pdf"); dialog->findChild<QPushButton *>("startSearch")->click(); dialog->close(); QTRY_VERIFY(dialog.isNull());
    }
};
QTEST_MAIN(SearchFavoritesTest)
#include "search_favorites_test.moc"
