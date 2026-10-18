#include "archive.h"
#include "archivedialog.h"
#include "mainwindow.h"
#include "filepanel.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeView>

class ArchiveTest : public QObject {
    Q_OBJECT
    static bool write(const QString &path, const QByteArray &data) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(data) == data.size(); }
    static QByteArray read(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
    static QByteArray hash(const QString &path) { return QCryptographicHash::hash(read(path), QCryptographicHash::Sha256); }
    static QByteArray member(const QByteArray &name, const QByteArray &size = "3", const QByteArray &attributes = "A") {
        return "Path = " + name + "\nSize = " + size + "\nAttributes = " + attributes + "\nEncrypted = -\n\n";
    }
    static ArchiveResult run(ArchiveRequest request) {
        ArchiveJob job(request); job.start();
        if (!job.wait(30000)) { job.cancel(); job.wait(); ArchiveResult failed; failed.message = "Prazo da fixture excedido"; return failed; }
        return job.result();
    }
    static int cli(const QStringList &args, const QString &cwd = {}) {
        QProcess p; p.setProgram(sevenZipExecutable()); p.setArguments(args); if (!cwd.isEmpty()) p.setWorkingDirectory(cwd);
        p.start(); p.closeWriteChannel(); if (!p.waitForFinished(30000)) { p.kill(); p.waitForFinished(); return -1; } return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    }
    static ArchiveRequest creating(const QString &root, ArchiveFormat format = ArchiveFormat::Zip) {
        ArchiveRequest r; r.action = ArchiveAction::Create; r.sources = {root + "/source"}; r.format = format; r.output = root + (format == ArchiveFormat::Zip ? "/archive.zip" : "/archive.7z"); return r;
    }
private slots:
    void rarSignaturesAndUnsupportedCreationFailBeforePublishing(){
        QTemporaryDir root;QVERIFY(write(root.filePath("not-rar.rar"),"not a RAR file"));ArchiveRequest request;request.archive=root.filePath("not-rar.rar");request.action=ArchiveAction::Extract;request.output=root.filePath("output");const auto result=run(request);QVERIFY(result.state!=ArchiveState::Completed);QVERIFY(!QFileInfo::exists(request.output));
        request=creating(root.path(),ArchiveFormat::Rar);QVERIFY(!validateArchiveRequest(request).isEmpty());
        auto header=QByteArray::fromHex("526172211a070000007301000d00");QVERIFY(write(root.filePath("volume.rar"),header));request={};request.archive=root.filePath("volume.rar");const auto volume=run(request);QVERIFY(volume.state!=ArchiveState::Completed);QVERIFY(volume.message.contains("volumes"));
        auto fields=member("item");fields.replace("\n\n","\nSymbolic Link = \nHard Link = \nCopy Link = \nAlternate Stream = -\nSplit Before = -\nSplit After = -\nVolume Index = \n\n");QCOMPARE(parseArchiveListing(fields).state,ArchiveState::Completed);fields.replace("Split After = -","Split After = +");QVERIFY(parseArchiveListing(fields).state!=ArchiveState::Completed);
    }
    void realRarVariantsListTestAndExtractWithSourceUnchanged(){
        const auto fixtures=qEnvironmentVariable("DIROPEN_RAR_FIXTURES");if(fixtures.isEmpty())QSKIP("Fixtures RAR públicas opcionais; ver docs/COMPILAR.md.");
        const QStringList names{"test_read_format_rar_subblock.rar","test_read_format_rar5_stored.rar","test_read_format_rar5_multiple_files_solid.rar"};
        for(const auto &name:names){QTemporaryDir root;const auto source=root.filePath("demo.rar");QVERIFY(QFile::copy(QDir(fixtures).filePath(name),source));const auto before=read(source);ArchiveRequest request;request.archive=source;const auto listed=run(request);QVERIFY2(listed.state==ArchiveState::Completed,qPrintable(listed.message+listed.diagnostics));request.expectedIdentity=listed.identity;request.action=ArchiveAction::Test;QVERIFY2(run(request).state==ArchiveState::Completed,qPrintable(name));request.action=ArchiveAction::Extract;request.output=root.filePath("extracted");const auto extracted=run(request);QVERIFY2(extracted.state==ArchiveState::Completed,qPrintable(extracted.message+extracted.diagnostics));QCOMPARE(read(source),before);
            if(name.contains("subblock"))QCOMPARE(read(request.output+"/test.txt"),QByteArray("test text document\r\n"));else if(name.contains("stored"))QCOMPARE(read(request.output+"/helloworld.txt"),QByteArray("hello libarchive test suite!\n"));else for(int magic=1;magic<=4;++magic){QByteArray expected;for(int k=1;k<=1024;++k){const auto value=quint32(qMax(0,k*k-3*k+1+magic));for(int j=0;j<4;++j)expected.append(char((value>>(8*j))&255));}QCOMPARE(read(request.output+QString("/test%1.bin").arg(magic)),expected);}
        }
        for(const auto &name:{"test_read_format_rar.rar","test_read_format_rar5_multiarchive.part01.rar"}){QTemporaryDir root;const auto source=root.filePath("unsafe.rar");QVERIFY(QFile::copy(QDir(fixtures).filePath(name),source));ArchiveRequest request;request.archive=source;request.action=ArchiveAction::Extract;request.output=root.filePath("output");const auto result=run(request);QVERIFY2(result.state!=ArchiveState::Completed,qPrintable(result.message));QVERIFY(!QFileInfo::exists(request.output));}
    }
    void safeListingAndWindowsCollisions() {
        auto result = parseArchiveListing(member("pasta", "0", "D") + member("pasta/ação.txt"));
        QCOMPARE(result.state, ArchiveState::Completed); QCOMPARE(result.entries.size(), 2); QCOMPARE(result.totalBytes, quint64(3));
        const QStringList names{"../evil.txt", "x/../../evil.txt", "/absolute.txt", "C:/evil.txt", "//server/share/file", "file.txt:stream", "a//b", "folder/.", "folder/..", "folder/COM1.txt", "file.", "file ", "a?b", "a\nb", ""};
        for (const auto &name : names) { const auto bad = parseArchiveListing(member(name.toUtf8())); QVERIFY2(bad.state != ArchiveState::Completed, qPrintable(name)); }
        QCOMPARE(parseArchiveListing(member("file") + member("FILE")).state, ArchiveState::Failed);
        QCOMPARE(parseArchiveListing(member("file") + member("file/child")).state, ArchiveState::Failed);
        auto link = member("link"); link.replace("Attributes = A", "Attributes = A lrwxrwxrwx"); QCOMPARE(parseArchiveListing(link).state, ArchiveState::Failed);
        link = member("link"); link.replace("\n\n", "\nSymbolic Link = ../outside\n\n"); QCOMPARE(parseArchiveListing(link).state, ArchiveState::Failed);
    }
    void malformedListingDoesNotBecomeSuccess() {
        const QList<QByteArray> invalid{member("a", "-1"), member("a", "18446744073709551616"), member("a") + "unrecognized\n", "Path = a\nPath = b\n", "Path = a\n", member("a") + "Path = x\nSize = 2\nAttributes = A\nEncrypted = ?\n", member(QByteArray("\xff", 1)), QByteArray(8 * 1024 * 1024 + 1, 'a')};
        for (const auto &bytes : invalid) QVERIFY(parseArchiveListing(bytes).state != ArchiveState::Completed);
        QByteArray many; for (int i = 0; i < 10001; ++i) many += member(QByteArray::number(i), "0");
        QVERIFY(parseArchiveListing(many).state != ArchiveState::Completed);
        QCOMPARE(parseArchiveListing({}).state, ArchiveState::Completed); // Empty archives are valid.
    }
    void roundTrip_data() {
        QTest::addColumn<bool>("seven"); QTest::newRow("zip") << false; QTest::newRow("7z") << true;
    }
    void roundTrip() {
        if (sevenZipExecutable().isEmpty()) QSKIP("7-Zip real não instalado."); QFETCH(bool, seven);
        QTemporaryDir dir(QDir::tempPath() + "/DirOpen archive espaço-XXXXXX"); QVERIFY(dir.isValid());
        QVERIFY(QDir().mkpath(dir.filePath("source/中文/empty")));
        const QMap<QString, QByteArray> files{{"source/ação 日本.txt", "conteúdo UTF-8"}, {"source/@report.txt", "@literal"}, {"source/-switch.txt", "-literal"}, {"source/中文/data.bin", QByteArray(32000, '\x81')}, {"source/zero.txt", {}}};
        for (auto it = files.cbegin(); it != files.cend(); ++it) QVERIFY(write(dir.filePath(it.key()), it.value()));
        auto request = creating(dir.path(), seven ? ArchiveFormat::SevenZip : ArchiveFormat::Zip);
        const auto created = run(request); QVERIFY2(created.state == ArchiveState::Completed, qPrintable(created.message + created.diagnostics));
        QVERIFY(QFileInfo::exists(request.output)); const auto archiveHash = hash(request.output);
        ArchiveRequest browse; browse.archive = request.output; const auto listing = run(browse);
        QVERIFY2(listing.state == ArchiveState::Completed, qPrintable(listing.message + listing.diagnostics)); QCOMPARE(listing.entries.size(), files.size() + 3); QVERIFY(listing.identity.has_value());
        browse.expectedIdentity = listing.identity; browse.action = ArchiveAction::Test; QCOMPARE(run(browse).state, ArchiveState::Completed);
        browse.action = ArchiveAction::Extract; browse.output = dir.filePath("extracted");
        const auto extracted = run(browse); QVERIFY2(extracted.state == ArchiveState::Completed, qPrintable(extracted.message + extracted.diagnostics)); QCOMPARE(extracted.completedFiles, files.size());
        for (auto it = files.cbegin(); it != files.cend(); ++it) { QCOMPARE(read(browse.output + '/' + it.key()), it.value()); QCOMPARE(read(dir.filePath(it.key())), it.value()); }
        QVERIFY(QFileInfo(browse.output + "/source/中文/empty").isDir()); QCOMPARE(hash(request.output), archiveHash);
        QCOMPARE(run(browse).state, ArchiveState::Failed); // Never merge into an occupied destination.
        const auto duplicate = run(request); QCOMPARE(duplicate.state, ArchiveState::Failed); QCOMPARE(hash(request.output), archiveHash);
    }
    void realUnsafeZipMembers_data() {
        QTest::addColumn<QByteArray>("name");
        QTest::newRow("traversal") << QByteArray("../x/evil.txt");
        QTest::newRow("absolute") << QByteArray("C:/x/evil.txt");
        QTest::newRow("unc") << QByteArray("//xy/evil.txt");
        QTest::newRow("ads") << QByteArray("safe/evil:txt");
        QTest::newRow("device") << QByteArray("safe/COM1.txt");
    }
    void realUnsafeZipMembers() {
        if (sevenZipExecutable().isEmpty()) QSKIP("7-Zip real não instalado."); QFETCH(QByteArray, name);
        QTemporaryDir dir; QVERIFY(QDir().mkpath(dir.filePath("safe"))); QVERIFY(write(dir.filePath("safe/evil.txt"), "fixture"));
        const auto archive = dir.filePath("unsafe.zip");
        QCOMPARE(cli({"a", "-tzip", archive, "safe/evil.txt"}, dir.path()), 0);
        auto bytes = read(archive); QCOMPARE(name.size(), QByteArray("safe/evil.txt").size()); QVERIFY(bytes.count("safe/evil.txt") >= 2); bytes.replace("safe/evil.txt", name); QVERIFY(write(archive, bytes));
        ArchiveRequest r; r.action = ArchiveAction::Extract; r.archive = archive; r.output = dir.filePath("output");
        const auto result = run(r); QVERIFY2(result.state != ArchiveState::Completed, qPrintable(result.message)); QVERIFY(!QFileInfo::exists(r.output));
        QVERIFY(!QFileInfo::exists(dir.filePath("evil.txt"))); QCOMPARE(read(dir.filePath("safe/evil.txt")), QByteArray("fixture"));
    }
    void changedArchiveAndEncryptedHeaders() {
        if (sevenZipExecutable().isEmpty()) QSKIP("7-Zip real não instalado.");
        QTemporaryDir dir; QVERIFY(QDir().mkpath(dir.filePath("source"))); QVERIFY(write(dir.filePath("source/file.txt"), "data"));
        auto create = creating(dir.path()); QCOMPARE(run(create).state, ArchiveState::Completed);
        ArchiveRequest r; r.archive = create.output; const auto listed = run(r); QVERIFY(listed.identity.has_value());
        auto bytes = read(create.output); bytes += "changed"; QVERIFY(write(create.output, bytes));
        r.expectedIdentity = listed.identity; r.action = ArchiveAction::Extract; r.output = dir.filePath("output");
        QCOMPARE(run(r).state, ArchiveState::Failed); QVERIFY(!QFileInfo::exists(r.output));
        const auto encrypted = dir.filePath("encrypted.7z");
        QCOMPARE(cli({"a", "-t7z", "-pFixturePassword", "-mhe=on", encrypted, dir.filePath("source/file.txt")}), 0);
        r.archive = encrypted; r.expectedIdentity.reset(); const auto denied = run(r);
        QVERIFY2(denied.state != ArchiveState::Completed, qPrintable(denied.message)); QVERIFY(!QFileInfo::exists(r.output));
    }
    void corruptionAndDownloadOrigin() {
        if (sevenZipExecutable().isEmpty()) QSKIP("7-Zip real não instalado.");
        QTemporaryDir dir; QVERIFY(QDir().mkpath(dir.filePath("source")));
        const QByteArray payload("Unique CRC fixture: 0198273498732026"); QVERIFY(write(dir.filePath("source/file.txt"), payload));
        auto create = creating(dir.path()); QCOMPARE(run(create).state, ArchiveState::Completed);
        const QByteArray zone("[ZoneTransfer]\r\nZoneId=3\r\nHostUrl=https://example.invalid/demo.zip\r\n");
        QVERIFY(write(create.output + ":Zone.Identifier", zone));
        ArchiveRequest r; r.archive = create.output; r.action = ArchiveAction::Extract; r.output = dir.filePath("marked");
        const auto marked = run(r); QVERIFY2(marked.state == ArchiveState::Completed, qPrintable(marked.message));
        QCOMPARE(read(r.output + "/source/file.txt:Zone.Identifier"), zone); QCOMPARE(read(r.output + "/source/file.txt"), payload);
        const auto broken = dir.filePath("broken.zip");
        QCOMPARE(cli({"a", "-tzip", "-mx=0", broken, "source/file.txt"}, dir.path()), 0);
        auto bytes = read(broken); const auto offset = bytes.indexOf(payload); QVERIFY(offset >= 0); bytes[offset] = char(bytes[offset] ^ 1); QVERIFY(write(broken, bytes));
        r.archive = broken; r.action = ArchiveAction::Test; QVERIFY(run(r).state != ArchiveState::Completed);
        r.action = ArchiveAction::Extract; r.output = dir.filePath("corrupt-output");
        const auto corrupt = run(r); QVERIFY(corrupt.state != ArchiveState::Completed); QCOMPARE(corrupt.completedFiles, 0);
        QVERIFY(!QFileInfo::exists(r.output + "/source/file.txt"));
    }
    void cancellationAndPartialResults() {
        QTemporaryDir dir; QVERIFY(QDir().mkpath(dir.filePath("source"))); QVERIFY(write(dir.filePath("source/a.txt"), "first")); QVERIFY(write(dir.filePath("source/b.txt"), "second"));
        auto create = creating(dir.path()); std::atomic_bool cancel{true};
        QCOMPARE(performArchive(create, cancel).state, ArchiveState::Cancelled); QVERIFY(!QFileInfo::exists(create.output));
        if (sevenZipExecutable().isEmpty()) QSKIP("7-Zip real não instalado.");
        create.sources = {dir.filePath("source/a.txt"), dir.filePath("source/b.txt")}; QCOMPARE(run(create).state, ArchiveState::Completed);
        ArchiveRequest r; r.archive = create.output; r.action = ArchiveAction::Extract; r.output = dir.filePath("output"); cancel = false;
        const auto partial = performArchive(r, cancel, [&](int done, int, const QString &) { if (done == 1) cancel = true; });
        QCOMPARE(partial.state, ArchiveState::Cancelled); QCOMPARE(partial.completedFiles, 1);
        QCOMPARE(QDir(r.output).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).size(), 1); QCOMPARE(read(r.output + "/a.txt"), QByteArray("first"));
    }
    void sourceJunctionAndNewDestinations() {
        QTemporaryDir dir; QVERIFY(QDir().mkpath(dir.filePath("source/sub"))); QVERIFY(QDir().mkpath(dir.filePath("outside")));
        auto request = creating(dir.path()); request.output = dir.filePath("source/self.zip"); QVERIFY(!validateArchiveRequest(request).isEmpty());
        request.output = dir.filePath("existing.zip"); QVERIFY(write(request.output, "preserved")); QVERIFY(!validateArchiveRequest(request).isEmpty());
        request.output = dir.filePath("new.zip"); const auto junction = dir.filePath("source/sub/link");
        QCOMPARE(QProcess::execute("cmd.exe", {"/d", "/c", "mklink", "/J", QDir::toNativeSeparators(junction), QDir::toNativeSeparators(dir.filePath("outside"))}), 0);
        const auto cleanup = qScopeGuard([&] { QDir().rmdir(junction); });
        if (!sevenZipExecutable().isEmpty()) { const auto result = run(request); QCOMPARE(result.state, ArchiveState::Failed); QVERIFY(!QFileInfo::exists(request.output)); }
    }
    void missingProviderDoesNotCreateOutput() {
        QTemporaryDir dir; QVERIFY(QDir().mkpath(dir.filePath("source"))); QVERIFY(write(dir.filePath("source/a.txt"), "source"));
        const QList<QByteArray> keys{"ProgramW6432", "ProgramFiles", "ProgramFiles(x86)"};
        QList<QByteArray> values; QList<bool> existed;
        for (const auto &key : keys) { values.append(qgetenv(key)); existed.append(qEnvironmentVariableIsSet(key)); qputenv(key, dir.path().toUtf8()); }
        const auto restore = qScopeGuard([&] { for (int i = 0; i < keys.size(); ++i) { if (existed[i]) qputenv(keys[i], values[i]); else qunsetenv(keys[i]); } });
        const auto request = creating(dir.path()); const auto result = run(request);
        QCOMPARE(result.state, ArchiveState::Failed); QVERIFY(!result.message.isEmpty()); QVERIFY(!QFileInfo::exists(request.output)); QCOMPARE(read(dir.filePath("source/a.txt")), QByteArray("source"));
    }
    void realZipSymlinkAndDestinationReplacement() {
        if (sevenZipExecutable().isEmpty()) QSKIP("7-Zip real não instalado.");
        QTemporaryDir dir; QVERIFY(QDir().mkpath(dir.filePath("source"))); QVERIFY(QDir().mkpath(dir.filePath("outside")));
        QVERIFY(write(dir.filePath("source/link.txt"), "../outside")); auto create = creating(dir.path()); QCOMPARE(run(create).state, ArchiveState::Completed);
        auto bytes = read(create.output); const auto central = bytes.lastIndexOf(QByteArray("PK\1\2", 4)); QVERIFY(central >= 0);
        bytes[central + 5] = char(3); // Unix host; symlink mode in ZIP external attributes.
        const quint32 mode = quint32(0120777) << 16;
        for (int i = 0; i < 4; ++i) bytes[central + 38 + i] = char(mode >> (i * 8));
        const auto unsafe = dir.filePath("symlink.zip"); QVERIFY(write(unsafe, bytes));
        ArchiveRequest r; r.archive = unsafe; r.action = ArchiveAction::Extract; r.output = dir.filePath("output");
        const auto rejectedLink = run(r); QVERIFY(rejectedLink.state != ArchiveState::Completed); QVERIFY(rejectedLink.message.contains("Links")); QVERIFY(!QFileInfo::exists(r.output));
        r.archive = create.output; std::atomic_bool cancel{false}; bool replaced = false;
        const auto result = performArchive(r, cancel, [&](int done, int, const QString &) {
            if (done != 0 || replaced) return;
            if (!QDir().rmdir(r.output)) return;
            replaced = QProcess::execute("cmd.exe", {"/d", "/c", "mklink", "/J", QDir::toNativeSeparators(r.output), QDir::toNativeSeparators(dir.filePath("outside"))}) == 0;
        });
        const auto cleanup = qScopeGuard([&] { if (QFileInfo(r.output).isJunction()) QDir().rmdir(r.output); });
        QVERIFY(replaced); QVERIFY(result.state != ArchiveState::Completed); QVERIFY(QDir(dir.filePath("outside")).isEmpty());
    }
    void publicUiCreateListTestExtract() {
        if (sevenZipExecutable().isEmpty()) QSKIP("7-Zip real não instalado.");
        QTemporaryDir dir; QVERIFY(QDir().mkpath(dir.filePath("source"))); QVERIFY(QDir().mkpath(dir.filePath("target")));
        QVERIFY(write(dir.filePath("source/ação.txt"), "UI data"));
        const auto archive = dir.filePath("target/Demonstração.zip"); bool created = false, extracted = false;
        {
            MainWindow window(dir.filePath("source"), dir.filePath("target"), false); window.show();
            auto *view = window.leftPanel()->findChild<QTreeView *>("files"); QTRY_COMPARE(window.leftPanel()->visibleCount(), 1); view->selectAll();
            QTimer::singleShot(0, &window, [&] {
                auto *dialog = window.findChild<ArchiveDialog *>("archiveDialog"); if (!dialog) return;
                const auto close = qScopeGuard([&] { dialog->findChild<QPushButton *>("archiveClose")->click(); });
                dialog->findChild<QLineEdit *>("archiveOutput")->setText(archive);
                dialog->findChild<QPushButton *>("archiveApply")->click();
                QTRY_VERIFY_WITH_TIMEOUT(dialog->findChild<QLabel *>("archiveStatus")->text().contains("aprovado"), 15000);
                created = QFileInfo::exists(archive);
            });
            window.findChild<QAction *>("createArchive")->trigger(); QVERIFY(created);
        }
        {
            MainWindow window(dir.filePath("target"), dir.filePath("source"), false); window.show();
            auto *view = window.leftPanel()->findChild<QTreeView *>("files"); QTRY_COMPARE(window.leftPanel()->visibleCount(), 1); view->selectAll();
            QTimer::singleShot(0, &window, [&] {
                auto *dialog = window.findChild<ArchiveDialog *>("archiveDialog"); if (!dialog) return;
                const auto close = qScopeGuard([&] { dialog->findChild<QPushButton *>("archiveClose")->click(); });
                auto *test = dialog->findChild<QPushButton *>("archiveTest"); QTRY_VERIFY_WITH_TIMEOUT(test->isEnabled(), 15000);
                QCOMPARE(dialog->findChild<QTreeWidget *>("archiveEntries")->topLevelItemCount(), 1);
                test->click(); QTRY_VERIFY_WITH_TIMEOUT(dialog->findChild<QLabel *>("archiveStatus")->text().contains("aprovado"), 15000);
                const auto capture = qEnvironmentVariable("DIROPEN_CAPTURE_ARCHIVE");
                if (!capture.isEmpty() && QApplication::platformName() == "windows") dialog->grab().save(capture);
                dialog->findChild<QLineEdit *>("archiveOutput")->setText(dir.filePath("extracted")); dialog->findChild<QPushButton *>("archiveApply")->click();
                QTRY_VERIFY_WITH_TIMEOUT(dialog->findChild<QLabel *>("archiveStatus")->text().contains("Extração concluída"), 15000);
                extracted = read(dir.filePath("extracted/ação.txt")) == QByteArray("UI data");
            });
            window.findChild<QAction *>("browseArchive")->trigger(); QVERIFY(extracted);
        }
    }
    void benchmarkWorkflow() {
        const auto root = qEnvironmentVariable("DIROPEN_ARCHIVE_BENCH_ROOT");
        if (root.isEmpty()) QSKIP("Benchmark opcional requer fixture marcada.");
        QCOMPARE(read(root + "/.diropen-archive-fixture"), QByteArray("Directory Open archive performance fixture v1"));
        const auto method = qEnvironmentVariable("DIROPEN_ARCHIVE_BENCH_ENGINE"); QVERIFY(method == "diropen" || method == "cli");
        const auto task = root + '/' + qEnvironmentVariable("DIROPEN_ARCHIVE_BENCH_TASK"); QVERIFY(!QFileInfo::exists(task)); QVERIFY(QDir().mkdir(task));
        const auto archive = task + "/archive.zip", output = task + "/extracted";
        QMap<QString, QByteArray> expected; QDirIterator files(root + "/source", QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) { const auto path = files.next(); expected.insert(QDir(root).relativeFilePath(path), hash(path)); }
        QVERIFY(!expected.isEmpty()); QElapsedTimer timer; timer.start();
        if (method == "diropen") {
            auto create = creating(root); create.output = archive; const auto created = run(create);
            QVERIFY2(created.state == ArchiveState::Completed, qPrintable(created.message + created.diagnostics));
            ArchiveRequest r; r.archive = archive; const auto listed = run(r); QCOMPARE(listed.state, ArchiveState::Completed);
            r.expectedIdentity = listed.identity; r.action = ArchiveAction::Test; QCOMPARE(run(r).state, ArchiveState::Completed);
            r.action = ArchiveAction::Extract; r.output = output; const auto extracted = run(r);
            QVERIFY2(extracted.state == ArchiveState::Completed, qPrintable(extracted.message + extracted.diagnostics));
        } else {
            QCOMPARE(cli({"a", "-tzip", "-mx=5", "-mmt=2", "-scsUTF-8", "-spd", "-r-", archive, "source"}, root), 0);
            QCOMPARE(cli({"t", "--", archive}), 0);
            QCOMPARE(cli({"l", "-slt", "-ba", "--", archive}), 0);
            QCOMPARE(cli({"l", "-slt", "-ba", "--", archive}), 0); QCOMPARE(cli({"t", "--", archive}), 0);
            QCOMPARE(cli({"l", "-slt", "-ba", "--", archive}), 0);
            QCOMPARE(cli({"x", "-y", "-o" + output, "--", archive}), 0); // Only trusted generated fixtures, never a product path.
        }
        for (auto it = expected.cbegin(); it != expected.cend(); ++it) QCOMPARE(hash(output + '/' + it.key()), it.value());
        qInfo().noquote() << "method=" << method << "files=" << expected.size() << "workflowMs=" << timer.elapsed() << "archiveBytes=" << QFileInfo(archive).size();
    }
};
QTEST_MAIN(ArchiveTest)
#include "archive_test.moc"
