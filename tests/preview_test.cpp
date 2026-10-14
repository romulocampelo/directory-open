#include "previewwidget.h"
#include "mainwindow.h"
#include "filepanel.h"
#include "sessionstore.h"
#include <QAction>
#include <QAbstractScrollArea>
#include <QImageReader>
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>

class PreviewTest : public QObject {
    Q_OBJECT
private:
    void write(const QString &path, const QByteArray &bytes) { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), qint64(bytes.size())); }
    QByteArray hash(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256); }
    void pdf(const QString &path, int pages = 2) {
        QPdfWriter writer(path); writer.setPageSize(QPageSize(QPageSize::A4)); writer.setResolution(96);
        QPainter painter(&writer); QVERIFY(painter.isActive());
        for (int page = 0; page < pages; ++page) { if (page) QVERIFY(writer.newPage()); painter.fillRect(painter.viewport(), page % 2 ? Qt::blue : Qt::red); painter.setPen(Qt::black); painter.drawText(25, 50, QString("Página %1 — Directory Open").arg(page + 1)); }
        painter.end();
    }
    void image(const QString &path, const QSize &size = QSize(640, 320)) { QImage picture(size, QImage::Format_RGB32); picture.fill(Qt::red); QVERIFY(picture.save(path)); }
    QAction *action(PreviewWidget &widget, const char *name) { return widget.findChild<QAction *>(name); }
