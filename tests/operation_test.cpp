#include "fileoperation.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <memory>

class OperationTest : public QObject {
    Q_OBJECT
private:
    std::unique_ptr<QTemporaryDir> m_fixture;
    QString source, target;
    QVector<OperationItemResult> recycled;
    void write(const QString &path, const QByteArray &data) {
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(data), qint64(data.size()));
    }
    QByteArray hash(const QString &path) {
        QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
        QCryptographicHash sha(QCryptographicHash::Sha256); if (!sha.addData(&file)) return {}; return sha.result();
    }
    OperationRequest request(QStringList paths, OperationConflict policy = OperationConflict::KeepBoth) {
        OperationRequest req; req.sources = paths; req.destination = target; req.conflict = policy; req.nativeUi = false; return req;
    }
    OperationResult run(OperationRequest req, OperationKind kind, const QString &name = {}) {
        req.kind = kind; req.name = name; std::atomic_bool cancel{false};
        auto result = performOperation(req, cancel);
        if (kind == OperationKind::Trash) for (const auto &item : result.items) if (!item.recycledParsingName.isEmpty()) recycled.append(item);
        return result;
    }
    bool restoreOwned(const OperationItemResult &receipt) {
        // Restore only receipts returned for this test's owned temporary tree.
        if (!receipt.source.startsWith(m_fixture->path() + '/', Qt::CaseInsensitive) || QFileInfo::exists(receipt.source)) return false;
        const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (FAILED(initialized)) return false;
        const bool restored = [&] {
            using Microsoft::WRL::ComPtr;
            ComPtr<IFileOperation> op; ComPtr<IShellItem> item, parent;
            HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&op));
            if (SUCCEEDED(hr)) hr = op->SetOperationFlags(FOF_SILENT | FOF_NOERRORUI | FOFX_EARLYFAILURE | FOF_NOCONFIRMMKDIR);
            if (SUCCEEDED(hr)) hr = SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(receipt.recycledParsingName.utf16()), nullptr, IID_PPV_ARGS(&item));
            const auto parentPath = QDir::toNativeSeparators(QFileInfo(receipt.source).absolutePath());
            if (SUCCEEDED(hr)) hr = SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(parentPath.utf16()), nullptr, IID_PPV_ARGS(&parent));
            const auto name = QFileInfo(receipt.source).fileName();
            if (SUCCEEDED(hr)) hr = op->MoveItem(item.Get(), parent.Get(), reinterpret_cast<LPCWSTR>(name.utf16()), nullptr);
            if (SUCCEEDED(hr)) hr = op->PerformOperations();
            BOOL aborted = TRUE; if (SUCCEEDED(hr)) hr = op->GetAnyOperationsAborted(&aborted);
            return SUCCEEDED(hr) && !aborted && QFileInfo::exists(receipt.source);
        }();
        CoUninitialize(); return restored;
    }
