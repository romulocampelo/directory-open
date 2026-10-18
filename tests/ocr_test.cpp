#include "ocr.h"
#include "ocrdialog.h"
#include "previewwidget.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
class OcrTest final:public QObject{
    Q_OBJECT
    QImage sample(){QImage image(1200,300,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);QPainter painter(&image);painter.setPen(Qt::black);painter.setFont(QFont("Arial",42));painter.drawText(image.rect().adjusted(45,20,-30,-20),Qt::AlignVCenter,"Directory Open 2027");return image;}
    OcrResult run(const OcrRequest &request){OcrJob job(request);job.start();if(!job.wait(35000)){job.cancel();job.wait();QTest::qFail("O OCR não concluiu a tarefa limitada.",__FILE__,__LINE__);}return job.takeResult();}
private slots:
    void installedEngineRecognizesOwnedImage(){
        const auto languages=run({{}, {},true});if(languages.state==OcrState::Unavailable)QSKIP(qPrintable(languages.message));QCOMPARE(languages.state,OcrState::Completed);QVERIFY(!languages.languages.isEmpty());const auto result=run({sample(),languages.languages.first(),false});QCOMPARE(result.state,OcrState::Completed);QVERIFY2(result.text.contains("Directory",Qt::CaseInsensitive),qPrintable(result.text+result.message));QVERIFY(result.text.contains("2027"));QVERIFY(!result.language.isEmpty());QCOMPARE(result.recognizedSize,QSize(1200,300));
    }
    void boundsUnavailableLanguageAndCancellation(){
        std::atomic_bool cancel=true;QCOMPARE(recognizePreview({sample()},cancel).state,OcrState::Cancelled);cancel=false;QVERIFY(recognizePreview({},cancel).state!=OcrState::Completed);const auto result=run({sample(),"xx-invalid-language",false});QVERIFY(result.state==OcrState::Unavailable||result.state==OcrState::Failed);QVERIFY(result.text.isEmpty());
    }
    void factoryReferencesBelongToEachShortWorker(){
        for(int i=0;i<4;++i){const auto result=run({{}, {},true});QVERIFY(result.state==OcrState::Completed||result.state==OcrState::Unavailable);}
        OcrJob first({{}, {},true}),second({{}, {},true});first.start();second.start();QVERIFY(first.wait(10000));QVERIFY(second.wait(10000));QVERIFY(first.takeResult().state!=OcrState::Failed);QVERIFY(second.takeResult().state!=OcrState::Failed);
    }
    void dialogRunsCopiesAndLeavesSourcePixelsUntouched(){
        const auto image=sample();const auto bytes=image.cacheKey();OcrDialog dialog(image,"Imagem de demonstração",nullptr);dialog.show();auto *start=dialog.findChild<QPushButton*>("startOcr");auto *status=dialog.findChild<QLabel*>("ocrStatus");QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled()||status->text().contains("não"),10000);if(!start->isEnabled())QSKIP(qPrintable(status->text()));start->click();auto *copy=dialog.findChild<QPushButton*>("copyOcrText");QTRY_VERIFY_WITH_TIMEOUT(copy->isEnabled(),35000);copy->click();QVERIFY(qApp->clipboard()->text().contains("2027"));QCOMPARE(image.cacheKey(),bytes);
        const auto out=qEnvironmentVariable("DIROPEN_OCR_CAPTURES");if(!out.isEmpty()){QVERIFY(QDir().mkpath(out));QVERIFY(dialog.grab().save(out+"/ocr-windows.png"));}QMetaObject::invokeMethod(&dialog,"reject");
    }
};
int main(int argc,char **argv){QApplication app(argc,argv);app.setStyle("Fusion");QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR")+"/Fonts/arial.ttf");OcrTest test;return QTest::qExec(&test,argc,argv);}
#include "ocr_test.moc"
