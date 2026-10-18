#include "filesearch.h"
#include "everythingipc.h"
#include "searchquery.h"
#include "searchdialog.h"
#include "windowspath.h"
#include <QComboBox>
#include <QCheckBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QProcess>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QTimer>
#include <QUuid>
#include <QtEndian>
#include <cstring>
#include <windows.h>

namespace {
void set32(QByteArray &bytes, qsizetype at, quint32 value) { qToLittleEndian(value, bytes.data() + at); }
QByteArray pageBytes(const QVector<SearchEntry> &entries, quint32 total, quint32 offset) {
    QByteArray bytes(20 + entries.size() * 8, '\0');
    set32(bytes, 0, total); set32(bytes, 4, quint32(entries.size())); set32(bytes, 8, offset);
    set32(bytes, 12, EverythingIpc::FullPath); set32(bytes, 16, EverythingIpc::NameAscending);
    for (qsizetype i = 0; i < entries.size(); ++i) {
        set32(bytes, 20 + i * 8, entries[i].directory ? 1 : 0); set32(bytes, 24 + i * 8, quint32(bytes.size()));
        const auto path = QDir::toNativeSeparators(entries[i].path); const auto start = bytes.size();
        bytes.resize(start + 4 + (path.size() + 1) * 2); set32(bytes, start, quint32(path.size()));
        for (qsizetype c = 0; c < path.size(); ++c) qToLittleEndian(path[c].unicode(), bytes.data() + start + 4 + c * 2);
        qToLittleEndian(quint16(0), bytes.data() + bytes.size() - 2);
    }
    return bytes;
}
// Native protocol fixture only. Real-provider behavior is checked separately.
class ProtocolServer final : public QThread {
public:
    enum class Mode { Ready, Empty, NotReady, Unsupported, Silent, MalformedSecondPage, ChangingTotal };
    QString instance = "DirOpen-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVector<SearchEntry> entries;
    Mode mode = Mode::Ready;
    QSemaphore ready, queried;
    HWND window = nullptr;
    EverythingIpc::Query pending{};
    QString queryText; // Read only after the requesting search has completed.
    ~ProtocolServer() override { if (window) PostMessageW(window, WM_CLOSE, 0, 0); wait(); }
    static LRESULT CALLBACK receive(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams));
        auto *self = reinterpret_cast<ProtocolServer *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self && message == WM_USER) {
            if (wparam == 0) return 1;
            if (wparam == 1) return self->mode == Mode::Unsupported ? 3 : 5;
            if (wparam == 2) return 0;
            if (wparam == 3) return 1423;
            if (wparam == 401) return self->mode != Mode::NotReady;
        }
        if (self && message == WM_COPYDATA) {
            const auto *data = reinterpret_cast<COPYDATASTRUCT *>(lparam);
            if (data->dwData != EverythingIpc::Query2W || data->cbData < sizeof(EverythingIpc::Query)) return FALSE;
            std::memcpy(&self->pending, data->lpData, sizeof(self->pending));
            self->queryText = QString::fromWCharArray(reinterpret_cast<const wchar_t *>(static_cast<const char *>(data->lpData) + sizeof(self->pending)));
            self->queried.release(); if (self->mode != Mode::Silent) PostMessageW(hwnd, WM_APP, 0, 0); return TRUE;
        }
        if (self && message == WM_APP) {
            const auto query = self->pending;
            const auto subset = self->mode == Mode::Empty ? QVector<SearchEntry>{} : self->entries.mid(query.offset, query.maxResults);
            auto total = self->mode == Mode::Empty ? 0 : quint32(self->entries.size());
            if (self->mode == Mode::ChangingTotal && query.offset) ++total;
            auto bytes = pageBytes(subset, total, query.offset);
            if (self->mode == Mode::MalformedSecondPage && query.offset) bytes = QByteArray(3, '\0');
            COPYDATASTRUCT data{query.replyId, DWORD(bytes.size()), bytes.data()}; DWORD_PTR result = 0;
            SendMessageTimeoutW(reinterpret_cast<HWND>(quintptr(query.replyWindow)), WM_COPYDATA, reinterpret_cast<WPARAM>(hwnd), reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG, 1000, &result);
            return 0;
        }
        if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    void run() override {
        const auto name = ("EVERYTHING_TASKBAR_NOTIFICATION_(" + instance + ')').toStdWString();
        WNDCLASSW value{}; value.lpfnWndProc = receive; value.hInstance = GetModuleHandleW(nullptr); value.lpszClassName = name.c_str();
        const auto atom = RegisterClassW(&value);
        if (atom) window = CreateWindowExW(0, name.c_str(), L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, value.hInstance, this);
        ready.release();
        if (window) { MSG message; while (GetMessageW(&message, nullptr, 0, 0) > 0) DispatchMessageW(&message); }
        if (window && IsWindow(window)) DestroyWindow(window);
        window = nullptr; if (atom) UnregisterClassW(name.c_str(), value.hInstance);
    }
};
}

