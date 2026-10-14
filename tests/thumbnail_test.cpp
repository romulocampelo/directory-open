#include "mainwindow.h"
#include "filepanel.h"
#include "filelistview.h"
#include "thumbnailcache.h"
#include "imagepreview.h"
#include "operationdialog.h"
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QMenu>
#include <QCryptographicHash>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QImageReader>
#include <QPainter>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <memory>

class ThumbnailTest final : public QObject {
    Q_OBJECT
private:
    std::unique_ptr<QMimeData> originalClipboard;
    QAction *action(FilePanel *panel, const char *name) { return panel->findChild<QAction *>(name); }
    QListView *grid(FilePanel *panel) { return panel->findChild<QListView *>("fileThumbnails"); }
    QByteArray bytes(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
    void write(const QString &path, const QByteArray &content) { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(content), qint64(content.size())); }
    void image(const QString &path, const QColor &color, QSize size = {600, 400}) { QImage bitmap(size, QImage::Format_RGB32); bitmap.fill(color); QVERIFY(bitmap.save(path)); }
    QModelIndex index(FilePanel *panel, const QString &name) {
        auto *view = panel->filesView();
        for (int row = 0; row < view->model()->rowCount(view->rootIndex()); ++row) {
            auto item = view->model()->index(row, 0, view->rootIndex()); if (item.data().toString() == name) return item;
        }
        return {};
    }
    void select(FilePanel *panel, const QString &name) {
        const auto item = index(panel, name); QVERIFY(item.isValid());
        panel->filesView()->selectionModel()->setCurrentIndex(item, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
    const ThumbnailResult *result(ThumbnailCache &cache, const QString &path) {
        const auto source = thumbnailSource(QFileInfo(path)); return source ? cache.find(*source) : nullptr;
    }
    void closeResult(MainWindow &window) { for (auto *message : window.findChildren<QMessageBox *>("operationResult")) message->close(); }
private slots:
    void initTestCase() {
        QImageReader::setAllocationLimit(64); originalClipboard = std::make_unique<QMimeData>();
        const auto *mime = qApp->clipboard()->mimeData();
        if (mime) for (const auto &format : mime->formats()) originalClipboard->setData(format, mime->data(format));
    }
    void cleanupTestCase() { qApp->clipboard()->setMimeData(originalClipboard.release()); }
    void init() { qApp->clipboard()->clear(); }
    void gridLoadsImagesPreservesNamesSelectionAndFiles() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); image(fixture.path() + "/Vermelho.png", Qt::red);
        image(fixture.path() + "/Azul.jpg", Qt::blue); write(fixture.path() + "/Anotações.txt", "hello");
        QVERIFY(QDir(fixture.path()).mkdir("Pasta")); write(fixture.path() + "/Corrompido.png", "invalid");
        const auto original = QCryptographicHash::hash(bytes(fixture.path() + "/Vermelho.png"), QCryptographicHash::Sha256);
        MainWindow window(fixture.path(), fixture.path(), false); window.show(); auto *panel = window.leftPanel();
        QTRY_COMPARE(panel->visibleCount(), 5); select(panel, "Azul.jpg");
        panel->filesView()->selectionModel()->select(index(panel, "Vermelho.png"), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        const auto selected = panel->selectedPaths(); action(panel, "viewThumbnails")->trigger();
        QCOMPARE(panel->filesView(), grid(panel)); QCOMPARE(panel->selectedPaths(), selected); QVERIFY(grid(panel)->isVisible());
        auto *cache = window.findChild<ThumbnailCache *>("thumbnailCache"); QVERIFY(cache);
        QTRY_VERIFY(result(*cache, fixture.path() + "/Vermelho.png")); QTRY_VERIFY(result(*cache, fixture.path() + "/Azul.jpg"));
        QTRY_VERIFY(result(*cache, fixture.path() + "/Corrompido.png"));
        QCOMPARE(result(*cache, fixture.path() + "/Vermelho.png")->image.pixelColor(20, 20), QColor(Qt::red));
        QVERIFY(!result(*cache, fixture.path() + "/Corrompido.png")->error.isEmpty());
        QCOMPARE(result(*cache, fixture.path() + "/Vermelho.png")->image.size(), QSize(256, 170));
        QVERIFY(!thumbnailSource(QFileInfo(fixture.path() + "/Anotações.txt"))); QVERIFY(!thumbnailSource(QFileInfo(fixture.path() + "/Pasta")));
        action(panel, "thumbnailSize256")->trigger(); QCOMPARE(grid(panel)->iconSize(), QSize(256, 256));
        action(panel, "viewNames")->trigger(); QVERIFY(!grid(panel)->isVisible()); QCOMPARE(panel->selectedPaths(), selected);
        QCOMPARE(QCryptographicHash::hash(bytes(fixture.path() + "/Vermelho.png"), QCryptographicHash::Sha256), original);
        QCOMPARE(QDir(fixture.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size(), 5);
    }
    void selectedTileRemainsVisibleWhenGridLosesFocus() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); image(fixture.path() + "/a.png", Qt::red); image(fixture.path() + "/b.png", Qt::blue);
        MainWindow window(fixture.path(), fixture.path(), false); window.show(); auto *panel = window.leftPanel();
        QTRY_COMPARE(panel->visibleCount(), 2); action(panel, "viewThumbnails")->trigger(); select(panel, "a.png");
        panel->findChild<QLineEdit *>("address")->setFocus(); QTRY_VERIFY(panel->findChild<QLineEdit *>("address")->hasFocus());
        auto *view = grid(panel); const auto selectedRect = view->visualRect(index(panel, "a.png")), otherRect = view->visualRect(index(panel, "b.png"));
        QVERIFY(view->viewport()->rect().contains(selectedRect.topLeft() + QPoint(5, 5)));
        QVERIFY(view->viewport()->rect().contains(otherRect.topLeft() + QPoint(5, 5)));
        const auto pixmap = view->viewport()->grab(); const auto bitmap = pixmap.toImage();
        const auto pixel = [&](const QPoint &point) { return bitmap.pixelColor(qRound(point.x() * pixmap.devicePixelRatio()), qRound(point.y() * pixmap.devicePixelRatio())); };
        QVERIFY(pixel(selectedRect.topLeft() + QPoint(5, 5)) != pixel(otherRect.topLeft() + QPoint(5, 5)));
        QCOMPARE(panel->selectedPaths().size(), 1);
    }
    void savedGridIsSharedAndRestoredWithoutLeakingAcrossFolders() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); QVERIFY(QDir(fixture.path()).mkdir("files")); QVERIFY(QDir(fixture.path()).mkdir("other"));
        const auto folder = fixture.path() + "/files", other = fixture.path() + "/other", session = fixture.path() + "/session.json";
        image(folder + "/A.png", Qt::red); image(folder + "/B.png", Qt::blue);
        {
            MainWindow window(folder, folder, false, session); window.show(); auto *panel = window.leftPanel();
            QTRY_COMPARE(panel->visibleCount(), 2); action(panel, "thumbnailSize192")->trigger(); action(panel, "viewDescending")->trigger();
            select(panel, "A.png"); action(panel, "rememberFolderView")->trigger();
            QCOMPARE(window.rightPanel()->filesView(), grid(window.rightPanel())); QCOMPARE(grid(window.rightPanel())->iconSize(), QSize(192, 192));
            QCOMPARE(panel->selectedPath(), folder + "/A.png");
            QVERIFY(panel->navigate(other)); QVERIFY(panel->filesView() != grid(panel)); panel->back(); QCOMPARE(panel->filesView(), grid(panel));
            panel->newTab(other); QVERIFY(panel->filesView() != grid(panel)); panel->closeTab(1); QCOMPARE(panel->filesView(), grid(panel));
            QVERIFY(window.close());
        }
        MainWindow reopened({}, {}, false, session); reopened.show();
        for (auto *panel : {reopened.leftPanel(), reopened.rightPanel()}) {
            QCOMPARE(panel->filesView(), grid(panel)); QCOMPARE(grid(panel)->iconSize(), QSize(192, 192));
            QTRY_COMPARE(panel->visibleCount(), 2); QCOMPARE(grid(panel)->model()->index(0, 0, grid(panel)->rootIndex()).data().toString(), QString("B.png"));
        }
        action(reopened.leftPanel(), "forgetFolderView")->trigger(); QVERIFY(reopened.leftPanel()->filesView() != grid(reopened.leftPanel()));
        QVERIFY(reopened.rightPanel()->filesView() != grid(reopened.rightPanel())); QVERIFY(reopened.close());
    }
    void filteredGridRoutesPreviewAndSkipsFolders() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); image(fixture.path() + "/a.png", Qt::red); image(fixture.path() + "/b.png", Qt::blue);
        QVERIFY(QDir(fixture.path()).mkdir("a-folder")); write(fixture.path() + "/c.txt", "plain text");
        MainWindow window(fixture.path(), fixture.path(), false); window.show(); auto *panel = window.leftPanel();
        QTRY_COMPARE(panel->visibleCount(), 4); action(panel, "viewThumbnails")->trigger();
        panel->setFilter(".png"); QTRY_COMPARE(panel->visibleCount(), 2); select(panel, "a.png"); panel->selectAdjacentFile(1);
        QCOMPARE(panel->selectedPath(), fixture.path() + "/b.png"); QCOMPARE(panel->selectedPaths().size(), 1);
        panel->setFilter({}); panel->selectAdjacentFile(1); QCOMPARE(panel->selectedPath(), fixture.path() + "/c.txt");
        window.findChild<QDockWidget *>("previewDock")->show(); select(panel, "c.txt");
        QTRY_COMPARE(window.findChild<QDockWidget *>("previewDock")->findChild<QPlainTextEdit *>()->toPlainText(), QString("plain text"));
        QVERIFY(!grid(panel)->editTriggers()); QVERIFY(grid(panel)->acceptDrops());
    }
    void copyPasteFromGridUsesNormalConfirmationAndNativeOperation() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); QVERIFY(QDir(fixture.path()).mkdir("source")); QVERIFY(QDir(fixture.path()).mkdir("target"));
        const auto source = fixture.path() + "/source", target = fixture.path() + "/target";
        image(source + "/Foto.png", Qt::green); const auto original = bytes(source + "/Foto.png");
        MainWindow window(source, target, false); window.show(); auto *left = window.leftPanel(), *right = window.rightPanel();
        QTRY_COMPARE(left->visibleCount(), 1); action(left, "viewThumbnails")->trigger(); action(right, "viewThumbnails")->trigger();
        select(left, "Foto.png"); grid(left)->setFocus(); QTRY_VERIFY(grid(left)->hasFocus()); QTest::keyClick(grid(left), Qt::Key_C, Qt::ControlModifier);
        grid(right)->setFocus(); QTRY_VERIFY(grid(right)->hasFocus());
        bool cancelled = false; QTimer::singleShot(0, &window, [&] { if (auto *dialog = window.findChild<OperationDialog *>("operationDialog")) { cancelled = true; dialog->reject(); } });
        QTest::keyClick(grid(right), Qt::Key_V, Qt::ControlModifier); QVERIFY(cancelled); QVERIFY(QDir(target).isEmpty());
        window.activateWindow(); grid(right)->setFocus(); QTRY_VERIFY(grid(right)->hasFocus());
        bool confirmed = false;
        QTimer::singleShot(0, &window, [&] { if (auto *dialog = window.findChild<OperationDialog *>("operationDialog")) { confirmed = true; dialog->findChild<QPushButton *>("confirmOperation")->click(); } });
        QTest::keyClick(grid(right), Qt::Key_V, Qt::ControlModifier); QVERIFY(confirmed);
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QMessageBox *>("operationResult"), 15000); QTRY_COMPARE(right->visibleCount(), 1);
        QCOMPARE(bytes(target + "/Foto.png"), original); QCOMPARE(bytes(source + "/Foto.png"), original); closeResult(window);
    }
    void gridDeleteAndContextMenuKeepSelectionUntilConfirmation() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); image(fixture.path() + "/a.png", Qt::red); image(fixture.path() + "/b.png", Qt::blue);
        MainWindow window(fixture.path(), fixture.path(), false); window.show(); auto *panel = window.leftPanel();
        QTRY_COMPARE(panel->visibleCount(), 2); action(panel, "viewThumbnails")->trigger(); grid(panel)->selectAll(); QCOMPARE(panel->selectedPaths().size(), 2);
        grid(panel)->setFocus(); QTRY_VERIFY(grid(panel)->hasFocus()); bool cancelled = false;
        QTimer::singleShot(0, &window, [&] { if (auto *dialog = window.findChild<OperationDialog *>("operationDialog")) { cancelled = true; dialog->reject(); } });
        QTest::keyClick(grid(panel), Qt::Key_Delete); QVERIFY(cancelled); QCOMPARE(panel->selectedPaths().size(), 2);
        QVERIFY(QFileInfo::exists(fixture.path() + "/a.png")); QVERIFY(QFileInfo::exists(fixture.path() + "/b.png"));
        window.activateWindow(); bool sawMenu = false;
        QTimer::singleShot(0, &window, [&] { if (auto *menu = window.findChild<QMenu *>("fileContextMenu")) { sawMenu = true; menu->close(); } });
        const auto point = grid(panel)->visualRect(index(panel, "a.png")).center();
        QContextMenuEvent context(QContextMenuEvent::Mouse, point, grid(panel)->viewport()->mapToGlobal(point));
        QApplication::sendEvent(grid(panel)->viewport(), &context); QVERIFY(sawMenu); QCOMPARE(panel->selectedPaths().size(), 2);
        window.activateWindow(); auto *address = panel->findChild<QLineEdit *>("address"); address->setFocus(); QTRY_VERIFY(address->hasFocus()); address->setText("abcd"); address->setCursorPosition(1);
        QTest::keyClick(address, Qt::Key_Delete); QCOMPARE(address->text(), QString("acd")); QVERIFY(!window.findChild<OperationDialog *>("operationDialog"));
    }
    void gridDropDefersConfirmationAndHonorsTransferLock() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); QVERIFY(QDir(fixture.path()).mkdir("target")); write(fixture.path() + "/source.txt", "original");
        FilePanel panel; panel.resize(600, 500); panel.show(); QVERIFY(QTest::qWaitForWindowExposed(&panel)); QVERIFY(panel.navigate(fixture.path())); QTRY_COMPARE(panel.visibleCount(), 2);
        action(&panel, "viewThumbnails")->trigger(); QTRY_VERIFY(grid(&panel)->viewport()->rect().contains(grid(&panel)->visualRect(index(&panel, "target")).center()));
        const auto point = grid(&panel)->visualRect(index(&panel, "target")).center(); QSignalSpy requests(&panel, &FilePanel::transferRequested);
        FileDragData mime({fixture.path() + "/source.txt"}); QVERIFY2(mime.transfer().valid(), qPrintable(mime.transfer().error));
        QVERIFY(grid(&panel)->acceptDrops()); QVERIFY(grid(&panel)->viewport()->acceptDrops());