private slots:
    void init() {
        m_fixture = std::make_unique<QTemporaryDir>(); QVERIFY(m_fixture->isValid());
        source = m_fixture->path() + "/source"; target = m_fixture->path() + "/target";
        QVERIFY(QDir().mkdir(source)); QVERIFY(QDir().mkdir(target));
    }
    void cleanup() {
        while (!recycled.isEmpty()) {
            if (!restoreOwned(recycled.first())) {
                m_fixture->setAutoRemove(false);
                QFAIL(qPrintable("Dados de teste preservados em " + m_fixture->path() + "; item na Lixeira: " + recycled.first().recycledParsingName));
            }
            recycled.removeFirst();
        }
        m_fixture.reset();
    }
    void copiesFilesAndPreservesOriginalContent() {
        write(source + "/one.txt", "original\r\n"); write(source + "/empty.txt", {});
        const auto before = hash(source + "/one.txt"); std::atomic_bool cancel{false};
        const auto result = performOperation(request({source + "/one.txt", source + "/empty.txt"}), cancel);
        QCOMPARE(result.state, OperationState::Completed); QCOMPARE(result.completedCount(), 2);
        QCOMPARE(hash(target + "/one.txt"), before); QCOMPARE(hash(source + "/one.txt"), before);
        QVERIFY(QFileInfo::exists(target + "/empty.txt"));
    }
    void copiesNestedFoldersAndEmptyFolders() {
        QVERIFY(QDir(source).mkpath("bundle/nested")); QVERIFY(QDir(source).mkpath("bundle/empty"));
        write(source + "/bundle/nested/file.txt", "nested content");
        std::atomic_bool cancel{false}; const auto result = performOperation(request({source + "/bundle"}), cancel);
        QCOMPARE(result.state, OperationState::Completed); QCOMPARE(result.completedCount(), 1);
        QCOMPARE(hash(target + "/bundle/nested/file.txt"), hash(source + "/bundle/nested/file.txt"));
        QVERIFY(QFileInfo(target + "/bundle/empty").isDir());
    }
    void keepBothPreservesExistingDestination() {
        write(source + "/same.txt", "new"); write(target + "/same.txt", "old");
        const auto previous = hash(target + "/same.txt"); std::atomic_bool cancel{false};
        const auto result = performOperation(request({source + "/same.txt"}), cancel);
        QCOMPARE(result.state, OperationState::Completed); QCOMPARE(result.completedCount(), 1);
        QCOMPARE(hash(target + "/same.txt"), previous);
        QVERIFY(!result.items[0].target.isEmpty()); QVERIFY(result.items[0].target != target + "/same.txt");
        QCOMPARE(hash(result.items[0].target), hash(source + "/same.txt"));
        QCOMPARE(QDir(target).entryList(QDir::Files).size(), 2);
    }
    void keepBothPreservesExistingFolderTree() {
        QVERIFY(QDir(source).mkdir("bundle")); QVERIFY(QDir(target).mkdir("bundle"));
        write(source + "/bundle/file.txt", "new"); write(target + "/bundle/file.txt", "old");
        std::atomic_bool cancel{false}; const auto result = performOperation(request({source + "/bundle"}), cancel);
        QCOMPARE(result.state, OperationState::Completed);
        QCOMPARE(hash(target + "/bundle/file.txt"), QCryptographicHash::hash("old", QCryptographicHash::Sha256));
        QVERIFY(result.items[0].target != target + "/bundle");
        QCOMPARE(hash(result.items[0].target + "/file.txt"), hash(source + "/bundle/file.txt"));
    }
    void supportsUnicodeAndPreservesModificationTime() {
        const auto name = QString::fromUtf8("ação e fotos.txt"); write(source + '/' + name, "utf8 content");
        QFile file(source + '/' + name); QVERIFY(file.open(QIODevice::ReadWrite));
        const auto date = QDateTime::fromString("2020-02-03T04:05:06Z", Qt::ISODate);
        QVERIFY(file.setFileTime(date, QFileDevice::FileModificationTime)); file.close();
        std::atomic_bool cancel{false}; const auto result = performOperation(request({source + '/' + name}), cancel);
        QCOMPARE(result.state, OperationState::Completed);
        QCOMPARE(hash(source + '/' + name), hash(target + '/' + name));
        QCOMPARE(QFileInfo(target + '/' + name).lastModified().toUTC(), date);
        QCOMPARE(QFileInfo(source + '/' + name).lastModified().toUTC(), date);
    }
    void cancelledBeforeStartDoesNotCreateFiles() {
        write(source + "/one.txt", "original"); std::atomic_bool cancel{true};
        const auto result = performOperation(request({source + "/one.txt"}), cancel);
        QCOMPARE(result.state, OperationState::Cancelled); QCOMPARE(result.completedCount(), 0);
        QVERIFY(QDir(target).isEmpty()); QVERIFY(QFileInfo::exists(source + "/one.txt"));
    }
    void cancellationDuringBatchReportsPartialOutcome() {
        write(source + "/a.txt", "first"); write(source + "/b.txt", "second"); write(source + "/c.txt", "third");
        std::atomic_bool cancel{false};
        const auto result = performOperation(request({source + "/a.txt", source + "/b.txt", source + "/c.txt"}), cancel,
            [&](quint32, quint32, const QString &item) { if (item.endsWith("/b.txt")) cancel.store(true); });
        QCOMPARE(result.state, OperationState::Cancelled);
        QVERIFY(QFileInfo::exists(target + "/a.txt")); QVERIFY(!QFileInfo::exists(target + "/c.txt"));
        QVERIFY(result.completedCount() < 3);
        QCOMPARE(hash(source + "/a.txt"), hash(target + "/a.txt"));
        QVERIFY(QFileInfo::exists(source + "/b.txt")); QVERIFY(QFileInfo::exists(source + "/c.txt"));
    }
    void missingSourceDoesNotPartiallyCopyOtherItems() {
        write(source + "/one.txt", "original"); std::atomic_bool cancel{false};
        const auto result = performOperation(request({source + "/one.txt", source + "/missing.txt"}), cancel);
        QCOMPARE(result.state, OperationState::Failed); QVERIFY(!result.error.isEmpty()); QVERIFY(QDir(target).isEmpty());
    }
    void rejectsDestinationInsideSourceAndSameFolder() {
        QVERIFY(QDir(source).mkdir("nested"));
        auto req = request({source}); req.destination = source + "/nested";
        QVERIFY(!validateOperationRequest(req).isEmpty());
        write(source + "/one.txt", "original"); req.sources = {source + "/one.txt"}; req.destination = source;
        QVERIFY(!validateOperationRequest(req).isEmpty());
    }
    void rejectsFileAsDestination() {
        write(source + "/one.txt", "original"); write(target + "/file.txt", "old");
        auto req = request({source + "/one.txt"}); req.destination = target + "/file.txt";
        std::atomic_bool cancel{false}; QCOMPARE(performOperation(req, cancel).state, OperationState::Failed);
        QCOMPARE(hash(target + "/file.txt"), QCryptographicHash::hash("old", QCryptographicHash::Sha256));
    }
    void lockedSourceProducesErrorAndPreservesDestination() {
        write(source + "/locked.txt", "original");
        const auto path = QDir::toNativeSeparators(source + "/locked.txt");
        HANDLE locked = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(locked != INVALID_HANDLE_VALUE); std::atomic_bool cancel{false};
        const auto result = performOperation(request({source + "/locked.txt"}), cancel); CloseHandle(locked);
        QCOMPARE(result.state, OperationState::Failed); QCOMPARE(result.completedCount(), 0);
        QVERIFY(!result.error.isEmpty() || !result.items[0].error.isEmpty());
        QCOMPARE(hash(source + "/locked.txt"), QCryptographicHash::hash("original", QCryptographicHash::Sha256));
    }
    void junctionInSourceIsRejectedBeforeAnyWrites() {
        const auto protectedFolder = m_fixture->path() + "/protected"; QVERIFY(QDir().mkdir(protectedFolder));
        write(protectedFolder + "/original.txt", "preserved");
        QVERIFY(QDir(source).mkdir("bundle"));
        const auto junction = source + "/bundle/link";
        const int created = QProcess::execute("cmd.exe", {"/d", "/c", "mklink", "/J", QDir::toNativeSeparators(junction), QDir::toNativeSeparators(protectedFolder)});
        QCOMPARE(created, 0); QVERIFY(QFileInfo(junction).isJunction());
        std::atomic_bool cancel{false}; const auto result = performOperation(request({source + "/bundle"}), cancel);
        QCOMPARE(result.state, OperationState::Failed); QVERIFY(QDir(target).isEmpty());
        QCOMPARE(hash(protectedFolder + "/original.txt"), QCryptographicHash::hash("preserved", QCryptographicHash::Sha256));
        QVERIFY(QDir().rmdir(junction)); // Remove the owned junction before fixture cleanup.
    }
    void movesFilesAndFoldersWithVerifiedContent() {
        write(source + "/one.txt", "move content"); QVERIFY(QDir(source).mkpath("bundle/empty"));
        write(source + "/bundle/nested.txt", "nested"); const auto before = hash(source + "/one.txt");
        const auto result = run(request({source + "/one.txt", source + "/bundle"}), OperationKind::Move);
        QCOMPARE(result.state, OperationState::Completed); QCOMPARE(result.completedCount(), 2);
        QVERIFY(!QFileInfo::exists(source + "/one.txt")); QVERIFY(!QFileInfo::exists(source + "/bundle"));
        QCOMPARE(hash(target + "/one.txt"), before); QVERIFY(QFileInfo(target + "/bundle/empty").isDir());
        QCOMPARE(hash(target + "/bundle/nested.txt"), QCryptographicHash::hash("nested", QCryptographicHash::Sha256));
    }
    void moveKeepBothDoesNotReplaceDestination() {
        write(source + "/same.txt", "moving"); write(target + "/same.txt", "existing"); const auto before = hash(source + "/same.txt");
        const auto result = run(request({source + "/same.txt"}), OperationKind::Move);
        QCOMPARE(result.state, OperationState::Completed); QVERIFY(!QFileInfo::exists(source + "/same.txt"));
        QCOMPARE(hash(target + "/same.txt"), QCryptographicHash::hash("existing", QCryptographicHash::Sha256));
        QVERIFY(result.items[0].target != target + "/same.txt"); QCOMPARE(hash(result.items[0].target), before);
    }
    void cancelledMovePreservesPendingSources() {
        write(source + "/a.txt", "a"); write(source + "/b.txt", "b");
        auto req = request({source + "/a.txt", source + "/b.txt"}); req.kind = OperationKind::Move; std::atomic_bool cancel{false};
        const auto result = performOperation(req, cancel, [&](quint32, quint32, const QString &item) { if (item.endsWith("/b.txt")) cancel.store(true); });
        QCOMPARE(result.state, OperationState::Cancelled); QCOMPARE(result.completedCount(), 1);
        QVERIFY(QFileInfo::exists(target + "/a.txt")); QVERIFY(!QFileInfo::exists(source + "/a.txt"));
        QVERIFY(QFileInfo::exists(source + "/b.txt")); QVERIFY(!QFileInfo::exists(target + "/b.txt"));
    }
    void lockedMoveKeepsOriginal() {
        write(source + "/locked.txt", "locked"); const auto path = QDir::toNativeSeparators(source + "/locked.txt");
        const auto locked = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(locked != INVALID_HANDLE_VALUE); const auto result = run(request({source + "/locked.txt"}), OperationKind::Move); CloseHandle(locked);
        QCOMPARE(result.state, OperationState::Failed); QVERIFY(QDir(target).isEmpty());
        QCOMPARE(hash(source + "/locked.txt"), QCryptographicHash::hash("locked", QCryptographicHash::Sha256));
    }
    void renamesUnicodeFileAndFolder() {
        write(source + "/one.txt", "preserved"); const auto before = hash(source + "/one.txt");
        auto result = run(request({source + "/one.txt"}), OperationKind::Rename, "ação.txt");
        QCOMPARE(result.state, OperationState::Completed); QVERIFY(!QFileInfo::exists(source + "/one.txt")); QCOMPARE(hash(source + "/ação.txt"), before);
        QVERIFY(QDir(source).mkpath("folder/empty"));
        result = run(request({source + "/folder"}), OperationKind::Rename, "Fotos");
        QCOMPARE(result.state, OperationState::Completed); QVERIFY(QFileInfo(source + "/Fotos/empty").isDir()); QVERIFY(!QFileInfo::exists(source + "/folder"));
    }
    void renameOnlyCaseChanges() {
        write(source + "/sample.txt", "case");
        const auto result = run(request({source + "/sample.txt"}), OperationKind::Rename, "Sample.txt");
        QCOMPARE(result.state, OperationState::Completed);
        QCOMPARE(QDir(source).entryList(QDir::Files), QStringList{"Sample.txt"});
    }
    void renameRejectsCollisionWithoutReplacingEitherFile() {
        write(source + "/one.txt", "one"); write(source + "/two.txt", "two");
        const auto result = run(request({source + "/one.txt"}), OperationKind::Rename, "two.txt");
        QCOMPARE(result.state, OperationState::Failed); QCOMPARE(hash(source + "/one.txt"), QCryptographicHash::hash("one", QCryptographicHash::Sha256));
        QCOMPARE(hash(source + "/two.txt"), QCryptographicHash::hash("two", QCryptographicHash::Sha256));
    }
    void invalidNamesCannotEscapeFolderOrCreateReservedItems() {
        for (const auto &name : QStringList{"", ".", "..", "../outside", "a/b", "a\\b", "C:bad", "bad*", "bad?", "trail.", "trail ", "CON", "nul.txt", "COM1.log", "LPT².txt", QString(256, 'x')}) {
            auto req = request({}); req.kind = OperationKind::CreateFolder; req.name = name;
            std::atomic_bool cancel{false}; const auto result = performOperation(req, cancel);
            QCOMPARE(result.state, OperationState::Failed); QVERIFY2(!result.error.isEmpty(), qPrintable(name));
        }
        QVERIFY(QDir(target).isEmpty()); QVERIFY(validateItemName("Fotos de ação").isEmpty());
    }
    void createsFolderAndRejectsExistingName() {
        auto req = request({}); auto result = run(req, OperationKind::CreateFolder, "Fotos de ação");
        QCOMPARE(result.state, OperationState::Completed); QCOMPARE(result.completedCount(), 1); QVERIFY(QFileInfo(target + "/Fotos de ação").isDir());
        write(target + "/existing.txt", "preserved");
        result = run(req, OperationKind::CreateFolder, "existing.txt"); QCOMPARE(result.state, OperationState::Failed);
        QCOMPARE(hash(target + "/existing.txt"), QCryptographicHash::hash("preserved", QCryptographicHash::Sha256));
    }
    void cancelledNamedOperationsDoNotWrite() {
        write(source + "/one.txt", "one"); std::atomic_bool cancel{true};
        auto req = request({source + "/one.txt"}); req.kind = OperationKind::Rename; req.name = "new.txt";
        QCOMPARE(performOperation(req, cancel).state, OperationState::Cancelled); QVERIFY(QFileInfo::exists(source + "/one.txt"));
        req.kind = OperationKind::CreateFolder; req.sources.clear(); req.name = "New";
        QCOMPARE(performOperation(req, cancel).state, OperationState::Cancelled); QVERIFY(QDir(target).isEmpty());
    }
    void recyclesOwnedFileAndFolderAndRestoresThemInCleanup() {
        write(source + "/ação.txt", "recycled content"); QVERIFY(QDir(source).mkpath("bundle/empty")); write(source + "/bundle/one.txt", "nested");
        const auto fileHash = hash(source + "/ação.txt");
        const auto result = run(request({source + "/ação.txt", source + "/bundle"}), OperationKind::Trash);
        QCOMPARE(result.state, OperationState::Completed); QCOMPARE(result.completedCount(), 2); QCOMPARE(recycled.size(), 2);
        QVERIFY(!QFileInfo::exists(source + "/ação.txt")); QVERIFY(!QFileInfo::exists(source + "/bundle"));
        // Verify recoverability now; cleanup remains responsible if an assertion fails.
        while (!recycled.isEmpty()) { QVERIFY(restoreOwned(recycled.first())); recycled.removeFirst(); }
        QCOMPARE(hash(source + "/ação.txt"), fileHash); QVERIFY(QFileInfo(source + "/bundle/empty").isDir());
        QCOMPARE(hash(source + "/bundle/one.txt"), QCryptographicHash::hash("nested", QCryptographicHash::Sha256));
    }
    void trashCancellationKeepsPendingItemsAndReturnsReceipts() {
        write(source + "/a.txt", "a"); write(source + "/b.txt", "b");
        auto req = request({source + "/a.txt", source + "/b.txt"}); req.kind = OperationKind::Trash; std::atomic_bool cancel{false};
        const auto result = performOperation(req, cancel, [&](quint32, quint32, const QString &item) { if (item.endsWith("/b.txt")) cancel.store(true); });
        for (const auto &item : result.items) if (!item.recycledParsingName.isEmpty()) recycled.append(item);
        QCOMPARE(result.state, OperationState::Cancelled); QCOMPARE(result.completedCount(), 1); QCOMPARE(recycled.size(), 1);
        QVERIFY(!QFileInfo::exists(source + "/a.txt")); QVERIFY(QFileInfo::exists(source + "/b.txt"));
    }
    void trashMissingInputDoesNotRecycleOtherItems() {
        write(source + "/one.txt", "preserved");
        const auto result = run(request({source + "/one.txt", source + "/missing"}), OperationKind::Trash);
        QCOMPARE(result.state, OperationState::Failed); QVERIFY(recycled.isEmpty()); QVERIFY(QFileInfo::exists(source + "/one.txt"));
    }
    void lockedTrashKeepsOriginalAndHasNoReceipt() {
        write(source + "/locked.txt", "locked"); const auto path = QDir::toNativeSeparators(source + "/locked.txt");
        const auto locked = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(locked != INVALID_HANDLE_VALUE); const auto result = run(request({source + "/locked.txt"}), OperationKind::Trash); CloseHandle(locked);
        QCOMPARE(result.state, OperationState::Failed); QVERIFY(recycled.isEmpty());
        QCOMPARE(hash(source + "/locked.txt"), QCryptographicHash::hash("locked", QCryptographicHash::Sha256));
    }

};
QTEST_GUILESS_MAIN(OperationTest)
#include "operation_test.moc"