class EverythingSearchTest final : public QObject {
    Q_OBJECT
private:
    void write(const QString &path) { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("fixture preserved"), qint64(17)); }
    QVector<SearchEntry> collect(const SearchRequest &request, SearchSummary &summary) {
        std::atomic_bool cancel{false}; QVector<SearchEntry> entries;
        summary = searchFiles(request, cancel, [&](const auto &batch, const auto &) { entries += batch; }); return entries;
    }
    QStringList paths(const QVector<SearchEntry> &entries) { QStringList out; for (const auto &e : entries) out.append(e.path); out.sort(); return out; }
    SearchRequest request(const QString &root, ProtocolServer &server) {
        SearchRequest r{root, "item"}; r.engine = SearchEngine::Everything; r.everythingInstance = server.instance; return r;
    }
private slots:
    void liveFiltersShareNavigationRulesAndValidateIntervals() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const auto sub = fixture.path() + "/sub"; QVERIFY(QDir().mkpath(sub));
        write(sub + "/item.txt"); write(sub + "/item.pdf");
        const auto stamp = QDateTime(QDate(2024, 2, 29), QTime(12, 0));
        for (const auto &name : {"item.txt", "item.pdf"}) {
            QFile file(sub + '/' + name); QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.setFileTime(stamp, QFileDevice::FileModificationTime));
        }
        SearchRequest r{fixture.path(), "*", true}; r.filters.patterns = "*.TXT";
        r.filters.minimumBytes = 17; r.filters.maximumBytes = 17;
        r.filters.modifiedFrom = stamp.date(); r.filters.modifiedTo = stamp.date(); r.filters.folders = false;
        SearchSummary s;
        QCOMPARE(paths(collect(r, s)), QStringList{sub + "/item.txt"}); QCOMPARE(s.state, SearchState::Completed);
        ProtocolServer server; server.entries = {{sub, true}, {sub + "/item.txt", false}, {sub + "/item.pdf", false}};
        server.start(); QVERIFY(server.ready.tryAcquire(1, 3000)); r.engine = SearchEngine::Everything; r.everythingInstance = server.instance;
        QCOMPARE(paths(collect(r, s)), QStringList{sub + "/item.txt"}); QCOMPARE(s.state, SearchState::Completed);
        r.engine = SearchEngine::Direct;
        r.filters.minimumBytes = 18; r.filters.maximumBytes = -1; QVERIFY(collect(r, s).isEmpty()); QCOMPARE(s.state, SearchState::Completed);
        r.filters.minimumBytes = 0; r.filters.modifiedFrom = stamp.date().addDays(1); r.filters.modifiedTo = {};
        QVERIFY(collect(r, s).isEmpty()); QCOMPARE(s.state, SearchState::Completed);
        r.filters.maximumBytes = -2; QVERIFY(collect(r, s).isEmpty()); QCOMPARE(s.failure->code, SearchErrorCode::InvalidRequest);
        r.filters = {}; r.filters.modifiedFrom = QDate(2025, 1, 1); r.filters.modifiedTo = QDate(2024, 1, 1);
        QVERIFY(collect(r, s).isEmpty()); QCOMPARE(s.failure->code, SearchErrorCode::InvalidRequest);
        r.filters = {}; r.filters.files = false; r.filters.folders = false;
        QVERIFY(collect(r, s).isEmpty()); QCOMPARE(s.failure->code, SearchErrorCode::InvalidRequest);
        r.filters = {}; r.filters.files = false;
        QCOMPARE(paths(collect(r, s)), QStringList{sub}); QCOMPARE(s.state, SearchState::Completed);
    }
    void fullPathMatchingUsesOneNormalizedPatternForBothEngines() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const auto sub = fixture.path() + "/ação [1]"; QVERIFY(QDir().mkpath(sub)); write(sub + "/item.txt");
        ProtocolServer server; server.entries = {{sub + "/item.txt", false}};
        server.start(); QVERIFY(server.ready.tryAcquire(1, 3000));
        auto r = request(fixture.path(), server); r.name = "ação [1]/item"; r.matchPath = true;
        SearchSummary s; QCOMPARE(paths(collect(r, s)), paths(server.entries)); QCOMPARE(s.state, SearchState::Completed);
        QVERIFY(server.queryText.contains("path:diacritics:regex:"));
        r.engine = SearchEngine::Direct; QCOMPARE(paths(collect(r, s)), paths(server.entries));
        r.name = "*ação*/item.?xt"; r.wildcard = true; QCOMPARE(paths(collect(r, s)), paths(server.entries));
        r.engine = SearchEngine::Everything; QCOMPARE(paths(collect(r, s)), paths(server.entries));
        r.engine = SearchEngine::Direct; r.matchPath = false; QVERIFY(collect(r, s).isEmpty());
    }
    void filteredPaginationCountsCandidatesSeparatelyAndKeepsLimitsHonest() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); ProtocolServer server;
        const auto rejected = fixture.path() + "/item.pdf", accepted = fixture.path() + "/item.txt";
        write(rejected); write(accepted);
        for (quint32 i = 0; i < EverythingIpc::PageSize + 1; ++i) server.entries.append({rejected, false});
        server.entries.append({accepted, false}); server.start(); QVERIFY(server.ready.tryAcquire(1, 3000));
        auto r = request(fixture.path(), server); r.filters.patterns = "*.txt"; r.maxResults = 1;
        SearchSummary s; QCOMPARE(paths(collect(r, s)), QStringList{accepted}); QCOMPARE(s.state, SearchState::Completed);
        QCOMPARE(s.visited, quint64(server.entries.size())); QCOMPARE(s.matches, quint64(1));
        r.maxVisited = EverythingIpc::PageSize; QVERIFY(collect(r, s).isEmpty()); QCOMPARE(s.state, SearchState::Limited);
        QCOMPARE(s.visited, r.maxVisited);
        r.maxVisited = 1000000; r.filters.patterns = "*.pdf";
        QCOMPARE(collect(r, s).size(), 1); QCOMPARE(s.state, SearchState::Limited); QCOMPARE(s.visited, quint64(1));
        r.filters.patterns = "*.absent";
        std::atomic_bool cancel{false};
        s = searchFiles(r, cancel, [&](const auto &, const auto &) { cancel = true; });
        QCOMPARE(s.state, SearchState::Cancelled);
    }
    void searchDialogEditsSharedFiltersAndSelectsNamedInstance() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); write(fixture.path() + "/item.txt"); write(fixture.path() + "/item.pdf");
        ProtocolServer server; server.entries = {{fixture.path() + "/item.txt", false}, {fixture.path() + "/item.pdf", false}};
        server.start(); QVERIFY(server.ready.tryAcquire(1, 3000));
        SearchDialog dialog(fixture.path()); dialog.setAttribute(Qt::WA_DeleteOnClose, false); dialog.show();
        dialog.findChild<QLineEdit *>("searchName")->setText("item");
        dialog.findChild<QLineEdit *>("searchEverythingInstance")->setText(server.instance);
        QTimer::singleShot(0, &dialog, [&] {
            auto *editor = dialog.findChild<QDialog *>("folderRulesDialog"); QVERIFY(editor);
            QCOMPARE(editor->windowTitle(), QString("Filtros da busca"));
            editor->findChild<QLineEdit *>("rulePatterns")->setText("*.txt");
            editor->findChild<QLineEdit *>("ruleMinimum")->setText("17");
            editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        dialog.findChild<QPushButton *>("searchFilters")->click();
        auto *start = dialog.findChild<QPushButton *>("startSearch"); start->click();
        QVERIFY(!dialog.findChild<QPushButton *>("searchFilters")->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 5000);
        QCOMPARE(dialog.findChild<QTreeWidget *>("searchResults")->topLevelItemCount(), 1);
        QVERIFY(dialog.findChild<QLabel *>("searchStatus")->text().contains(server.instance));
        QVERIFY(server.queryText.contains("nopath:"));
        QTimer::singleShot(0, &dialog, [&] { auto *editor = dialog.findChild<QDialog *>("folderRulesDialog"); QVERIFY(editor);
            editor->findChild<QLineEdit *>("rulePatterns")->setText("*.pdf"); editor->reject(); });
        dialog.findChild<QPushButton *>("searchFilters")->click(); start->click(); QTRY_VERIFY(start->isEnabled());
        QCOMPARE(dialog.findChild<QTreeWidget *>("searchResults")->topLevelItem(0)->text(0), QString("item.txt"));
        const auto capture = qEnvironmentVariable("DIROPEN_SEARCH_FILTER_CAPTURE");
        if (!capture.isEmpty()) { QVERIFY(QTest::qWaitForWindowExposed(&dialog)); QVERIFY(dialog.grab().save(capture)); }
        QTimer::singleShot(0, &dialog, [&] { auto *editor = dialog.findChild<QDialog *>("folderRulesDialog"); QVERIFY(editor);
            editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Reset)->click(); });
        dialog.findChild<QPushButton *>("searchFilters")->click(); start->click(); QTRY_VERIFY(start->isEnabled());
        QCOMPARE(dialog.findChild<QTreeWidget *>("searchResults")->topLevelItemCount(), 2);
        dialog.close();
    }
    void packetRejectsTruncationCountsOffsetsFlagsAndUnterminatedPaths() {
        const auto valid = pageBytes({{"C:/dados/férias # %.txt", false}, {"C:/dados/sub", true}}, 2, 0);
        auto decoded = EverythingIpc::decodePage(valid, 0, 128); QVERIFY(std::holds_alternative<EverythingIpc::Page>(decoded));
        QCOMPARE(std::get<EverythingIpc::Page>(decoded).entries[0].path, QString("C:/dados/férias # %.txt"));
        QVector<QByteArray> invalid{valid.first(19), valid.first(valid.size() - 1)};
        for (const auto at : {4, 8, 12, 16, 24}) { auto bytes = valid; set32(bytes, at, 0xffffffff); invalid.append(bytes); }
        auto enormous = valid; set32(enormous, 36, 0xffffffff); invalid.append(enormous);
        auto unterminated = valid; unterminated[unterminated.size() - 2] = 'x'; invalid.append(unterminated);
        invalid.append(pageBytes({{"relative.txt", false}}, 1, 0));
        invalid.append(pageBytes({{"C:/dados/../escape.txt", false}}, 1, 0));
        for (const auto &bytes : invalid) QVERIFY(std::holds_alternative<SearchFailure>(EverythingIpc::decodePage(bytes, 0, 128)));
    }
    void commonMatcherPreservesLiteralOperatorsAndWildcardSemantics() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const auto extended = prepareSearch({WindowsPath::forFileApi(fixture.path()), "item"});
        QVERIFY(std::holds_alternative<PreparedSearch>(extended));
        QCOMPARE(std::get<PreparedSearch>(extended).root, fixture.path());
        for (const auto &name : {QString("literal[1] ! size:>5"), QString("ação # 100%.txt"), QString("a\"b")}) {
            const auto result = prepareSearch({fixture.path(), name}); QVERIFY(std::holds_alternative<PreparedSearch>(result));
            const auto &p = std::get<PreparedSearch>(result); QVERIFY(p.matches(name)); QVERIFY(!p.matches("other"));
            QVERIFY(p.contains(fixture.path() + "/sub/file")); QVERIFY(!p.contains(fixture.path() + "-other/file"));
        }
    }
    void explicitUnavailableFailsWhileAutomaticFallsBackWithReason() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); write(fixture.path() + "/item.txt");
        ProtocolServer absent; auto r = request(fixture.path(), absent); SearchSummary s;
        QVERIFY(collect(r, s).isEmpty()); QCOMPARE(s.state, SearchState::Failed); QVERIFY(s.failure);
        QCOMPARE(s.failure->code, SearchErrorCode::Unavailable);
        r.engine = SearchEngine::Automatic; QCOMPARE(collect(r, s).size(), 1); QCOMPARE(s.state, SearchState::Completed);
        QCOMPARE(s.engine, SearchEngine::Direct); QVERIFY(!s.fallbackReason.isEmpty());
        r.filters.patterns = "*.pdf"; QVERIFY(collect(r, s).isEmpty()); QCOMPARE(s.state, SearchState::Completed);
        QVERIFY(!s.fallbackReason.isEmpty());
    }
    void unreadyAndUnsupportedMayFallbackButEmptyIndexDoesNot() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); write(fixture.path() + "/item.txt");
        for (auto mode : {ProtocolServer::Mode::NotReady, ProtocolServer::Mode::Unsupported, ProtocolServer::Mode::Empty}) {
            ProtocolServer server; server.mode = mode; server.start(); QVERIFY(server.ready.tryAcquire(1, 3000)); QVERIFY(server.window);
            auto r = request(fixture.path(), server); r.engine = SearchEngine::Automatic; SearchSummary s;
            const auto entries = collect(r, s); QCOMPARE(s.state, SearchState::Completed);
            if (mode == ProtocolServer::Mode::Empty) { QVERIFY(entries.isEmpty()); QCOMPARE(s.engine, SearchEngine::Everything); QVERIFY(s.fallbackReason.isEmpty()); QCOMPARE(*s.indexedTotal, quint64(0)); }
            else { QCOMPARE(entries.size(), 1); QCOMPARE(s.engine, SearchEngine::Direct); QVERIFY(!s.fallbackReason.isEmpty()); }
        }
    }
    void paginatedProtocolPreservesPathsAndExactResultLimit() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); ProtocolServer server;
        const int count = int(EverythingIpc::PageSize) * 2 + 4;
        for (int i = 0; i < count; ++i) { const auto path = fixture.path() + QString("/item-ação #%1.txt").arg(i, 4, 10, QChar('0')); write(path); server.entries.append({path, false}); }
        server.start(); QVERIFY(server.ready.tryAcquire(1, 3000)); QVERIFY(server.window);
        auto r = request(fixture.path(), server); SearchSummary s;
        QCOMPARE(paths(collect(r, s)), paths(server.entries)); QCOMPARE(s.state, SearchState::Completed); QCOMPARE(*s.indexedTotal, quint64(count));
        r.maxResults = int(EverythingIpc::PageSize) + 2; QCOMPARE(collect(r, s).size(), r.maxResults); QCOMPARE(s.state, SearchState::Limited);
        r.maxResults = count; QCOMPARE(collect(r, s).size(), count); QCOMPARE(s.state, SearchState::Completed);
        std::atomic_bool cancel{false}; QVector<SearchEntry> partial;
        s = searchFiles(r, cancel, [&](const auto &batch, const auto &) { partial += batch; cancel = true; });
        QCOMPARE(s.state, SearchState::Cancelled); QCOMPARE(partial.size(), qsizetype(EverythingIpc::PageSize)); QCOMPARE(s.matches, quint64(EverythingIpc::PageSize));
    }
    void protocolFailureAfterFirstPageRetainsOnlyDeliveredFacts() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        for (auto mode : {ProtocolServer::Mode::MalformedSecondPage, ProtocolServer::Mode::ChangingTotal}) {
            ProtocolServer server; server.mode = mode;
            for (quint32 i = 0; i <= EverythingIpc::PageSize; ++i) { const auto path = fixture.path() + QString("/item-%1.txt").arg(i); write(path); server.entries.append({path, false}); }
            server.start(); QVERIFY(server.ready.tryAcquire(1, 3000)); SearchSummary s;
            QCOMPARE(collect(request(fixture.path(), server), s).size(), qsizetype(EverythingIpc::PageSize)); QCOMPARE(s.state, SearchState::Failed);
            QCOMPARE(s.matches, quint64(EverythingIpc::PageSize)); QVERIFY(s.failure); QCOMPARE(s.failure->code, SearchErrorCode::Protocol);
        }
    }
    void outOfScopePageCannotCountUndeliveredEntries() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); ProtocolServer server;
        const auto valid = fixture.path() + "/item.txt"; write(valid);
        server.entries = {{valid, false}, {fixture.path() + "-other/item.txt", false}};
        server.start(); QVERIFY(server.ready.tryAcquire(1, 3000)); SearchSummary s;
        QVERIFY(collect(request(fixture.path(), server), s).isEmpty()); QCOMPARE(s.matches, quint64(0));
        QCOMPARE(s.state, SearchState::Failed); QCOMPARE(s.failure->code, SearchErrorCode::Protocol);
    }
    void stalePathsAndJunctionDescendantsRemainPartialWithoutFollowingTargets() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); ProtocolServer server;
        const auto root = fixture.path() + "/root", target = fixture.path() + "/target";
        QVERIFY(QDir().mkpath(root)); QVERIFY(QDir().mkpath(target)); write(root + "/item.txt"); write(target + "/item-private.txt");
        const auto link = root + "/junction";
        QCOMPARE(QProcess::execute("cmd.exe", {"/d", "/c", "mklink", "/J", QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)}), 0);
        server.entries = {{root + "/item.txt", false}, {root + "/item-missing.txt", false}, {link + "/item-private.txt", false}};
        server.start(); QVERIFY(server.ready.tryAcquire(1, 3000)); SearchSummary s;
        const auto entries = collect(request(root, server), s);
        QVERIFY(QDir().rmdir(link)); // Remove only the owned junction before fixture cleanup.
        QCOMPARE(entries.size(), 1); QCOMPARE(s.state, SearchState::Partial); QCOMPARE(s.errors, quint64(1));
        QCOMPARE(s.skippedLinks, quint64(1)); QVERIFY(QFileInfo::exists(target + "/item-private.txt"));
    }
    void deadlineAndCancellationReleaseTheWaitingWorker() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); ProtocolServer server; server.mode = ProtocolServer::Mode::Silent;
        server.start(); QVERIFY(server.ready.tryAcquire(1, 3000)); auto r = request(fixture.path(), server); r.timeoutMs = 150;
        SearchSummary s; QElapsedTimer timer; timer.start(); QVERIFY(collect(r, s).isEmpty());
        QCOMPARE(s.state, SearchState::Failed); QCOMPARE(s.failure->code, SearchErrorCode::Timeout); QVERIFY(timer.elapsed() < 2000);
        r.timeoutMs = 5000; SearchJob job(r); server.queried.tryAcquire(server.queried.available()); job.start();
        QVERIFY(server.queried.tryAcquire(1, 2000)); timer.restart(); job.cancel(); QVERIFY(job.wait(1500));
        QCOMPARE(job.summary().state, SearchState::Cancelled); QVERIFY(timer.elapsed() < 1500);
    }
    void realEverythingAndNativeSearchDialog() {
        if (!qEnvironmentVariableIsSet("DIROPEN_TEST_EVERYTHING")) QSKIP("Verificação real requer Everything em execução e DIROPEN_TEST_EVERYTHING=1.");
        QTemporaryDir fixture(QDir::current().filePath("diropen-everything-XXXXXX")); QVERIFY(fixture.isValid());
        QVERIFY(QDir(fixture.path()).mkdir("sub"));
        const int count = int(EverythingIpc::PageSize) + 4;
        for (int i = 0; i < count; ++i) write(fixture.path() + QString("/sub/item-ação #%1.txt").arg(i, 4, 10, QChar('0')));
        write(fixture.path() + "/literal[1] ! %.txt"); write(fixture.path() + "/sub/not-an-item.pdf");
        const auto hidden = fixture.path() + "/item-ação-hidden.txt"; write(hidden);
        QVERIFY(SetFileAttributesW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(hidden).utf16()), FILE_ATTRIBUTE_HIDDEN));
        SearchRequest r{fixture.path(), "item-ação"}; r.engine = SearchEngine::Everything;
        SearchSummary indexed; QVector<SearchEntry> entries;
        QTRY_COMPARE_WITH_TIMEOUT((entries = collect(r, indexed)).size(), count + 1, 15000);
        QCOMPARE(indexed.state, SearchState::Completed); QCOMPARE(indexed.engine, SearchEngine::Everything);
        SearchSummary direct; r.engine = SearchEngine::Direct; QCOMPARE(paths(collect(r, direct)), paths(entries));
        r.engine = SearchEngine::Everything;
        for (const auto &name : {QString("[1] ! %"), QString("size:>5 | !"), QString("a\"b")}) {
            r.name = name; const auto found = collect(r, indexed); QCOMPARE(indexed.state, SearchState::Completed);
            QCOMPARE(found.size(), name == "[1] ! %" ? 1 : 0);
        }
        r.name = "*.pdf"; r.wildcard = true; QCOMPARE(collect(r, indexed).size(), 1); QCOMPARE(indexed.state, SearchState::Completed);
        r.name = "sub/item-ação"; r.wildcard = false; r.matchPath = true;
        QCOMPARE(collect(r, indexed).size(), count); QCOMPARE(indexed.state, SearchState::Completed);
        r.name = "*sub/*.txt"; r.wildcard = true;
        QCOMPARE(collect(r, indexed).size(), count); QCOMPARE(indexed.state, SearchState::Completed);
        r.filters.patterns = "*.txt"; r.filters.folders = false; r.filters.minimumBytes = 17; r.filters.maximumBytes = 17;
        r.filters.modifiedFrom = QFileInfo(hidden).lastModified().date(); r.filters.modifiedTo = r.filters.modifiedFrom;
        const auto liveFiltered = collect(r, indexed); QCOMPARE(liveFiltered.size(), count); QCOMPARE(indexed.state, SearchState::Completed);
        r.engine = SearchEngine::Direct; QCOMPARE(paths(collect(r, direct)), paths(liveFiltered));
        r.engine = SearchEngine::Everything; r.filters.minimumBytes = 18; r.filters.maximumBytes = -1;
        QVERIFY(collect(r, indexed).isEmpty()); QCOMPARE(indexed.state, SearchState::Completed);
        auto *dialog = new SearchDialog(fixture.path()); QPointer<SearchDialog> guard(dialog); dialog->show();
        dialog->findChild<QLineEdit *>("searchName")->setText("[1] ! %");
        auto *start = dialog->findChild<QPushButton *>("startSearch"); start->click(); QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 10000);
        auto *results = dialog->findChild<QTreeWidget *>("searchResults"); QCOMPARE(results->topLevelItemCount(), 1);
        QVERIFY(dialog->findChild<QLabel *>("searchStatus")->text().contains("Everything"));
        QSignalSpy opening(dialog, &SearchDialog::openFileRequested); results->setCurrentItem(results->topLevelItem(0));
        dialog->findChild<QPushButton *>("openSearchResult")->click(); QCOMPARE(opening.size(), 1);
        QCOMPARE(opening[0][0].toString(), fixture.path() + "/literal[1] ! %.txt");
        const auto capture = qEnvironmentVariable("DIROPEN_EVERYTHING_CAPTURE");
        if (!capture.isEmpty()) { QVERIFY(QTest::qWaitForWindowExposed(dialog)); QVERIFY(dialog->grab().save(capture)); }
        dialog->close(); QTRY_VERIFY(guard.isNull());
    }
    void representativeCompletedSearchTask() {
        const auto root = qEnvironmentVariable("DIROPEN_SEARCH_BENCH_ROOT");
        if (root.isEmpty()) QSKIP("Medição opcional com fixture preparada e motores equivalentes.");
        const bool indexed = qEnvironmentVariable("DIROPEN_SEARCH_BENCH_ENGINE") == "everything";
        const auto expected = qEnvironmentVariableIntValue("DIROPEN_SEARCH_BENCH_EXPECTED"); QVERIFY(expected > 0 && expected <= 10000);
        SearchDialog dialog(root); dialog.setAttribute(Qt::WA_DeleteOnClose, false); dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto *engine = dialog.findChild<QComboBox *>("searchEngine");
        engine->setCurrentIndex(engine->findData(int(indexed ? SearchEngine::Everything : SearchEngine::Direct)));
        dialog.findChild<QLineEdit *>("searchName")->setText("relatório-");
        const auto filter = qEnvironmentVariable("DIROPEN_SEARCH_BENCH_FILTER");
        if (!filter.isEmpty()) {
            QTimer::singleShot(0, &dialog, [&] {
                auto *editor = dialog.findChild<QDialog *>("folderRulesDialog"); QVERIFY(editor);
                editor->findChild<QLineEdit *>("rulePatterns")->setText(filter);
                editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            });
            dialog.findChild<QPushButton *>("searchFilters")->click();
        }
        QJsonArray measurements;
        for (int pass = 0; pass < 2; ++pass) {
            QElapsedTimer timer; timer.start(); auto *start = dialog.findChild<QPushButton *>("startSearch"); start->click();
            while (!start->isEnabled() && timer.elapsed() < 15000) QTest::qWait(1);
            QVERIFY(start->isEnabled()); const auto elapsed = timer.nsecsElapsed() / 1000000.0;
            QCOMPARE(dialog.findChild<QTreeWidget *>("searchResults")->topLevelItemCount(), expected);
            const auto status = dialog.findChild<QLabel *>("searchStatus")->text(); QVERIFY(status.contains("Busca concluída"));
            QVERIFY(status.contains(indexed ? "Everything" : "Busca direta"));
            measurements.append(QJsonObject{{"pass", pass + 1}, {"elapsedMs", elapsed}, {"results", expected}});
        }
        const auto output = qEnvironmentVariable("DIROPEN_SEARCH_BENCH_OUTPUT"); QVERIFY(!output.isEmpty());
        QFile file(output); QVERIFY(file.open(QIODevice::WriteOnly));
        const auto bytes = QJsonDocument(QJsonObject{{"engine", indexed ? "everything" : "direct"}, {"filter", filter}, {"tasks", measurements}}).toJson();
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
    }
};
QTEST_MAIN(EverythingSearchTest)
#include "everything_search_test.moc"