#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
        QDragEnterEvent enter(QPointF(point), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::ShiftModifier);
#else
        QDragEnterEvent enter(point, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::ShiftModifier);
#endif
        QVERIFY(QApplication::sendEvent(grid(&panel)->viewport(), &enter)); QVERIFY(enter.isAccepted());
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
        QDragMoveEvent move(QPointF(point), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::ShiftModifier);
#else
        QDragMoveEvent move(point, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::ShiftModifier);
#endif
        QApplication::sendEvent(grid(&panel)->viewport(), &move); QVERIFY(move.isAccepted());
        QDropEvent drop(point, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::ShiftModifier); QApplication::sendEvent(grid(&panel)->viewport(), &drop);
        QVERIFY(drop.isAccepted()); QCOMPARE(requests.count(), 0); QTRY_COMPARE(requests.count(), 1);
        QCOMPARE(qvariant_cast<FileTransfer>(requests.first()[0]).kind, OperationKind::Move); QCOMPARE(requests.first()[1].toString(), fixture.path() + "/target");
        QCOMPARE(bytes(fixture.path() + "/source.txt"), QByteArray("original"));
        panel.setTransfersEnabled(false);
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
        QDragEnterEvent blocked(QPointF(point), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
#else
        QDragEnterEvent blocked(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
#endif
        QApplication::sendEvent(grid(&panel)->viewport(), &blocked); QVERIFY(!blocked.isAccepted());
    }
    void cacheSharesDecodesBoundsDemandAndRefreshesIdenticalMetadata() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto path = fixture.path() + "/same.bmp"; image(path, Qt::red, {700, 700});
        ThumbnailCache cache; QObject left, right; const auto source = *thumbnailSource(QFileInfo(path));
        cache.setDemand(&left, {source}); cache.setDemand(&right, {source}); QTRY_VERIFY(cache.find(source));
        QCOMPARE(cache.completedDecodes(), quint64(1)); QCOMPARE(cache.find(source)->image.size(), QSize(256, 256)); QCOMPARE(cache.find(source)->image.pixelColor(20, 20), QColor(Qt::red));
        const auto time = QFileInfo(path).lastModified(); image(path, Qt::blue, {700, 700});
        QFile file(path); QVERIFY(file.open(QIODevice::ReadWrite)); QVERIFY(file.setFileTime(time, QFileDevice::FileModificationTime)); file.close();
        QCOMPARE(*thumbnailSource(QFileInfo(path)), source);
        cache.invalidate(); QTRY_VERIFY(cache.find(source)); QCOMPARE(cache.find(source)->image.pixelColor(20, 20), QColor(Qt::blue)); QCOMPARE(cache.completedDecodes(), quint64(2));
        auto list = QVector<ThumbnailSource>(400, source); cache.setDemand(&left, list); QCOMPARE(cache.demandedCount(), 1);
    }
    void cacheCapacityIncludesHighBitDepthImagesWithoutDecodeChurn() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); QImage bitmap(300, 300, QImage::Format_RGBA64); bitmap.fill(Qt::magenta);
        QVector<ThumbnailSource> sources;
        for (int i = 0; i < 256; ++i) {
            const auto path = fixture.path() + QString("/image-%1.png").arg(i); QVERIFY(bitmap.save(path));
            sources.append(*thumbnailSource(QFileInfo(path)));
        }
        ThumbnailCache cache; QObject left, right; cache.setDemand(&left, sources.mid(0, 128)); cache.setDemand(&right, sources.mid(128));
        QTRY_COMPARE_WITH_TIMEOUT(cache.completedDecodes(), quint64(256), 20000);
        QCOMPARE(cache.entryCount(), 256); QVERIFY(cache.retainedKiB() <= 64 * 1024); QCOMPARE(cache.demandedCount(), 256);
        for (const auto &source : sources) {
            QVERIFY(cache.find(source)); QCOMPARE(cache.find(source)->image.format(), QImage::Format_ARGB32_Premultiplied);
            QVERIFY(cache.find(source)->image.width() <= 256 && cache.find(source)->image.height() <= 256);
        }
        cache.setDemand(&left, sources.mid(0, 128)); QTest::qWait(80); QCOMPARE(cache.completedDecodes(), quint64(256));
        cache.removeDemand(&left); cache.removeDemand(&right); QCOMPARE(cache.demandedCount(), 0);
    }
    void viewportOnlyQueuesVisibleImagesAndStopsDemandWhenHidden() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        QImage bitmap(80, 60, QImage::Format_RGB32); bitmap.fill(Qt::cyan);
        for (int i = 0; i < 500; ++i) QVERIFY(bitmap.save(fixture.path() + QString("/image-%1.png").arg(i, 4, 10, QChar('0'))));
        MainWindow window(fixture.path(), fixture.path(), false); window.resize(1000, 700); window.show(); auto *panel = window.leftPanel();
        QTRY_COMPARE_WITH_TIMEOUT(panel->visibleCount(), 500, 15000); action(panel, "viewThumbnails")->trigger();
        auto *cache = window.findChild<ThumbnailCache *>("thumbnailCache"); QTRY_VERIFY(cache->completedDecodes() > 0);
        QVERIFY(cache->demandedCount() > 0 && cache->demandedCount() < 500); QVERIFY(cache->demandedCount() <= 128);
        grid(panel)->scrollTo(index(panel, "image-0499.png"));
        QTRY_VERIFY_WITH_TIMEOUT(result(*cache, fixture.path() + "/image-0499.png"), 10000);
        action(panel, "viewDetails")->trigger(); QTRY_COMPARE(cache->demandedCount(), 0);
    }
    void malformedAndLegacyThumbnailPreferencesPreserveSessions() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); FolderView view; view.thumbnails = true; view.thumbnailSize = 192;
        auto encoded = encodeFolderViews({{fixture.path(), view}}); auto legacy = encoded.first().toObject(); legacy.remove("thumbnails"); legacy.remove("thumbnailSize");
        const auto decoded = decodeFolderViews(QJsonArray{legacy}); QVERIFY(decoded); QVERIFY(!decoded->first().view.thumbnails); QCOMPARE(decoded->first().view.thumbnailSize, 144);
        QCOMPARE(decodeFolderViews(encoded)->first().view, view);
        WindowSession session; session.left.tabs = {{fixture.path(), {}}}; session.right.tabs = session.left.tabs; session.folderViews = QVector<FolderViewEntry>{{fixture.path(), view}};
        SessionStore store(fixture.path() + "/good.json"); QVERIFY(store.save(session).isEmpty());
        auto document = QJsonDocument::fromJson(bytes(fixture.path() + "/good.json")).object();
        for (const auto &bad : {QJsonValue("large"), QJsonValue(0), QJsonValue(143), QJsonValue(192.5), QJsonValue(QJsonValue::Null)}) {
            auto entry = encoded.first().toObject(); entry["thumbnailSize"] = bad; document["folderViews"] = QJsonArray{entry};
            const auto original = QJsonDocument(document).toJson(); const auto path = fixture.path() + "/bad.json"; write(path, original);
            SessionStore invalid(path); QCOMPARE(invalid.load().state, SessionLoadState::Invalid); QCOMPARE(bytes(path), original);
            const auto before = QDir(fixture.path()).entryList({"bad.json.preserved-*.json"}, QDir::Files);
            QVERIFY(invalid.save(session).isEmpty());
            const auto after = QDir(fixture.path()).entryList({"bad.json.preserved-*.json"}, QDir::Files);
            QString preserved; for (const auto &name : after) if (!before.contains(name)) preserved = name;
            QVERIFY(!preserved.isEmpty()); QCOMPARE(bytes(fixture.path() + '/' + preserved), original);
        }
        auto bad = encoded.first().toObject(); bad["thumbnails"] = 1; QVERIFY(!decodeFolderViews(QJsonArray{bad}));
    }
    void imageOrientationIsSharedAndLargeOrCorruptInputsFail() {
        QImage bitmap(80, 40, QImage::Format_RGB32); bitmap.fill(Qt::red); QBuffer buffer; buffer.open(QIODevice::WriteOnly); QVERIFY(bitmap.save(&buffer, "JPEG"));
        // TIFF IFD0 orientation=6, in a real JPEG EXIF APP1 segment.
        const auto exif = QByteArray::fromHex("45786966000049492a0008000000010012010300010000000600000000000000");
        const int length = int(exif.size()) + 2; QByteArray segment = QByteArray::fromHex("ffe1"); segment.append(char(length >> 8)); segment.append(char(length & 255)); segment.append(exif);
        auto jpeg = buffer.data(); jpeg.insert(2, segment); const auto oriented = decodeImagePreview(jpeg, 256);
        QVERIFY(oriented.error.isEmpty()); QCOMPARE(oriented.originalSize, QSize(40, 80)); QCOMPARE(oriented.image.size(), QSize(40, 80));
        QVERIFY(!decodeImagePreview("invalid", 256).error.isEmpty()); QVERIFY(!decodeImagePreview(buffer.data(), 8192).error.isEmpty());
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto path = fixture.path() + "/large.png"; QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.resize(maxImageFileBytes + 1)); file.close();
        ThumbnailCache cache; QObject owner; const auto source = *thumbnailSource(QFileInfo(path)); cache.setDemand(&owner, {source}); QTRY_VERIFY(cache.find(source));
        QVERIFY(!cache.find(source)->error.isEmpty()); QVERIFY(cache.find(source)->image.isNull());
    }
    void rapidNavigationAndWorkerDestructionDoNotApplyOldResults() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); QVERIFY(QDir(fixture.path()).mkdir("red")); QVERIFY(QDir(fixture.path()).mkdir("blue"));
        image(fixture.path() + "/red/image.png", Qt::red); image(fixture.path() + "/blue/image.png", Qt::blue);
        for (int repetition = 0; repetition < 3; ++repetition) {
            MainWindow window(fixture.path() + "/red", fixture.path() + "/blue", false); window.show(); auto *panel = window.leftPanel();
            action(panel, "viewThumbnails")->trigger();
            for (int i = 0; i < 20; ++i) QVERIFY(panel->navigate(fixture.path() + (i % 2 ? "/blue" : "/red")));
            QTRY_COMPARE(panel->visibleCount(), 1); auto *cache = window.findChild<ThumbnailCache *>("thumbnailCache");
            QTRY_VERIFY(result(*cache, fixture.path() + "/blue/image.png")); QCOMPARE(result(*cache, fixture.path() + "/blue/image.png")->image.pixelColor(20, 20), QColor(Qt::blue));
            QVERIFY(window.close());
        }
    }
    void captureNativeGrid() {
        const auto capture = qEnvironmentVariable("DIROPEN_THUMBNAIL_CAPTURE"); if (capture.isEmpty()) QSKIP("Captura opcional da grade nativa.");
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QStringList names{"Praia", "Montanha", "Jardim", "Cidade", "Lago", "Pôr do sol"};
        const QColor colors[]{QColor("#48a9c5"), QColor("#576b92"), QColor("#559971"), QColor("#aa7959"), QColor("#486b9d"), QColor("#d48157")};
        for (int i = 0; i < names.size(); ++i) {
            QImage bitmap(640, 400, QImage::Format_RGB32); bitmap.fill(colors[i]); QPainter painter(&bitmap);
            painter.setPen(Qt::white); QFont font("Segoe UI", 38); painter.setFont(font); painter.drawText(bitmap.rect(), Qt::AlignCenter, names[i]); painter.end();
            QVERIFY(bitmap.save(fixture.path() + '/' + names[i] + ".jpg"));
        }
        write(fixture.path() + "/Notas da viagem.txt", "Lista de lugares para conhecer."); QVERIFY(QDir(fixture.path()).mkdir("Documentos"));
        MainWindow window(fixture.path(), fixture.path(), false); window.resize(1400, 820); window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_COMPARE(window.leftPanel()->visibleCount(), 8); action(window.leftPanel(), "viewThumbnails")->trigger();
        auto *cache = window.findChild<ThumbnailCache *>("thumbnailCache"); QTRY_COMPARE_WITH_TIMEOUT(cache->completedDecodes(), quint64(6), 10000);
        select(window.leftPanel(), "Praia.jpg"); QVERIFY(window.grab().save(capture));
        window.findChild<QAction *>("darkTheme")->setChecked(true); QTest::qWait(100); QVERIFY(window.grab().save(capture + ".dark.png"));
    }
};
QTEST_MAIN(ThumbnailTest)
#include "thumbnail_test.moc"
