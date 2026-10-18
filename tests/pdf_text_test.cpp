#include "previewwidget.h"
#include "mainwindow.h"
#include "filepanel.h"
#include <QAbstractScrollArea>
#include <QAction>
#include <QScrollBar>
#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDialog>
#include <QDockWidget>
#include <QFile>
#include <QFontDatabase>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPainter>
#include <QPdfWriter>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <limits>
#include <windows.h>

class PdfTextTest final : public QObject {
    Q_OBJECT
    std::unique_ptr<QMimeData> m_clipboard;
    void pdf(const QString &path, const QStringList &pages) {
        QPdfWriter writer(path); writer.setPageSize(QPageSize(QPageSize::A4)); writer.setResolution(72);
        QPainter painter(&writer); QVERIFY(painter.isActive()); painter.setFont(QFont("Arial",18));
        for (int page=0; page<pages.size(); ++page) {
            if (page) QVERIFY(writer.newPage());
            painter.fillRect(painter.viewport(),Qt::white); painter.setPen(Qt::black);
            if (pages[page].isEmpty()) painter.fillRect(QRect(30,60,100,100),Qt::blue);
            else painter.drawText(30,80,pages[page]);
        }
    }
    void load(PreviewWidget &viewer, const QString &path) {
        viewer.resize(700,900); viewer.show(); viewer.setPath(path);
        QTRY_COMPARE_WITH_TIMEOUT(viewer.result().kind,PreviewKind::Pdf,15000);
        QVERIFY(!viewer.result().pdfDigest.isEmpty()); QVERIFY(viewer.result().pdfPointSize.isValid());
    }
    void selectPage(PreviewWidget &viewer) {
        QSignalSpy done(&viewer,&PreviewWidget::displayed);
        viewer.findChild<QPushButton *>("pdfSelectPage")->click();
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,15000);
    }
    void find(PreviewWidget &viewer, const QString &query, bool backwards=false) {
        viewer.findChild<QLineEdit *>("pdfFind")->setText(query);
        QSignalSpy done(&viewer,&PreviewWidget::displayed);
        viewer.findChild<QPushButton *>(backwards ? "pdfFindPrevious" : "pdfFindNext")->click();
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,15000);
    }
    QByteArray bytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
