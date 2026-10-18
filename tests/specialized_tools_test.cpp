#include "specializedtools.h"
#include "toolsdialog.h"
#include "mainwindow.h"
#include "sessionstore.h"
#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <QUuid>
#include <windows.h>

class SpecializedToolsTest : public QObject {
    Q_OBJECT
    static bool write(const QString &p, const QByteArray &bytes) { QFile f(p); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(); }
    static QByteArray read(const QString &p) { QFile f(p); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
    static ToolResult run(const ToolRequest &r) { std::atomic_bool cancel{false}; return performTool(r, cancel); }
    static QString findNamed(const QString &root, const QString &name) { QDirIterator it(root, QStringList{name}, QDir::Files, QDirIterator::Subdirectories); return it.hasNext() ? it.next() : QString(); }
    static QString guiTemplate() { const auto p = QDir::current().filePath(".cache/tools-demo"); QDir().mkpath(p); return p + "/demo-XXXXXX"; }
    ToolRequest backupRequest(const QTemporaryDir &dir, ToolAction action) {
        ToolRequest r; r.executable = discoverTool("restic"); r.action = action; r.repository = dir.filePath("repository"); r.password = "fixture-password-not-personal"; return r;
    }
private slots:
    void validationAndNoSideEffects() {
        QTemporaryDir dir; ToolRequest r; r.executable = QCoreApplication::applicationFilePath(); r.action = ToolAction::ContentSearch;
        r.root = dir.path(); r.pattern = "--version"; QVERIFY(validateToolRequest(r).isEmpty());
        r.pattern = QString(QChar(0)); QVERIFY(!validateToolRequest(r).isEmpty());
        r = backupRequest(dir, ToolAction::Restore); r.executable = QCoreApplication::applicationFilePath(); r.snapshot = QString(64, 'a'); r.destination = dir.path();
        QVERIFY(!validateToolRequest(r).isEmpty());
        r.action = ToolAction::Backup; r.sources = {dir.path()}; QVERIFY(!validateToolRequest(r).isEmpty());
        r.action = ToolAction::RemoteList; r.config = dir.filePath("config"); QVERIFY(write(r.config, "[local]\ntype = local\n"));
        r.source = ":sftp,host=example,password=secret:path"; QVERIFY(!validateToolRequest(r).isEmpty());
        r.source = "--config=evil"; QVERIFY(!validateToolRequest(r).isEmpty());
        r.source = "C:relative"; QVERIFY(!validateToolRequest(r).isEmpty());
        r.source = "Nuvem+ação@local:pasta"; QVERIFY(validateToolRequest(r).isEmpty());
    }
    void sessionPreferencesRoundTripAndLegacy() {
        QTemporaryDir dir; const auto p = dir.filePath("session.json"); SessionStore store(p); store.load();
        WindowSession s; s.left.tabs = {{dir.path(), {}}}; s.right.tabs = s.left.tabs;
        s.tools = {"C:/Tools/restic.exe", "C:/Tools/rclone.exe", "C:/Tools/rg.exe", "C:/Configs/rclone.conf", "D:/Backups"};
        QVERIFY(store.save(s).isEmpty()); SessionStore reload(p); auto loaded = reload.load(); QCOMPARE(loaded.state, SessionLoadState::Loaded); QCOMPARE(loaded.session.tools.repository, s.tools.repository);
        QVERIFY(!read(p).contains("password")); auto json = QJsonDocument::fromJson(read(p)).object(); json.remove("tools"); QVERIFY(write(p, QJsonDocument(json).toJson()));
        SessionStore legacy(p); QCOMPARE(legacy.load().session.tools.restic, QString());
        QFile::remove(p + ".bak"); json["tools"] = QJsonObject{{"restic", 42}}; QVERIFY(write(p, QJsonDocument(json).toJson())); SessionStore invalid(p); QCOMPARE(invalid.load().state, SessionLoadState::Invalid);
    }
    void cancellationBeforeStartCreatesNothing() {
        if (discoverTool("restic").isEmpty()) QSKIP("restic não instalado");
        QTemporaryDir dir; auto r = backupRequest(dir, ToolAction::InitBackup); std::atomic_bool cancel{true};
        QCOMPARE(performTool(r, cancel).state, ToolState::Cancelled); QVERIFY(!QFileInfo::exists(r.repository));
    }
    void malformedProtocolAndOwnedChildAreNotCompletion() {
        QTemporaryDir dir; const auto old = qgetenv("DIROPEN_TEST_TOOL_MODE");
        auto restore = qScopeGuard([&] { if(old.isNull()) qunsetenv("DIROPEN_TEST_TOOL_MODE"); else qputenv("DIROPEN_TEST_TOOL_MODE", old); });
        ToolRequest r; r.executable = QCoreApplication::applicationFilePath(); r.root = dir.path(); r.pattern = "fixture";
        qputenv("DIROPEN_TEST_TOOL_MODE", "malformed"); QCOMPARE(run(r).state, ToolState::Incomplete);
        qputenv("DIROPEN_TEST_TOOL_MODE", "child"); const auto pidFile = dir.filePath("owned-child.pid"); qputenv("DIROPEN_TEST_TOOL_PID", pidFile.toUtf8());
        const auto cleanup = qScopeGuard([] { qunsetenv("DIROPEN_TEST_TOOL_PID"); });
        QCOMPARE(run(r).state, ToolState::Incomplete);
        const auto pid = read(pidFile).trimmed().toULong(); QVERIFY(pid > 0);
        const auto handle = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (handle) { QCOMPARE(WaitForSingleObject(handle, 0), DWORD(WAIT_OBJECT_0)); CloseHandle(handle); }
    }
    void cancellationWhileRunningAndDialogCloseAwaitTermination() {
        QTemporaryDir dir; qputenv("DIROPEN_TEST_TOOL_MODE", "wait"); const auto restore = qScopeGuard([] { qunsetenv("DIROPEN_TEST_TOOL_MODE"); });
        ToolRequest r; r.executable = QCoreApplication::applicationFilePath(); r.root = dir.path(); r.pattern = "fixture";
        ToolJob job(r); job.start(); QTest::qWait(200); QVERIFY(job.isRunning()); job.cancel(); QVERIFY(job.wait(5000)); QCOMPARE(job.result().state, ToolState::Cancelled);
        ToolSettings s; s.ripgrep = r.executable; ToolsDialog dialog(ToolPage::Content, s, dir.path(), {}); dialog.show();
        dialog.findChild<QLineEdit *>("contentPattern")->setText("fixture"); auto *button = dialog.findChild<QPushButton *>("runContentSearch");
        QTest::mouseClick(button, Qt::LeftButton); QTest::qWait(200); QVERIFY(!button->isEnabled()); dialog.close(); QTRY_VERIFY_WITH_TIMEOUT(!dialog.isVisible(), 5000);
    }
    void realContentSearchOptionsUnicodeAndNoMatches() {
        if (discoverTool("rg").isEmpty()) QSKIP("ripgrep não instalado");
        QTemporaryDir dir; QVERIFY(write(dir.filePath("ação.txt"), "prefix\nNeedle [literal]\n"));
        QVERIFY(write(dir.filePath("ignored.txt"), "Needle [literal]\n")); QVERIFY(write(dir.filePath(".ignore"), "ignored.txt\n")); QVERIFY(write(dir.filePath(".hidden.txt"), "Needle [literal]\n"));
        ToolRequest r; r.executable = discoverTool("rg"); r.root = dir.path(); r.pattern = "Needle [literal]";
        const auto old = qgetenv("RIPGREP_CONFIG_PATH"); qputenv("RIPGREP_CONFIG_PATH", dir.filePath("absent").toUtf8()); auto env = qScopeGuard([&] { if(old.isNull()) qunsetenv("RIPGREP_CONFIG_PATH"); else qputenv("RIPGREP_CONFIG_PATH", old); });
        auto a = run(r); QVERIFY2(a.state == ToolState::Completed, qPrintable(a.message + a.diagnostics)); QCOMPARE(a.rows.size(), 1); QCOMPARE(a.rows.first().number, 2);
        r.hidden = true; r.ignored = true; a = run(r); QCOMPARE(a.state, ToolState::Completed); QCOMPARE(a.rows.size(), 3);
        r.pattern = "ABSENT"; a = run(r); QCOMPARE(a.state, ToolState::Completed); QVERIFY(a.rows.isEmpty());
        r.regex = true; r.pattern = "["; a = run(r); QCOMPARE(a.state, ToolState::Failed); QVERIFY(!a.diagnostics.isEmpty());
        r.regex = false; r.pattern = "--version"; a = run(r); QCOMPARE(a.state, ToolState::Completed); QVERIFY(a.rows.isEmpty());
    }
    void realContentLimitPreservesPartialResults() {
        if (discoverTool("rg").isEmpty()) QSKIP("ripgrep não instalado");
        QTemporaryDir dir; QVERIFY(write(dir.filePath("many.txt"), QByteArray("needle\n").repeated(10050)));
        ToolRequest r; r.executable = discoverTool("rg"); r.root = dir.path(); r.pattern = "needle";
        const auto a = run(r); QCOMPARE(a.state, ToolState::Incomplete); QCOMPARE(a.rows.size(), 10000);
    }
    void realVersionedBackupRestoreAndPasswordFailure() {
        if (discoverTool("restic").isEmpty()) QSKIP("restic não instalado");
        QTemporaryDir dir(guiTemplate()); QVERIFY(QDir().mkdir(dir.filePath("source"))); const auto file = dir.filePath("source/ação.txt"); QVERIFY(write(file, "first version\n"));
        auto r = backupRequest(dir, ToolAction::InitBackup); auto a = run(r); QVERIFY2(a.state == ToolState::Completed, qPrintable(a.message + a.diagnostics));
        r.action = ToolAction::Snapshots; a = run(r); QCOMPARE(a.state, ToolState::Completed); QVERIFY(a.rows.isEmpty());
        r.action = ToolAction::Backup; r.sources = {dir.filePath("source")}; a = run(r); QVERIFY2(a.state == ToolState::Completed, qPrintable(a.message + a.diagnostics));
        r.action = ToolAction::Snapshots; a = run(r); QCOMPARE(a.state, ToolState::Completed); QCOMPARE(a.rows.size(), 1); const auto first = a.rows.first().key;
        QVERIFY(write(file, "second version, changed\n")); r.action = ToolAction::Backup; a = run(r); QCOMPARE(a.state, ToolState::Completed);
        r.action = ToolAction::Snapshots; a = run(r); QCOMPARE(a.state, ToolState::Completed); QCOMPARE(a.rows.size(), 2);
        r.action = ToolAction::Restore; r.snapshot = first; r.destination = dir.filePath("restored"); a = run(r); QVERIFY2(a.state == ToolState::Completed, qPrintable(a.message + a.diagnostics));
        const auto restored = findNamed(r.destination, "ação.txt"); QVERIFY(!restored.isEmpty()); QCOMPARE(read(restored), QByteArray("first version\n")); QCOMPARE(read(file), QByteArray("second version, changed\n"));
        a = run(r); QCOMPARE(a.state, ToolState::Failed); QCOMPARE(read(restored), QByteArray("first version\n"));
        r.action = ToolAction::Snapshots; r.password = "wrong-password"; a = run(r); QCOMPARE(a.state, ToolState::Failed); QVERIFY(!a.diagnostics.contains("wrong-password"));
        ToolSettings s; s.repository = r.repository; ToolsDialog dialog(ToolPage::Backup, s, dir.path(), {dir.filePath("source")}); dialog.show();
        dialog.findChild<QLineEdit *>("backupPassword")->setText("fixture-password-not-personal");
        auto *list = dialog.findChild<QPushButton *>("listSnapshots"); QTest::mouseClick(list, Qt::LeftButton); QTRY_VERIFY_WITH_TIMEOUT(list->isEnabled(), 10000);
        auto *tree = dialog.findChild<QTreeWidget *>("toolResults"); QCOMPARE(tree->topLevelItemCount(), 2);
        for (int i=0; i<tree->topLevelItemCount(); ++i) if (tree->topLevelItem(i)->data(0, Qt::UserRole).toString() == first) tree->setCurrentItem(tree->topLevelItem(i));
        if (qEnvironmentVariableIsSet("DIROPEN_CAPTURE_TOOLS")) QVERIFY(dialog.grab().save(qEnvironmentVariable("DIROPEN_CAPTURE_TOOLS") + "/versioned-backups.png"));
        dialog.findChild<QLineEdit *>("restoreDestination")->setText(dir.filePath("ui-restored"));
        QTimer::singleShot(0, [&] { auto *box = dialog.findChild<QMessageBox *>(); if (box && box->text().contains(first)) box->button(QMessageBox::Yes)->click(); });
        auto *restoreButton = dialog.findChild<QPushButton *>("restoreSnapshot"); QTest::mouseClick(restoreButton, Qt::LeftButton); QTRY_VERIFY_WITH_TIMEOUT(restoreButton->isEnabled(), 15000);
        QCOMPARE(read(findNamed(dir.filePath("ui-restored"), "ação.txt")), QByteArray("first version\n"));
    }
    void realRemoteLocalBackendPreviewCopyConflictAndReceive() {
        if (discoverTool("rclone").isEmpty()) QSKIP("rclone não instalado");
        QTemporaryDir dir; QVERIFY(QDir().mkpath(dir.filePath("source/empty"))); QVERIFY(QDir().mkdir(dir.filePath("remote"))); QVERIFY(write(dir.filePath("source/ação.txt"), "fixture data\n"));
        ToolRequest r; r.executable = discoverTool("rclone"); r.config = dir.filePath("rclone.conf"); QVERIFY(write(r.config, "[fixture]\ntype = local\n"));
        r.action = ToolAction::Remotes; auto a = run(r); QVERIFY2(a.state == ToolState::Completed, qPrintable(a.message+a.diagnostics)); QCOMPARE(a.rows.size(), 1); QCOMPARE(a.rows.first().key, "fixture:");
        r.source = dir.filePath("source"); r.destination = "fixture:" + dir.filePath("remote"); r.action = ToolAction::TransferPreview;
        a = run(r); QCOMPARE(a.state, ToolState::Completed); QVERIFY(!QFileInfo::exists(dir.filePath("remote/ação.txt"))); QVERIFY(a.diagnostics.contains("ação.txt"));
        QVERIFY(!a.diagnostics.contains("\"stats\":"));
        QVERIFY(a.diagnostics.contains("Simulados:")); QVERIFY(!a.diagnostics.contains("Transferidos:"));
        r.action = ToolAction::TransferCopy; a = run(r); QVERIFY2(a.state == ToolState::Completed, qPrintable(a.message+a.diagnostics)); QCOMPARE(read(dir.filePath("remote/ação.txt")), QByteArray("fixture data\n")); QVERIFY(QFileInfo(dir.filePath("remote/empty")).isDir());
        r.action = ToolAction::RemoteList; r.source = r.destination; a = run(r); QCOMPARE(a.state, ToolState::Completed); QCOMPARE(a.rows.size(), 2);
        r.action = ToolAction::TransferCopy; r.source = dir.filePath("source"); QVERIFY(write(dir.filePath("source/ação.txt"), "different and longer contents\n")); a = run(r); QCOMPARE(a.state, ToolState::Incomplete); QCOMPARE(read(dir.filePath("remote/ação.txt")), QByteArray("fixture data\n"));
        r.source = r.destination; r.destination = dir.filePath("received"); a = run(r); QCOMPARE(a.state, ToolState::Completed); QCOMPARE(read(dir.filePath("received/ação.txt")), QByteArray("fixture data\n"));
    }
    void realDialogSearchAndOpenSignal() {
        if (discoverTool("rg").isEmpty()) QSKIP("ripgrep não instalado");
        QTemporaryDir dir(guiTemplate()); QVERIFY(write(dir.filePath("demo.txt"), "needle here\n")); ToolsDialog dialog(ToolPage::Content, {}, dir.path(), {}); dialog.show();
        auto *pattern = dialog.findChild<QLineEdit *>("contentPattern"); auto *button = dialog.findChild<QPushButton *>("runContentSearch"); auto *tree = dialog.findChild<QTreeWidget *>("toolResults");
        pattern->setText("needle"); QTest::mouseClick(button, Qt::LeftButton); QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 10000); QCOMPARE(tree->topLevelItemCount(), 1);
        QSignalSpy opened(&dialog, &ToolsDialog::openFileRequested); tree->setCurrentItem(tree->topLevelItem(0)); emit tree->itemDoubleClicked(tree->topLevelItem(0), 0); QCOMPARE(opened.count(), 1); QCOMPARE(opened.first().first().toString(), dir.filePath("demo.txt"));
        if (qEnvironmentVariableIsSet("DIROPEN_CAPTURE_TOOLS")) QVERIFY(dialog.grab().save(qEnvironmentVariable("DIROPEN_CAPTURE_TOOLS") + "/content-search.png"));
    }
    void publicMenusAndRemotePreviewInvalidation() {
        QTemporaryDir dir(guiTemplate()); MainWindow window(dir.path(), dir.path(), false); window.show();
        for (const auto &name : {"searchContent", "versionedBackup", "remoteStorage"}) {
            auto *action = window.findChild<QAction *>(name); QVERIFY(action); QVERIFY(action->isEnabled()); bool seen = false;
            QTimer::singleShot(0, [&] { if (auto *dialog = window.findChild<ToolsDialog *>()) { seen = true; dialog->close(); } }); action->trigger(); QVERIFY(seen);
        }
        if (discoverTool("rclone").isEmpty()) QSKIP("rclone não instalado");
        const auto config = dir.filePath("rclone.conf"); QVERIFY(write(config, "[fixture]\ntype = local\n"));
        QVERIFY(QDir().mkdir(dir.filePath("source"))); QVERIFY(QDir().mkdir(dir.filePath("destination"))); QVERIFY(write(dir.filePath("source/demo.txt"), "fixture"));
        ToolSettings s; s.rcloneConfig = config; ToolsDialog dialog(ToolPage::Remote, s, dir.filePath("source"), {}); dialog.show();
        dialog.findChild<QLineEdit *>("remotePath")->setText("fixture:"+dir.filePath("destination")); auto *preview = dialog.findChild<QPushButton *>("previewTransfer"); auto *copy = dialog.findChild<QPushButton *>("copyRemote"); QVERIFY(!copy->isEnabled());
        QTest::mouseClick(preview, Qt::LeftButton); QTRY_VERIFY_WITH_TIMEOUT(preview->isEnabled(), 10000); QVERIFY2(copy->isEnabled(), qPrintable(dialog.findChild<QLabel *>("toolStatus")->text()));
        dialog.findChild<QLineEdit *>("remotePath")->setText("fixture:other"); QVERIFY(!copy->isEnabled());
        dialog.findChild<QLineEdit *>("remotePath")->setText("fixture:"+dir.filePath("destination"));
        QTest::mouseClick(preview, Qt::LeftButton); QTRY_VERIFY_WITH_TIMEOUT(copy->isEnabled(), 10000);
        if (qEnvironmentVariableIsSet("DIROPEN_CAPTURE_TOOLS")) QVERIFY(dialog.grab().save(qEnvironmentVariable("DIROPEN_CAPTURE_TOOLS") + "/remote-storage.png"));
        QTimer::singleShot(0, [&] { auto *box = dialog.findChild<QMessageBox *>(); if (box && box->text().contains("fixture:")) box->button(QMessageBox::Yes)->click(); });
        QTest::mouseClick(copy, Qt::LeftButton); QTRY_VERIFY_WITH_TIMEOUT(preview->isEnabled(), 15000); QCOMPARE(read(dir.filePath("destination/demo.txt")), QByteArray("fixture"));
    }
    void benchmarkWorkflow() {
        const auto root = qEnvironmentVariable("DIROPEN_TOOLS_BENCH_ROOT");
        if (root.isEmpty()) QSKIP("Benchmark opcional, somente fixture marcada.");
        QVERIFY(read(root + "/.diropen-tools-fixture") == "Directory Open tools benchmark v1");
        const auto scenario = qEnvironmentVariable("DIROPEN_TOOLS_BENCH_SCENARIO");
        const auto method = qEnvironmentVariable("DIROPEN_TOOLS_BENCH_ENGINE");
        QVERIFY(method == "cli" || method == "diropen"); QVERIFY(scenario == "content" || scenario == "backup" || scenario == "remote");
        const auto task = root + "/" + QUuid::createUuid().toString(QUuid::WithoutBraces); QVERIFY(QDir().mkdir(task));
        const auto source = root + "/source"; const auto sourceParent = QFileInfo(source).absolutePath();
        const auto password = QString("benchmark-fixture-password");
        auto cli = [&](const QString &exe, const QStringList &args, const QString &cwd = QString()) {
            QProcess p; auto env = QProcessEnvironment::systemEnvironment();
            for (const auto &key : env.keys()) if (key.startsWith("RESTIC_") || key.startsWith("RCLONE_") || key == "RIPGREP_CONFIG_PATH") env.remove(key);
            env.insert("RESTIC_PASSWORD", password); env.insert("GOMAXPROCS", "2"); p.setProcessEnvironment(env);
            if (!cwd.isEmpty()) p.setWorkingDirectory(cwd); p.start(exe, args); p.closeWriteChannel();
            if (!p.waitForFinished(30000)) { p.kill(); p.waitForFinished(); return QByteArray("TASK_FAILED"); }
            if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) return QByteArray("TASK_FAILED:") + p.readAllStandardError();
            return p.readAllStandardOutput();
        };
        if (scenario == "content") {
            ToolRequest r; r.executable = discoverTool("rg"); r.root = source; r.pattern = "needle-fixture";
            if (method == "diropen") { const auto a = run(r); QCOMPARE(a.state, ToolState::Completed); QCOMPARE(a.rows.size(), 200); }
            else { const auto b = cli(r.executable, {"--no-config", "--json", "--threads", "2", "--max-filesize", "64M", "--color", "never", "--ignore-case", "--fixed-strings", "--", r.pattern, r.root});
                QVERIFY(!b.startsWith("TASK_FAILED")); int count=0; for(const auto &line : b.split('\n')) if(QJsonDocument::fromJson(line).object()["type"].toString()=="match") ++count; QCOMPARE(count, 200); }
            return;
        }
        QString destination;
        if (scenario == "backup") {
            ToolRequest r; r.executable = discoverTool("restic"); r.repository = task + "/repository"; r.password = password; r.sources = {source}; r.destination = task + "/restored";
            r.action = ToolAction::InitBackup;
            if (method == "diropen") {
                auto a = run(r); QVERIFY2(a.state == ToolState::Completed, qPrintable(a.message+a.diagnostics)); r.action = ToolAction::Backup; a = run(r); QCOMPARE(a.state, ToolState::Completed);
                r.action = ToolAction::Snapshots; a = run(r); QCOMPARE(a.state, ToolState::Completed); QCOMPARE(a.rows.size(), 1); r.snapshot = a.rows.first().key;
                r.action = ToolAction::Restore; a = run(r); QVERIFY2(a.state == ToolState::Completed, qPrintable(a.message+a.diagnostics));
            } else {
                const QStringList prefix{"--repo", r.repository, "--json", "--no-cache"};
                QVERIFY(!cli(r.executable, prefix + QStringList{"init"}).startsWith("TASK_FAILED"));
                QVERIFY(!cli(r.executable, prefix + QStringList{"backup", "--", "source"}, sourceParent).startsWith("TASK_FAILED"));
                const auto snapshots = QJsonDocument::fromJson(cli(r.executable, prefix + QStringList{"snapshots"})).array(); QCOMPARE(snapshots.size(), 1); r.snapshot = snapshots.first().toObject()["id"].toString();
                QVERIFY(QDir().mkdir(r.destination)); QVERIFY(!cli(r.executable, prefix + QStringList{"restore", r.snapshot, "--target", r.destination, "--verify", "--overwrite", "never"}).startsWith("TASK_FAILED"));
            }
            destination = r.destination + "/source";
        } else {
            ToolRequest r; r.executable = discoverTool("rclone"); r.config = root + "/rclone.conf"; r.source = source; r.destination = "fixture:" + task + "/remote";
            if (method == "diropen") {
                r.action = ToolAction::TransferPreview; QCOMPARE(run(r).state, ToolState::Completed); r.action = ToolAction::TransferCopy; QCOMPARE(run(r).state, ToolState::Completed);
                r.action = ToolAction::RemoteList; r.source = r.destination; const auto a = run(r); QCOMPARE(a.state, ToolState::Completed); QCOMPARE(a.rows.size(), 200);
            } else {
                const QStringList prefix{"--config", r.config, "--ask-password=false"};
                const auto copy = prefix + QStringList{"copy", r.source, r.destination, "--immutable", "--create-empty-src-dirs", "--transfers", "2", "--checkers", "2", "--retries", "1", "--use-json-log", "--stats", "1s", "--stats-log-level", "NOTICE"};
                QVERIFY(!cli(r.executable, copy + QStringList{"--dry-run", "--combined", "-"}).startsWith("TASK_FAILED"));
                QVERIFY(!cli(r.executable, copy).startsWith("TASK_FAILED")); const auto array = QJsonDocument::fromJson(cli(r.executable, prefix + QStringList{"lsjson", r.destination, "--no-mimetype"})).array(); QCOMPARE(array.size(), 200);
            }
            destination = task + "/remote";
        }
        const auto files = QDir(source).entryList(QDir::Files); QCOMPARE(files.size(), 200);
        for (const auto &name : files) QCOMPARE(QCryptographicHash::hash(read(destination+"/"+name), QCryptographicHash::Sha256), QCryptographicHash::hash(read(source+"/"+name), QCryptographicHash::Sha256));
    }
};
int main(int argc, char **argv) {
    if (argc > 1 && QByteArray(argv[1]) == "--owned-helper") { Sleep(30000); return 0; }
    const auto mode = qgetenv("DIROPEN_TEST_TOOL_MODE");
    if (!mode.isEmpty()) {
        QCoreApplication app(argc, argv); QFile out; if (!out.open(stdout, QIODevice::WriteOnly)) return 3;
        if (mode == "malformed") { out.write("not-json\n"); out.flush(); return 0; }
        if (mode == "wait") { Sleep(30000); return 0; }
        if (mode == "child") {
            qint64 pid = 0;
            if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), {"--owned-helper"}, {}, &pid)) return 1;
            QFile f(QString::fromUtf8(qgetenv("DIROPEN_TEST_TOOL_PID"))); if (!f.open(QIODevice::WriteOnly)) return 2; f.write(QByteArray::number(pid)); f.close();
            out.write("{\"type\":\"summary\",\"data\":{}}\n"); out.flush(); return 0;
        }
    }
    QApplication app(argc, argv); SpecializedToolsTest tests; return QTest::qExec(&tests, argc, argv);
}
#include "specialized_tools_test.moc"
