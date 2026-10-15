#include "thumbnailcache.h"
#include "filepanel.h"
#include "mainwindow.h"
#include "mediafixture.h"
#include <QAction>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QImageReader>
#include <QListView>
#include <QLineEdit>
#include <QStatusBar>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTest>
#include <windows.h>
#include <psapi.h>
class DocumentThumbnailsTest final : public QObject {
    Q_OBJECT
    void write(const QString &path, const QByteArray &bytes) { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(bytes),qint64(bytes.size())); }
    QByteArray hash(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256) : QByteArray{}; }
    void pdf(const QString &path) {
        QPdfWriter writer(path); writer.setPageSize(QPageSize(QPageSize::A4)); writer.setResolution(96);
        QPainter painter(&writer); QVERIFY(painter.isActive()); painter.fillRect(painter.viewport(),Qt::red);
        QVERIFY(writer.newPage()); painter.fillRect(painter.viewport(),Qt::blue); painter.end();
    }
    const ThumbnailResult *result(ThumbnailCache &cache, const QString &path) {
        auto source = thumbnailSource(QFileInfo(path)); return source ? cache.find(*source) : nullptr;
    }
private slots:
    void initTestCase() { QImageReader::setAllocationLimit(64); }
    void classifiesMetadataOnlyAndKeepsAudioOnItsNormalIcon() {
        QTemporaryDir dir; const auto document = dir.filePath("arquivo.PDF"), video = dir.filePath("filme.MP4"), audio = dir.filePath("som.wav");
        write(document,"invalid"); write(video,"invalid"); write(audio,MediaFixture::wave());
        QCOMPARE(thumbnailSource(QFileInfo(document))->kind,ThumbnailKind::Pdf);
        QCOMPARE(thumbnailSource(QFileInfo(video))->kind,ThumbnailKind::Video);
        QVERIFY(!thumbnailSource(QFileInfo(audio))); QVERIFY(!thumbnailSource(QFileInfo(dir.path())));
    }
    void rendersFirstPdfPageBoundedAndReusesCacheWithoutChangingSource() {
        QTemporaryDir dir; const auto path = dir.filePath("páginas.pdf"); pdf(path); const auto before = hash(path);
        ThumbnailCache cache; QObject owner; const auto source = *thumbnailSource(QFileInfo(path)); QElapsedTimer elapsed; elapsed.start();
        cache.setDemand(&owner,{source}); QTRY_VERIFY_WITH_TIMEOUT(cache.find(source),15000);
        const auto *loaded = cache.find(source); QVERIFY2(loaded->error.isEmpty(),qPrintable(loaded->error)); QVERIFY(!loaded->image.isNull());
        QVERIFY(loaded->image.width() <= 256); QVERIFY(loaded->image.height() <= 256);
        const auto center = loaded->image.pixelColor(loaded->image.width()/2,loaded->image.height()/2); QVERIFY(center.red() > 200); QVERIFY(center.blue() < 40);
        const auto cold = elapsed.elapsed(); const auto decodes = cache.completedDecodes(); elapsed.restart();
        cache.removeDemand(&owner); cache.setDemand(&owner,{source}); QCOMPARE(cache.completedDecodes(),decodes); QVERIFY(cache.find(source));
        qInfo("PDF first-demand=%lldms cache-reuse=%lldms",cold,elapsed.elapsed()); QCOMPARE(hash(path),before);
        cache.removeDemand(&owner); QVERIFY(QFile::rename(path,dir.filePath("liberado.pdf")));
    }
    void decodesRealVideoFrameSilentlyAndReleasesDevice() {
        QTemporaryDir dir; const auto path = dir.filePath("filme.avi"); write(path,MediaFixture::video()); const auto before = hash(path);
        ThumbnailCache cache; QObject owner; const auto source = *thumbnailSource(QFileInfo(path)); QElapsedTimer elapsed; elapsed.start();
        cache.setDemand(&owner,{source}); QTRY_VERIFY_WITH_TIMEOUT(cache.find(source),15000);
        const auto *loaded = cache.find(source); QVERIFY2(loaded->error.isEmpty(),qPrintable(loaded->error)); QVERIFY(!loaded->image.isNull());
        QVERIFY(loaded->image.width() <= 256); QVERIFY(loaded->image.height() <= 256);
        const auto center = loaded->image.pixelColor(loaded->image.width()/2,loaded->image.height()/2);
        QVERIFY(center.blue() > 160); QVERIFY(center.green() > 90); QVERIFY(center.red() < 80);
        qInfo("MJPEG AVI first-demand=%lldms",elapsed.elapsed()); QCOMPARE(hash(path),before);
        cache.removeDemand(&owner); QVERIFY(QFile::rename(path,dir.filePath("liberado.avi")));
    }
    void corruptFilesReturnBoundedCachedErrorsAndWorkerContinues() {
        QTemporaryDir dir; const auto badPdf = dir.filePath("inválido.pdf"), badVideo = dir.filePath("inválido.mp4"), good = dir.filePath("válido.pdf");
        write(badPdf,"not a PDF"); write(badVideo,"not a video"); pdf(good);
        ThumbnailCache cache; QObject owner;
        cache.setDemand(&owner,{*thumbnailSource(QFileInfo(badPdf)),*thumbnailSource(QFileInfo(badVideo)),*thumbnailSource(QFileInfo(good))});
        QTRY_VERIFY_WITH_TIMEOUT(result(cache,badPdf) && result(cache,badVideo) && result(cache,good),20000);
        QVERIFY(!result(cache,badPdf)->error.isEmpty()); QVERIFY(!result(cache,badVideo)->error.isEmpty()); QVERIFY(!result(cache,good)->image.isNull()); QVERIFY(cache.retainedKiB() <= 64*1024);
    }
    void refreshDropsObsoleteVideoGenerationAndStopsOwnedWorker() {
        QTemporaryDir dir; const auto video = dir.filePath("filme.avi"), document = dir.filePath("página.pdf"); write(video,MediaFixture::video()); pdf(document);
        ThumbnailWorker worker; worker.submit({*thumbnailSource(QFileInfo(video))},1);
        worker.submit({*thumbnailSource(QFileInfo(document))},2);
        std::optional<ThumbnailResult> loaded;
        QTRY_VERIFY_WITH_TIMEOUT(loaded.has_value() || (loaded = worker.takeResult()).has_value(),15000);
        QCOMPARE(loaded->generation,quint64(2)); QCOMPARE(loaded->source.kind,ThumbnailKind::Pdf); QVERIFY2(loaded->error.isEmpty(),qPrintable(loaded->error));
        QElapsedTimer elapsed; elapsed.start();
        { ThumbnailWorker closing; closing.submit({*thumbnailSource(QFileInfo(video))},1); QTest::qWait(15); }
        QVERIFY(elapsed.elapsed() < 15000);
        QVERIFY(QFile::rename(video,dir.filePath("liberado.avi")));
    }
    void nativeGridDisplaysBothKindsAndSharesCacheAcrossPanels() {
        QTemporaryDir dir; const auto video = dir.filePath("filme.avi"), document = dir.filePath("página.pdf"); write(video,MediaFixture::video()); pdf(document);
        MainWindow window(dir.path(),dir.path(),false); window.resize(1500,900); window.show();
        window.leftPanel()->findChild<QAction *>("viewThumbnails")->trigger(); window.rightPanel()->findChild<QAction *>("viewThumbnails")->trigger();
        auto *cache = window.findChild<ThumbnailCache *>(); QVERIFY(cache);
        QTRY_VERIFY_WITH_TIMEOUT(result(*cache,video) && result(*cache,document),20000);
        QVERIFY(!result(*cache,video)->image.isNull()); QVERIFY(!result(*cache,document)->image.isNull());
        QCOMPARE(cache->completedDecodes(),quint64(2)); QVERIFY(window.leftPanel()->findChild<QListView *>("fileThumbnails")->isVisible());
        if (qEnvironmentVariable("QT_QPA_PLATFORM") == "windows") {
            const auto output = qEnvironmentVariable("DIROPEN_CAPTURE_DIR");
            if (!output.isEmpty()) {
                window.findChild<QWidget *>("sidebar")->hide(); QTest::qWait(50);
                for (auto *edit : window.findChildren<QLineEdit *>("address")) { QSignalBlocker blocker(edit); edit->setText("C:\\Exemplos\\Miniaturas"); }
                for (auto *tabs : window.findChildren<QTabBar *>("folderTabs")) tabs->setTabText(0,"Miniaturas");
                window.statusBar()->showMessage("Demonstração — arquivos gerados para os testes");
                QVERIFY(QTest::qWaitForWindowExposed(&window)); QVERIFY(window.grab().save(output + "/miniaturas-012.png"));
            }
        }
        PROCESS_MEMORY_COUNTERS memory{}; memory.cb = sizeof(memory); QVERIFY(GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory)));
        FILETIME created,exit,kernel,user; QVERIFY(GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user));
        auto time = [](FILETIME t) { return (quint64(t.dwHighDateTime)<<32)|t.dwLowDateTime; };
        qInfo("Process cumulative CPU=%llums peak working-set=%lluKiB; includes preceding tests, decoder/GPU families unmeasured",
            (time(kernel)+time(user))/10000,quint64(memory.PeakWorkingSetSize)/1024);
    }
};
QTEST_MAIN(DocumentThumbnailsTest)
#include "document_thumbnails_test.moc"