private slots:
    void initTestCase() {
        QImageReader::setAllocationLimit(64);
        // offscreen has no Windows font database. Supply a local system font to
        // the fixture writer rather than silently creating PDFs without text.
        QVERIFY(QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR")+"/Fonts/arial.ttf") >= 0);
        m_clipboard = std::make_unique<QMimeData>(); const auto *mime = qApp->clipboard()->mimeData();
        if (mime) for (const auto &format : mime->formats()) m_clipboard->setData(format,mime->data(format));
    }
    void cleanupTestCase() {
        if (QGuiApplication::platformName()!="windows") { qApp->clipboard()->setMimeData(m_clipboard.release()); return; }
        // Keep the backup until the OS accepts the restoration. Qt takes
        // ownership of each attempted copy even when OleSetClipboard fails.
        for (int attempt=0; attempt<40; ++attempt) {
            DWORD owner=0; GetWindowThreadProcessId(GetClipboardOwner(),&owner);
            if (owner && owner!=GetCurrentProcessId()) { qInfo("Clipboard ownership changed; preserving newer external content."); return; }
            const auto sequence=GetClipboardSequenceNumber();
            auto *copy=new QMimeData;
            for (const auto &format:m_clipboard->formats()) copy->setData(format,m_clipboard->data(format));
            qApp->clipboard()->setMimeData(copy);
            if (GetClipboardSequenceNumber()!=sequence) {
                const auto *actual=qApp->clipboard()->mimeData(); bool matches=actual!=nullptr;
                if (actual) for (const auto &format:m_clipboard->formats()) matches=matches && actual->data(format)==m_clipboard->data(format);
                if (matches) return;
            }
            QTest::qWait(25);
        }
        QFAIL("Clipboard restoration was not confirmed after bounded retries.");
    }
    void searchesAcrossPagesForwardBackwardAndWraps() {
        QTemporaryDir fixture; const auto path=fixture.filePath("ação.pdf");
        pdf(path,{"ação alvo primeiro alvo final","segunda página alvo outro"}); PreviewWidget viewer; load(viewer,path);
        find(viewer,"ALVO"); QVERIFY(viewer.result().pdfText.found); QCOMPARE(viewer.result().page,0); QCOMPARE(viewer.result().pdfText.matchIndex,0); QVERIFY(!viewer.result().pdfText.bounds.isEmpty());
        const auto capture=qEnvironmentVariable("DIROPEN_PDF_TEXT_CAPTURE");
        if (!capture.isEmpty() && QGuiApplication::platformName()=="windows") { QVERIFY(QTest::qWaitForWindowExposed(&viewer)); QVERIFY(viewer.grab().save(capture)); }
        find(viewer,"ALVO"); QCOMPARE(viewer.result().pdfText.matchIndex,1); QCOMPARE(viewer.result().page,0);
        find(viewer,"ALVO"); QCOMPARE(viewer.result().page,1); QCOMPARE(viewer.findChild<QSpinBox *>("previewPage")->value(),2);
        find(viewer,"ALVO"); QCOMPARE(viewer.result().page,0); QCOMPARE(viewer.result().pdfText.matchIndex,0);
        find(viewer,"ALVO",true); QCOMPARE(viewer.result().page,1); QCOMPARE(viewer.result().pdfText.matchIndex,0);
        find(viewer,"ALVO",true); QCOMPARE(viewer.result().page,0); QCOMPARE(viewer.result().pdfText.matchIndex,1);
        find(viewer,"ação",true); QCOMPARE(viewer.result().page,0); QVERIFY(viewer.result().pdfText.found);
    }
    void unicodeTextSelectionCopiesOnlyOnExplicitRequestAndPreservesFile() {
        QTemporaryDir fixture; const auto path=fixture.filePath("seleção.pdf"); pdf(path,{"ação café — Directory Open"}); const auto before=bytes(path);
        PreviewWidget viewer; load(viewer,path); qApp->clipboard()->setText("fixture clipboard sentinel"); selectPage(viewer);
        QVERIFY(viewer.result().pdfText.text.contains("ação")); QVERIFY(viewer.result().pdfText.text.contains("café")); QVERIFY(!viewer.result().pdfText.bounds.isEmpty());
        QCOMPARE(qApp->clipboard()->text(),QString("fixture clipboard sentinel"));
        viewer.findChild<QPushButton *>("pdfCopySelection")->click(); QCOMPARE(qApp->clipboard()->text(),viewer.result().pdfText.text);
        QCOMPARE(bytes(path),before); QVERIFY(QFile::rename(path,fixture.filePath("liberado.pdf")));
        selectPage(viewer); QVERIFY(viewer.result().pdfText.text.contains("ação")); // immutable displayed snapshot
        viewer.reload(); QTRY_COMPARE(viewer.result().kind,PreviewKind::Error);
    }
    void mouseSelectionUsesPageCoordinatesAfterZoomAndScroll() {
        QTemporaryDir fixture; const auto path=fixture.filePath("mouse.pdf"); pdf(path,{"Directory Open texto selecionável"});
        PreviewWidget viewer; load(viewer,path); selectPage(viewer); QVERIFY(!viewer.result().pdfText.bounds.isEmpty());
        QRectF bound; for (const auto &glyph : viewer.result().pdfText.bounds) bound=bound.united(glyph);
        auto *canvas=viewer.findChild<QAbstractScrollArea *>("previewCanvas");
        viewer.findChild<QAction *>("previewActualSize")->trigger();
        canvas->horizontalScrollBar()->setValue(20); canvas->verticalScrollBar()->setValue(30);
        const auto original=viewer.result().originalSize; const auto points=viewer.result().pdfPointSize;
        const double scale=1.0;
        const double w=original.width()*scale,h=original.height()*scale;
        const QPointF offset(qMax(0.0,(canvas->viewport()->width()-w)/2)-canvas->horizontalScrollBar()->value(),qMax(0.0,(canvas->viewport()->height()-h)/2)-canvas->verticalScrollBar()->value());
        auto map=[&](QPointF p) { return (offset+QPointF(p.x()/points.width()*w,p.y()/points.height()*h)).toPoint(); };
        const auto left=map({bound.left()+1,bound.center().y()}),right=map({bound.right()-1,bound.center().y()});
        QSignalSpy done(&viewer,&PreviewWidget::displayed);
        QTest::mousePress(canvas->viewport(),Qt::LeftButton,{},left); QTest::mouseRelease(canvas->viewport(),Qt::LeftButton,{},right);
        QTRY_COMPARE(done.count(),1); QVERIFY2(!viewer.result().pdfText.text.isEmpty(),qPrintable(viewer.result().pdfText.message));
        canvas->setFocus(); QTest::keyClick(canvas,Qt::Key_A,Qt::ControlModifier); QTRY_COMPARE(done.count(),2);
        QVERIFY(viewer.result().pdfText.text.contains("Directory Open"));
        QTest::keyClick(canvas,Qt::Key_C,Qt::ControlModifier); QCOMPARE(qApp->clipboard()->text(),viewer.result().pdfText.text);
    }
    void imageOnlyPdfHasNoInventedTextAndSearchReportsCoverage() {
        QTemporaryDir fixture; const auto path=fixture.filePath("imagem.pdf"); pdf(path,{QString{}}); PreviewWidget viewer; load(viewer,path);
        selectPage(viewer); QVERIFY(viewer.result().pdfText.text.isEmpty()); QVERIFY(!viewer.findChild<QPushButton *>("pdfCopySelection")->isEnabled());
        find(viewer,"ausente"); QVERIFY(!viewer.result().pdfText.found); QVERIFY(viewer.result().pdfText.complete); QVERIFY(viewer.result().pdfText.message.contains("OCR"));
    }
    void pageChangeAndReloadClearSelectionAndRefreshDigest() {
        QTemporaryDir fixture; const auto path=fixture.filePath("atualizar.pdf"); pdf(path,{"primeiro alvo","segundo texto"});
        PreviewWidget viewer; load(viewer,path); const auto digest=viewer.result().pdfDigest; selectPage(viewer);
        viewer.findChild<QSpinBox *>("previewPage")->setValue(2); QTRY_COMPARE(viewer.result().page,1); QVERIFY(viewer.result().pdfText.text.isEmpty());
        selectPage(viewer); QVERIFY(viewer.result().pdfText.text.contains("segundo"));
        pdf(path,{"conteúdo renovado"}); viewer.reload(); QTRY_COMPARE(viewer.result().kind,PreviewKind::Pdf); QTRY_COMPARE(viewer.result().page,0);
        QVERIFY(viewer.result().pdfDigest!=digest); selectPage(viewer); QVERIFY(viewer.result().pdfText.text.contains("renovado")); QVERIFY(!viewer.result().pdfText.text.contains("primeiro"));
    }
    void queryChangesAndFileSwitchDiscardOldInteraction() {
        QTemporaryDir fixture; const auto path=fixture.filePath("busca.pdf"),text=fixture.filePath("atual.txt"); pdf(path,{"alvo conteúdo"});
        {QFile f(text);QVERIFY(f.open(QIODevice::WriteOnly));f.write("arquivo atual");}
        PreviewWidget viewer; load(viewer,path);
        auto *query=viewer.findChild<QLineEdit *>("pdfFind"); query->setText("alvo"); viewer.findChild<QPushButton *>("pdfFindNext")->click(); query->setText("outro");
        QTest::qWait(100); QVERIFY(!viewer.result().pdfText.found); QVERIFY(!viewer.findChild<QPushButton *>("pdfCopySelection")->isEnabled());
        viewer.findChild<QPushButton *>("pdfSelectPage")->click(); viewer.setPath(text); QTRY_COMPARE(viewer.result().kind,PreviewKind::Text); QCOMPARE(viewer.result().text,QString("arquivo atual")); QTest::qWait(100); QCOMPARE(viewer.result().kind,PreviewKind::Text);
    }
    void searchBudgetIsPartialAndManualPageNavigationContinues() {
        QTemporaryDir fixture; const auto path=fixture.filePath("longo.pdf"); QStringList pages;
        for(int i=0;i<128;++i) pages.append(QString("página %1 sem a palavra buscada").arg(i)); pages.append("destino exclusivo"); pdf(path,pages);
        PreviewWidget viewer; load(viewer,path); find(viewer,"exclusivo"); QVERIFY(!viewer.result().pdfText.found); QVERIFY(!viewer.result().pdfText.complete); QCOMPARE(viewer.result().pdfText.examinedPages,128);
        viewer.findChild<QSpinBox *>("previewPage")->setValue(129); QTRY_COMPARE(viewer.result().page,128); find(viewer,"exclusivo"); QVERIFY(viewer.result().pdfText.found);
    }
    void snapshotTokenAndInvalidCoordinatesAreRejectedAtOwner() {
        QTemporaryDir fixture; const auto a=fixture.filePath("a.pdf"),b=fixture.filePath("b.pdf"); pdf(a,{"primeiro"}); pdf(b,{"segundo"});
        PdfRenderer renderer; const auto original=renderer.render(bytes(a),0,{256,256},[]{return false;}); QVERIFY(!original.digest.isEmpty());
        PdfTextRequest request; request.action=PdfTextAction::SelectAll; request.digest=original.digest;
        QVERIFY(renderer.interact(request,[]{return false;}).text.contains("primeiro"));
        const auto other=renderer.render(bytes(b),0,{256,256},[]{return false;});
        auto stale=renderer.interact(request,[]{return false;}); QVERIFY(stale.text.isEmpty()); QVERIFY(!stale.complete);
        request.digest=other.digest; request.action=PdfTextAction::Select; request.start={-1,0}; request.end={1,1};
        QVERIFY(!renderer.interact(request,[]{return false;}).complete);
        request.action=PdfTextAction::Find; request.query=QString(257,'a'); QVERIFY(!renderer.interact(request,[]{return false;}).complete);
        request.query="segundo"; request.matchIndex=std::numeric_limits<int>::max(); QVERIFY(!renderer.interact(request,[]{return false;}).complete);
        request.matchIndex=-1; QVERIFY(!renderer.interact(request,[]{return true;}).complete);
    }
    void densePageReportsTextBudgetInsteadOfCopyingTruncatedText() {
        QTemporaryDir fixture; const auto path=fixture.filePath("denso.pdf"); pdf(path,{QString(65538,'a')}); PreviewWidget viewer; load(viewer,path);
        selectPage(viewer); QVERIFY(!viewer.result().pdfText.complete); QVERIFY(viewer.result().pdfText.text.isEmpty()); QVERIFY(!viewer.findChild<QPushButton *>("pdfCopySelection")->isEnabled());
        find(viewer,"a"); QVERIFY(!viewer.result().pdfText.complete); QVERIFY(!viewer.result().pdfText.found);
    }
    void closingWithPendingInteractionWaitsAndReleasesSource() {
        QTemporaryDir fixture; const auto path=fixture.filePath("fechar.pdf"); pdf(path,{"alvo texto"});
        { PreviewWidget viewer; load(viewer,path); viewer.findChild<QLineEdit *>("pdfFind")->setText("alvo"); viewer.findChild<QPushButton *>("pdfFindNext")->click(); }
        QVERIFY(QFile::rename(path,fixture.filePath("liberado.pdf")));
    }
    void previewOwnsShortcutsWithoutBreakingTheFilePanelOrExpandedViewer() {
        QTemporaryDir fixture; const auto path=fixture.filePath("atalhos.pdf"); pdf(path,{"Directory Open alvo"});
        MainWindow window(fixture.path(),fixture.path(),false); window.show(); window.activateWindow();
        window.findChild<QDockWidget *>("previewDock")->show();
        auto *files=window.leftPanel()->findChild<QTreeView *>("files"); QTRY_COMPARE(window.leftPanel()->visibleCount(),1);
        files->selectionModel()->setCurrentIndex(files->model()->index(0,0,files->rootIndex()),QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        auto *viewer=window.findChild<PreviewWidget *>(); QTRY_COMPARE(viewer->result().kind,PreviewKind::Pdf);
        auto *canvas=viewer->findChild<QAbstractScrollArea *>("previewCanvas"); auto *query=viewer->findChild<QLineEdit *>("pdfFind");
        QSignalSpy selected(viewer,&PreviewWidget::displayed); canvas->setFocus(); QTest::keyClick(canvas,Qt::Key_A,Qt::ControlModifier);
        QTRY_COMPARE(selected.count(),1); QVERIFY(viewer->result().pdfText.text.contains("Directory Open"));
        QTest::keyClick(canvas,Qt::Key_C,Qt::ControlModifier); QCOMPARE(qApp->clipboard()->text(),viewer->result().pdfText.text);
        canvas->setFocus(); QTest::keyClick(canvas,Qt::Key_F,Qt::ControlModifier); QTRY_VERIFY(query->hasFocus());
        files->setFocus(); QTest::keyClick(files,Qt::Key_F,Qt::ControlModifier); QTRY_VERIFY(window.leftPanel()->findChild<QLineEdit *>("filter")->hasFocus());
        bool expandedSearch=false;
        QTimer::singleShot(100,&window,[&] {
            auto *dialog=window.findChild<QDialog *>("viewerDialog"); if (!dialog) return;
            QTimer::singleShot(5000,dialog,&QDialog::reject);
            QVERIFY(QTest::qWaitForWindowExposed(dialog)); dialog->activateWindow(); QTest::qWait(30);
            canvas->setFocus(); QTest::keyClick(canvas,Qt::Key_F,Qt::ControlModifier); expandedSearch=query->hasFocus(); dialog->reject();
        });
        window.findChild<QAction *>("viewFile")->trigger(); QVERIFY(expandedSearch);
    }
};
QTEST_MAIN(PdfTextTest)
#include "pdf_text_test.moc"
