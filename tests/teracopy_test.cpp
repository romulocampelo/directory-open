#include "teracopy.h"
#include "operationdialog.h"
#include "mainwindow.h"
#include "filepanel.h"
#include "sessionstore.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QSaveFile>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <windows.h>

// A launched process records its received arguments and exits successfully without
// copying anything. This tests the real process boundary, not provider behavior.
int receiveArguments(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments().mid(1);
    if (args.size() != 5 || args[0] != "Copy" || !args[1].startsWith('*')) return 2;
    QSaveFile output(args[1].mid(1) + ".received.json");
    if (!output.open(QIODevice::WriteOnly)) return 3;
    return output.write(QJsonDocument(QJsonObject{{"destination", args[2]}, {"conflict", args[3]}, {"window", args[4]}}).toJson()) > 0 && output.commit() ? 0 : 4;
}
class ProgramRoots final {
    QVector<QByteArray> values;
    QVector<bool> existed;
    QByteArray originalPath;
public:
    explicit ProgramRoots(const QString &root) {
        originalPath=qgetenv("PATH");
        // The copied probe must resolve the same deployed Qt DLLs even when
        // CTest is launched outside build.cmd's compiler environment.
        qputenv("PATH",QDir::toNativeSeparators(QCoreApplication::applicationDirPath()).toUtf8()+";"+originalPath);
        for (const auto key : {"ProgramW6432", "ProgramFiles", "ProgramFiles(x86)"}) {
            existed.append(qEnvironmentVariableIsSet(key)); values.append(qgetenv(key)); qputenv(key, root.toUtf8());
        }
    }
    ~ProgramRoots() {
        qputenv("PATH",originalPath);
        int i = 0;
        for (const auto key : {"ProgramW6432", "ProgramFiles", "ProgramFiles(x86)"}) {
            if (existed[i]) qputenv(key, values[i]); else qunsetenv(key); ++i;
        }
    }
};
class TeraCopyTest : public QObject {
    Q_OBJECT
    static QByteArray read(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
    static bool write(const QString &path, const QByteArray &bytes) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(); }
    static OperationRequest request(const QString &root) {
        OperationRequest r; r.sources = {root + "/ação 日本 & arquivo.txt"}; r.destination = root + "/destino";
        r.engine = TransferEngine::TeraCopy; r.conflict = OperationConflict::KeepBoth; return r;
    }
    static bool fixture(const QString &root) { return QDir().mkpath(root + "/destino") && write(request(root).sources.first(), "fixture conteúdo"); }
private slots:
    void validationAndCancellation() {
        QTemporaryDir dir; QVERIFY(fixture(dir.path())); auto r = request(dir.path());
        r.kind = OperationKind::Move; QVERIFY(!validateOperationRequest(r).isEmpty());
        r.kind = OperationKind::Copy; r.conflict = OperationConflict::Ask; QVERIFY(!validateOperationRequest(r).isEmpty());
        r.conflict = OperationConflict::KeepBoth; r.sources[0] += '\n'; QVERIFY(!validateOperationRequest(r).isEmpty());
        r = request(dir.path()); r.destination = dir.path(); QVERIFY(!validateOperationRequest(r).isEmpty());
        r = request(dir.path()); std::atomic_bool cancel{true}; const auto out = dispatchTeraCopy(r, cancel, {}, dir.filePath("records"));
        QCOMPARE(out.state, OperationState::Cancelled); QCOMPARE(out.completedCount(), 0); QVERIFY(!QFileInfo::exists(dir.filePath("records")));
        QVERIFY(QFileInfo::exists(r.sources.first())); QVERIFY(QDir(r.destination).isEmpty());
    }
    void missingAndBrokenProviderNeverFallback() {
        QTemporaryDir dir; QVERIFY(fixture(dir.path())); ProgramRoots roots(dir.path());
        std::atomic_bool cancel{false}; auto r = request(dir.path());
        auto out = dispatchTeraCopy(r, cancel, {}, dir.filePath("records"));
        QCOMPARE(out.state, OperationState::Failed); QVERIFY(!out.error.isEmpty()); QVERIFY(QDir(r.destination).isEmpty());
        QVERIFY(QDir().mkpath(dir.filePath("TeraCopy")));
        const auto executable = dir.filePath("TeraCopy/TeraCopy.exe");
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), executable));
        const auto native = QDir::toNativeSeparators(executable);
        const auto lock = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        QVERIFY(lock != INVALID_HANDLE_VALUE);
        const auto unlock = qScopeGuard([&] { CloseHandle(lock); });
        out = dispatchTeraCopy(r, cancel, {}, dir.filePath("records"));
        QCOMPARE(out.state, OperationState::Failed); QCOMPARE(out.completedCount(), 0); QVERIFY(QDir(r.destination).isEmpty());
        QVERIFY(QFileInfo::exists(out.externalRecord)); // Attempt may be uncertain; keep its list.
        const auto json = QJsonDocument::fromJson(read(out.externalRecord)).object(); QVERIFY(QFileInfo::exists(json["list"].toString()));
    }
    void handoffPreservesUnicodeAndNeverClaimsCompletion() {
        QTemporaryDir dir; QVERIFY(fixture(dir.path())); ProgramRoots roots(dir.path());
        QVERIFY(QDir().mkpath(dir.filePath("TeraCopy")));
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), dir.filePath("TeraCopy/TeraCopy.exe")));
        auto r = request(dir.path());
        QVERIFY(write(dir.filePath("segundo.txt"), "second")); r.sources.append(dir.filePath("segundo.txt"));
        std::atomic_bool cancel{false}; const auto out = dispatchTeraCopy(r, cancel, {}, dir.filePath("records"));
        QCOMPARE(out.state, OperationState::Delegated); QCOMPARE(out.completedCount(), 0);
        for (const auto &item : out.items) QCOMPARE(item.state, OperationItemState::Delegated);
        const auto record = QJsonDocument::fromJson(read(out.externalRecord)).object();
        const auto listPath = record["list"].toString(); const auto list = read(listPath);
        QVERIFY(list.startsWith(QByteArray("\xff\xfe", 2)));
        QString decoded;
        for (qsizetype i = 2; i + 1 < list.size(); i += 2) decoded += QChar(quint16(quint8(list[i])) | quint16(quint8(list[i + 1])) << 8);
        QCOMPARE(decoded, QDir::toNativeSeparators(r.sources.first()) + "\r\n" + QDir::toNativeSeparators(r.sources.last()) + "\r\n");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(listPath + ".received.json"), 5000);
        const auto received = QJsonDocument::fromJson(read(listPath + ".received.json")).object();
        QCOMPARE(received["destination"].toString(), QDir::toNativeSeparators(r.destination));
        QCOMPARE(received["conflict"].toString(), QString("/RenameCopied")); QCOMPARE(received["window"].toString(), QString("/NoClose"));
        QVERIFY(QFileInfo::exists(r.sources.first())); QVERIFY(QDir(r.destination).isEmpty());
        QTest::qWait(200); // Let the owned probe exit before its temporary executable is removed.
    }
    void nestedLinksAndCancelledPreparationDoNotDispatch() {
        QTemporaryDir dir; QVERIFY(fixture(dir.path())); auto r = request(dir.path());
        QVERIFY(QDir().mkpath(dir.filePath("folder/sub"))); r.sources = {dir.filePath("folder")};
        std::atomic_bool cancel{false};
        auto out = dispatchTeraCopy(r, cancel, [&](quint32, quint32, const QString &) { cancel = true; }, dir.filePath("records"));
        QCOMPARE(out.state, OperationState::Cancelled); QVERIFY(!QFileInfo::exists(dir.filePath("records")));
        cancel = false;
        const auto junction = dir.filePath("folder/sub/link");
        QCOMPARE(QProcess::execute("cmd.exe", {"/d", "/c", "mklink", "/J", QDir::toNativeSeparators(junction), QDir::toNativeSeparators(dir.filePath("destino"))}), 0);
        const auto cleanup = qScopeGuard([&] { QDir().rmdir(junction); });
        QVERIFY(QFileInfo(junction).isJunction());
        out = dispatchTeraCopy(r, cancel, {}, dir.filePath("records"));
        QCOMPARE(out.state, OperationState::Failed); QVERIFY(out.error.contains("link", Qt::CaseInsensitive)); QVERIFY(!QFileInfo::exists(dir.filePath("records")));
    }
    void dialogAndSessionPreference() {
        QTemporaryDir dir; QVERIFY(fixture(dir.path())); auto r = request(dir.path());
        OperationDialog dialog(r); auto *engine = dialog.findChild<QComboBox *>("operationEngine"); QVERIFY(engine);
        QCOMPARE(dialog.request().engine, TransferEngine::TeraCopy);
        QVERIFY(!dialog.findChild<QComboBox *>("operationConflict")->isEnabled());
        engine->setCurrentIndex(0); QCOMPARE(dialog.request().engine, TransferEngine::Windows);
        QVERIFY(dialog.findChild<QComboBox *>("operationConflict")->isEnabled());
        r.kind = OperationKind::Move; r.engine = TransferEngine::Windows;
        OperationDialog moving(r); QVERIFY(!moving.findChild<QComboBox *>("operationEngine"));
        const auto sessionPath = dir.filePath("session.json");
        {
            MainWindow w(dir.path(), dir.path(), false, sessionPath);
            QVERIFY(!w.session().teraCopyForCopy);
            w.findChild<QAction *>("teraCopyForCopy")->setChecked(true); w.close();
        }
        MainWindow reopened({}, {}, false, sessionPath); QVERIFY(reopened.session().teraCopyForCopy);
        SessionStore store(sessionPath); const auto loaded = store.load(); QCOMPARE(loaded.state, SessionLoadState::Loaded);
        auto legacy = QJsonDocument::fromJson(read(sessionPath)).object(); legacy.remove("teraCopyForCopy");
        QVERIFY(write(sessionPath, QJsonDocument(legacy).toJson())); SessionStore oldStore(sessionPath); QVERIFY(!oldStore.load().session.teraCopyForCopy);
        legacy["teraCopyForCopy"] = "true"; QVERIFY(write(sessionPath, QJsonDocument(legacy).toJson()));
        QFile::remove(sessionPath + ".bak");
        SessionStore invalidStore(sessionPath); QCOMPARE(invalidStore.load().state, SessionLoadState::Invalid);
    }
    void realInstalledCopy() {
        if (!qEnvironmentVariableIsSet("DIROPEN_TEST_TERACOPY_REAL")) QSKIP("Provedor real requer DIROPEN_TEST_TERACOPY_REAL=1; inicia TeraCopy somente para fixtures.");
        QVERIFY(!teraCopyExecutable().isEmpty());
        const auto retainedRoot = qEnvironmentVariable("DIROPEN_TEST_TERACOPY_ROOT");
        QTemporaryDir dir(retainedRoot.isEmpty() ? QDir::tempPath() + "/teracopy-real-XXXXXX" : retainedRoot + "/real-XXXXXX");
        QVERIFY(dir.isValid()); if (!retainedRoot.isEmpty()) dir.setAutoRemove(false);
        QVERIFY(fixture(dir.path())); auto r = request(dir.path());
        qInfo().noquote() << "Fixture:" << dir.path();
        QVERIFY(write(dir.filePath("destino/ação 日本 & arquivo.txt"), "existing preserved"));
        QString deep = dir.filePath("pasta");
        for (int i = 0; i < 8; ++i) deep += "/subpasta_com_nome_longo_123456789";
        QVERIFY(QDir().mkpath(deep + "/vazia")); QVERIFY(write(deep + "/oculto.txt", "nested"));
        r.sources.append(dir.filePath("pasta"));
        std::atomic_bool cancel{false}; const auto out = dispatchTeraCopy(r, cancel, {}, dir.filePath("records"));
        QCOMPARE(out.state, OperationState::Delegated); QCOMPARE(out.completedCount(), 0);
        const auto targetDeep = r.destination + deep.mid(dir.path().size());
        QTRY_COMPARE_WITH_TIMEOUT(read(targetDeep + "/oculto.txt"), QByteArray("nested"), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(targetDeep + "/vazia").isDir(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(QDir(r.destination).entryList(QDir::Files).size() == 2, 10000);
        QCOMPARE(read(dir.filePath("destino/ação 日本 & arquivo.txt")), QByteArray("existing preserved"));
        bool matched = false;
        for (const auto &name : QDir(r.destination).entryList(QDir::Files))
            if (QCryptographicHash::hash(read(r.destination + '/' + name), QCryptographicHash::Sha256) == QCryptographicHash::hash(read(r.sources.first()), QCryptographicHash::Sha256)) matched = true;
        QVERIFY(matched); QVERIFY(QFileInfo::exists(r.sources.first())); QVERIFY(QFileInfo::exists(deep + "/oculto.txt"));
    }
    void realUiCopy() {
        if (!qEnvironmentVariableIsSet("DIROPEN_TEST_TERACOPY_REAL")) QSKIP("Interface com provedor real é opcional e usa somente fixtures.");
        QStandardPaths::setTestModeEnabled(true);
        const auto reset = qScopeGuard([] { QStandardPaths::setTestModeEnabled(false); });
        QTemporaryDir dir; QVERIFY(QDir().mkpath(dir.filePath("origem"))); QVERIFY(QDir().mkpath(dir.filePath("destino")));
        QVERIFY(write(dir.filePath("origem/relatório ação.txt"), "UI fixture one")); QVERIFY(write(dir.filePath("origem/foto.txt"), "UI fixture two"));
        MainWindow window(dir.filePath("origem"), dir.filePath("destino"), false); window.show();
        auto *view = window.leftPanel()->findChild<QTreeView *>("files"); QVERIFY(view);
        QTRY_COMPARE(window.leftPanel()->visibleCount(), 2); view->selectAll();
        bool confirmed = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<OperationDialog *>("operationDialog"); if (!dialog) return;
            auto *engine = dialog->findChild<QComboBox *>("operationEngine"); if (!engine) return;
            engine->setCurrentIndex(1);
            QApplication::processEvents();
            auto *hint = dialog->findChild<QLabel *>("operationHint");
            if (!hint || hint->height() < hint->heightForWidth(hint->width())) { dialog->reject(); return; }
            const auto capture = qEnvironmentVariable("DIROPEN_CAPTURE_TERACOPY");
            if (!capture.isEmpty() && QApplication::platformName() == "windows") dialog->grab().save(capture);
            confirmed = true; dialog->findChild<QPushButton *>("confirmOperation")->click();
        });
        window.findChild<QAction *>("copyFiles")->trigger(); QVERIFY(confirmed);
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QMessageBox *>("operationResult"), 10000);
        auto *message = window.findChild<QMessageBox *>("operationResult");
        QCOMPARE(message->windowTitle(), QString("Cópia enviada ao TeraCopy")); QVERIFY(message->text().contains("não foi confirmado"));
        QVERIFY(window.session().teraCopyForCopy); QVERIFY(!window.findChild<QAction *>("cancelOperation")->isVisible());
        QTRY_COMPARE_WITH_TIMEOUT(read(dir.filePath("destino/relatório ação.txt")), QByteArray("UI fixture one"), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(read(dir.filePath("destino/foto.txt")), QByteArray("UI fixture two"), 10000);
        QCOMPARE(read(dir.filePath("origem/relatório ação.txt")), QByteArray("UI fixture one"));
        message->close(); window.close();
    }
    void benchmarkTransfer() {
        const auto root = qEnvironmentVariable("DIROPEN_TRANSFER_BENCH_ROOT");
        if (root.isEmpty()) QSKIP("Benchmark opcional requer fixture marcada e motor escolhido.");
        QCOMPARE(read(root + "/.diropen-transfer-fixture"), QByteArray("Directory Open transfer performance fixture v1"));
        const auto method = qEnvironmentVariable("DIROPEN_TRANSFER_BENCH_ENGINE");
        QVERIFY(method == "windows" || method == "teracopy");
        const auto destination = root + '/' + qEnvironmentVariable("DIROPEN_TRANSFER_BENCH_TARGET");
        QVERIFY(destination != root + '/'); QVERIFY(!QFileInfo::exists(destination)); QVERIFY(QDir().mkdir(destination));
        OperationRequest r; r.sources = {root + "/source"}; r.destination = destination;
        r.nativeUi = false; r.conflict = OperationConflict::KeepBoth;
        r.engine = method == "teracopy" ? TransferEngine::TeraCopy : TransferEngine::Windows;
        QMap<QString, QByteArray> expected;
        QDirIterator entries(r.sources.first(), QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (entries.hasNext()) { const auto file = entries.next(); expected.insert(QDir(root).relativeFilePath(file), QCryptographicHash::hash(read(file), QCryptographicHash::Sha256)); }
        QVERIFY(!expected.isEmpty()); std::atomic_bool cancel{false}; QElapsedTimer timer; timer.start();
        const auto out = r.engine == TransferEngine::TeraCopy ? dispatchTeraCopy(r, cancel, {}, root + "/records") : performOperation(r, cancel);
        QVERIFY2(out.state == (method == "teracopy" ? OperationState::Delegated : OperationState::Completed), qPrintable(out.error));
        const auto dispatchMs = timer.elapsed();
        const auto matches = [&] {
            for (auto it = expected.cbegin(); it != expected.cend(); ++it)
                if (QCryptographicHash::hash(read(destination + '/' + it.key()), QCryptographicHash::Sha256) != it.value()) return false;
            return true;
        };
        QTRY_VERIFY_WITH_TIMEOUT(matches(), 45000);
        qInfo().noquote() << "method=" << method << "files=" << expected.size() << "dispatchMs=" << dispatchMs << "sha256EndpointMs=" << timer.elapsed();
    }
};
int main(int argc, char **argv) {
    if (argc > 1 && QByteArray(argv[1]) == "Copy") return receiveArguments(argc, argv);
    QApplication app(argc, argv); TeraCopyTest test; return QTest::qExec(&test, argc, argv);
}
#include "teracopy_test.moc"