private slots:
    void initTestCase() { QImageReader::setAllocationLimit(64); }
    void imagesZoomRotateFitAndPreserveSource() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto path = fixture.path() + "/imagem.png"; image(path, QSize(1800, 900)); const auto before = hash(path);
        PreviewWidget viewer; viewer.resize(380, 500); viewer.show(); viewer.setPath(path);
        QTRY_COMPARE_WITH_TIMEOUT(viewer.result().kind, PreviewKind::Image, 10000); QCOMPARE(viewer.result().originalSize, QSize(1800, 900));
        auto *canvas = viewer.findChild<QAbstractScrollArea *>("previewCanvas"); QVERIFY(canvas);
        action(viewer, "previewActualSize")->trigger(); QTRY_VERIFY(canvas->horizontalScrollBar()->maximum() > 0);
        action(viewer, "previewZoomIn")->trigger(); action(viewer, "previewZoomOut")->trigger(); action(viewer, "previewRotate")->trigger();
        QVERIFY(canvas->verticalScrollBar()->maximum() > 0); action(viewer, "previewFit")->trigger();
        QTRY_COMPARE(canvas->horizontalScrollBar()->maximum(), 0); QTRY_COMPARE(canvas->verticalScrollBar()->maximum(), 0);
        QCOMPARE(hash(path), before); QVERIFY(action(viewer, "previewRotate")->isEnabled());
    }
    void imagesAboveDecodedBudgetAreReducedBeforeDisplay() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto path = fixture.path() + "/grande.jpg";
        image(path, QSize(5000, 4000)); PreviewWidget viewer; viewer.setPath(path);
        QTRY_COMPARE_WITH_TIMEOUT(viewer.result().kind, PreviewKind::Image, 10000);
        QCOMPARE(viewer.result().originalSize, QSize(5000, 4000)); QVERIFY(viewer.result().image.width() <= 4096); QVERIFY(viewer.result().image.height() <= 4096);
        QVERIFY(viewer.findChild<QLabel *>("previewDetails")->text().contains("reduzida"));
    }
    void textIsPlainSearchableAndDoesNotRunMarkup() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto path = fixture.path() + "/pagina.html";
        const auto content = QByteArray("<script>throw 'never executed'</script> %PDF-1.7 alvo primeiro alvo segundo"); write(path, content); const auto before = hash(path);
        PreviewWidget viewer; viewer.show(); viewer.setPath(path); QTRY_COMPARE(viewer.result().kind, PreviewKind::Text);
        auto *text = viewer.findChild<QPlainTextEdit *>("previewText"); QCOMPARE(text->toPlainText(), QString::fromUtf8(content)); QVERIFY(text->isReadOnly());
        viewer.findChild<QLineEdit *>("previewFind")->setText("alvo"); viewer.findChild<QPushButton *>("previewFindNext")->click(); QCOMPARE(text->textCursor().selectedText(), QString("alvo"));
        const auto first = text->textCursor().position(); viewer.findChild<QPushButton *>("previewFindNext")->click(); QVERIFY(text->textCursor().position() > first);
        QCOMPARE(hash(path), before);
    }
    void unicodeBomAndLegacyEncodingsAreExplicit() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto unicode = fixture.path() + "/unicode.txt";
        write(unicode, QByteArray::fromHex("fffe6100e700e3006f00")); PreviewWidget viewer; viewer.setPath(unicode);
        QTRY_COMPARE(viewer.result().kind, PreviewKind::Text); QCOMPARE(viewer.result().text, QString::fromUtf8("ação")); QCOMPARE(viewer.result().encoding, QString("UTF-16 LE"));
        const auto noBom = fixture.path() + "/sem-bom.txt"; write(noBom, QByteArray::fromHex("6100e700e3006f00")); viewer.setPath(noBom);
        QTRY_COMPARE(viewer.result().kind, PreviewKind::Error); QVERIFY(viewer.result().textCandidate);
        viewer.findChild<QComboBox *>("previewEncoding")->setCurrentIndex(viewer.findChild<QComboBox *>("previewEncoding")->findData(int(PreviewEncoding::Utf16LE))); QTRY_COMPARE(viewer.result().kind, PreviewKind::Text); QCOMPARE(viewer.result().text, QString::fromUtf8("ação"));
        const auto legacy = fixture.path() + "/legado.txt"; write(legacy, QByteArray::fromHex("61e7e36f")); viewer.setPath(legacy);
        QTRY_COMPARE(viewer.result().path, legacy); QTRY_COMPARE(viewer.result().kind, PreviewKind::Text); QCOMPARE(viewer.result().text, QString::fromUtf8("ação")); QVERIFY(viewer.result().encoding.contains("alternativa"));
        viewer.findChild<QComboBox *>("previewEncoding")->setCurrentIndex(viewer.findChild<QComboBox *>("previewEncoding")->findData(int(PreviewEncoding::Utf8))); QTRY_COMPARE(viewer.result().kind, PreviewKind::Error);
        QVERIFY(!viewer.findChild<QComboBox *>("previewEncoding")->isHidden());
        viewer.findChild<QComboBox *>("previewEncoding")->setCurrentIndex(viewer.findChild<QComboBox *>("previewEncoding")->findData(int(PreviewEncoding::Windows1252))); QTRY_COMPARE(viewer.result().kind, PreviewKind::Text); QCOMPARE(viewer.result().text, QString::fromUtf8("ação"));
    }
    void boundedTextReportsPartialAndBinaryIsNotMistakenForText() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto text = fixture.path() + "/large.log"; write(text, QByteArray(1024 * 1024 + 10, 'a'));
        PreviewWidget viewer; viewer.setPath(text); QTRY_COMPARE(viewer.result().kind, PreviewKind::Text); QVERIFY(viewer.result().truncated); QCOMPARE(viewer.result().text.size(), 1024 * 1024);
        QVERIFY(viewer.findChild<QLabel *>("previewDetails")->text().contains("parcial"));
        const auto binary = fixture.path() + "/binary.bin"; write(binary, QByteArray::fromHex("0001020304000506")); viewer.setPath(binary);
        QTRY_COMPARE(viewer.result().kind, PreviewKind::Error); QVERIFY(!action(viewer, "previewZoomIn")->isEnabled()); QVERIFY(viewer.result().image.isNull());
    }
    void realWindowsPdfRendersPagesAndRevalidatesCachedContent() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto path = fixture.path() + "/documento.pdf"; pdf(path); const auto before = hash(path);
        PreviewWidget viewer; viewer.show(); viewer.setPath(path);
        QTRY_VERIFY_WITH_TIMEOUT(viewer.result().kind != PreviewKind::Empty, 20000); QVERIFY2(viewer.result().kind == PreviewKind::Pdf, qPrintable(viewer.result().message)); QCOMPARE(viewer.result().pages, 2); QCOMPARE(viewer.result().page, 0);
        QVERIFY(viewer.result().image.width() <= 1800); QVERIFY(viewer.result().image.height() <= 2400);
        auto color = viewer.result().image.pixelColor(viewer.result().image.width()/2, viewer.result().image.height()/2); QVERIFY(color.red() > 150 && color.blue() < 100);
        action(viewer, "previewActualSize")->trigger();
        auto *pdfCanvas = viewer.findChild<QAbstractScrollArea *>("previewCanvas"); QVERIFY(pdfCanvas->verticalScrollBar()->maximum() > 0);
        viewer.findChild<QSpinBox *>("previewPage")->setValue(2); QTRY_COMPARE_WITH_TIMEOUT(viewer.result().kind, PreviewKind::Pdf, 10000); QTRY_COMPARE(viewer.result().page, 1); QVERIFY(pdfCanvas->verticalScrollBar()->maximum() > 0);
        color = viewer.result().image.pixelColor(viewer.result().image.width()/2, viewer.result().image.height()/2); QVERIFY(color.blue() > 150 && color.red() < 100);
        action(viewer, "previewZoomIn")->trigger(); QVERIFY(!action(viewer, "previewRotate")->isEnabled()); QCOMPARE(hash(path), before);
        pdf(path, 1); viewer.reload(); QTRY_COMPARE_WITH_TIMEOUT(viewer.result().kind, PreviewKind::Pdf, 10000); QCOMPARE(viewer.result().pages, 1); QCOMPARE(viewer.result().page, 0);
    }
    void corruptMissingAndOversizedPdfHaveUsefulErrors() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto invalid = fixture.path() + "/invalid.pdf"; write(invalid, "%PDF-invalid");
        PreviewWidget viewer; viewer.setPath(invalid); QTRY_COMPARE_WITH_TIMEOUT(viewer.result().kind, PreviewKind::Error, 10000); QVERIFY(viewer.result().message.contains("PDF"));
        const auto huge = fixture.path() + "/huge.pdf"; { QFile file(huge); QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.resize(64 * 1024 * 1024 + 1)); }
        viewer.setPath(huge); QTRY_COMPARE(viewer.result().kind, PreviewKind::Error); QVERIFY(viewer.result().message.contains("64 MiB"));
        viewer.setPath(fixture.path() + "/missing.pdf"); QTRY_COMPARE(viewer.result().kind, PreviewKind::Error); QVERIFY(viewer.result().message.contains("indisponível"));
    }
    void rapidSelectionNeverDisplaysPreviousRequestAsCurrent() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); const auto picture = fixture.path() + "/picture.png", text = fixture.path() + "/current.txt";
        image(picture); write(text, "conteúdo atual"); PreviewWidget viewer;
        for (int i = 0; i < 40; ++i) { viewer.setPath(picture); viewer.setPath(text); }
        QTRY_COMPARE(viewer.result().kind, PreviewKind::Text); QCOMPARE(viewer.result().path, text); QCOMPARE(viewer.result().text, QString::fromUtf8("conteúdo atual"));
        viewer.setPath({}); QTRY_COMPARE(viewer.result().kind, PreviewKind::Empty); QVERIFY(viewer.result().image.isNull()); QVERIFY(viewer.result().text.isEmpty());
    }
    void viewerUsesCurrentSelectedFileAndSkipsFolders() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); QVERIFY(QDir(fixture.path()).mkdir("folder")); write(fixture.path()+"/a.txt", "arquivo a"); write(fixture.path()+"/b.txt", "arquivo b");
        MainWindow window(fixture.path(), fixture.path(), false); window.show(); window.findChild<QDockWidget *>("previewDock")->show();
        auto *panel = window.leftPanel(); auto *list = panel->findChild<QTreeView *>("files"); auto *viewer = window.findChild<PreviewWidget *>(); QTRY_COMPARE(panel->visibleCount(), 3);
        list->selectAll(); list->selectionModel()->setCurrentIndex(list->model()->index(2,0,list->rootIndex()), QItemSelectionModel::NoUpdate);
        QTRY_COMPARE(viewer->result().path, fixture.path()+"/b.txt"); QCOMPARE(panel->selectedPaths().size(), 3);
        action(*viewer, "previewPreviousFile")->trigger(); QTRY_COMPARE(viewer->result().path, fixture.path()+"/a.txt"); QCOMPARE(panel->selectedPaths().size(), 1);
        action(*viewer, "previewPreviousFile")->trigger(); QCOMPARE(panel->selectedPath(), fixture.path()+"/a.txt");
        action(*viewer, "previewNextFile")->trigger(); QTRY_COMPARE(viewer->result().path, fixture.path()+"/b.txt");
    }
    void expandedViewerReturnsSameWidgetAndSessionSurvivesRestart() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); write(fixture.path()+"/a.txt", "prévia"); const auto session = fixture.path()+"/session.json";
        MainWindow window(fixture.path(), fixture.path(), false, session); window.show(); auto *viewer = window.findChild<PreviewWidget *>();
        auto *list = window.leftPanel()->findChild<QTreeView *>("files"); QTRY_COMPARE(window.leftPanel()->visibleCount(), 1); list->selectAll();
        bool saw = false; QTimer::singleShot(0, &window, [&] { auto *dialog = window.findChild<QDialog *>("viewerDialog"); if (dialog) {
            QTimer::singleShot(8000, dialog, &QDialog::reject);
            saw = dialog->findChild<PreviewWidget *>() == viewer; QVERIFY(viewer->isVisible());
            QTRY_COMPARE(viewer->result().kind, PreviewKind::Text);
            if (QApplication::platformName() == "windows") {
                QVERIFY(QTest::qWaitForWindowExposed(dialog)); dialog->activateWindow();
                QVERIFY(QTest::qWaitForWindowActive(dialog));
                // Send keys to the actual focused child after the window is ready.
                auto *focus = QApplication::focusWidget(); QVERIFY(focus && dialog->isAncestorOf(focus));
                QTest::keyClick(focus, Qt::Key_F11); QTRY_VERIFY(dialog->isFullScreen());
                QVERIFY(QTest::qWaitForWindowActive(dialog)); focus = QApplication::focusWidget();
                QVERIFY(focus && dialog->isAncestorOf(focus)); QTest::keyClick(focus, Qt::Key_F11); QTRY_VERIFY(!dialog->isFullScreen());
            }
            QCOMPARE(viewer->result().text, QString::fromUtf8("prévia")); dialog->reject();
        } });
        window.findChild<QAction *>("viewFile")->trigger(); QVERIFY(saw); QCOMPARE(window.findChild<QDockWidget *>("previewDock")->widget(), viewer); QVERIFY(viewer->isVisible());
        QVERIFY(window.close()); MainWindow reopened({}, {}, false, session); reopened.show(); QVERIFY(reopened.session().previewVisible); QVERIFY(reopened.close());
    }
    void closingManagerWhileViewerIsOpenRestoresDockBeforeSaving() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); write(fixture.path()+"/a.txt", "arquivo preservado"); const auto session = fixture.path()+"/session.json";
        MainWindow window(fixture.path(), fixture.path(), false, session); window.show();
        QTimer::singleShot(0, &window, [&] { QVERIFY(window.findChild<QDialog *>("viewerDialog")); window.close(); });
        window.findChild<QAction *>("viewFile")->trigger(); QTRY_VERIFY(!window.isVisible());
        QVERIFY(QFileInfo::exists(session)); SessionStore store(session); const auto loaded = store.load(); QCOMPARE(loaded.state, SessionLoadState::Loaded); QVERIFY(loaded.session.previewVisible);
        QCOMPARE(window.findChild<QDockWidget *>("previewDock")->widget(), window.findChild<PreviewWidget *>());
    }
    void brandChangeDoesNotChangeSessionStorageIdentity() {
        const auto original = QCoreApplication::applicationName(); QCoreApplication::setApplicationName("Navega"); const auto before = SessionStore::defaultPath();
        QCoreApplication::setApplicationName("Directory Open"); const auto after = SessionStore::defaultPath(); QCoreApplication::setApplicationName(original);
        QCOMPARE(after, before); QVERIFY(after.endsWith("Navega/session.json"));
    }
    void captureNativeImageAndPdfViewer() {
        const auto capture = qEnvironmentVariable("DIROPEN_PREVIEW_CAPTURE"); if (capture.isEmpty()) QSKIP("Captura opcional da interface nativa.");
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); image(fixture.path()+"/Imagem.png"); pdf(fixture.path()+"/Documento.pdf");
        MainWindow window(fixture.path(), fixture.path(), false); window.resize(1500,900); window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window)); window.findChild<QDockWidget *>("previewDock")->show();
        auto *viewer = window.findChild<PreviewWidget *>(); auto *files = window.leftPanel()->findChild<QTreeView *>("files"); QTRY_COMPARE(window.leftPanel()->visibleCount(), 2);
        files->selectionModel()->setCurrentIndex(files->model()->index(1,0,files->rootIndex()), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows); QTRY_COMPARE(viewer->result().kind, PreviewKind::Image); QVERIFY(window.grab().save(capture+"-image.png"));
        files->selectionModel()->setCurrentIndex(files->model()->index(0,0,files->rootIndex()), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows); QTRY_COMPARE_WITH_TIMEOUT(viewer->result().kind, PreviewKind::Pdf, 20000); QVERIFY(window.grab().save(capture+"-pdf.png"));
        bool expanded = false; QTimer::singleShot(300, &window, [&] { auto *dialog = window.findChild<QDialog *>("viewerDialog"); if (dialog) { expanded = viewer->isVisible() && dialog->grab().save(capture+"-expanded.png"); dialog->reject(); } });
        window.findChild<QAction *>("viewFile")->trigger(); QVERIFY(expanded);
    }
};
QTEST_MAIN(PreviewTest)
#include "preview_test.moc"
