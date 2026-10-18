#include "documenttext.h"
#include "externalprocess.h"
#include "archive.h"
#include "previewwidget.h"
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTest>
class OfficeTextTest final : public QObject {
    Q_OBJECT
    QString zip(const QString &root,const QMap<QString,QByteArray>&parts,const QString &name);
    QByteArray bytes(const QString&path);
private slots:
    void initTestCase();
    void docxPreviewFindAndSourceRemainReadOnly();
    void workbookNamesHiddenSheetsSharedStringsAndCachedFormulas();
    void presentationNumericOrderAndOpenDocumentSpacing();
    void malformedXmlExternalSheetsAndCancellationAreNotComplete();
};

QString OfficeTextTest::zip(const QString &root,const QMap<QString,QByteArray>&parts,const QString &name){
    const auto folder=QDir(root).filePath("content");if(!QDir().mkpath(folder))return {};for(auto i=parts.begin();i!=parts.end();++i){const auto path=QDir(folder).filePath(i.key());if(!QDir().mkpath(QFileInfo(path).absolutePath()))return {};QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(i.value())!=i.value().size())return {};}
    const auto path=QDir(root).filePath(name);ExternalCommand command;command.executable=sevenZipExecutable();command.workingDirectory=folder;command.arguments={"a","-tzip","-mx=1","-r-",path,"."};std::atomic_bool cancel=false;const auto result=runExternal(command,cancel);return result.state==ExternalState::Exited&&result.exitCode==0?path:QString{};
}

QByteArray OfficeTextTest::bytes(const QString&path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return file.readAll();}

void OfficeTextTest::initTestCase(){if(sevenZipExecutable().isEmpty())QSKIP("7-Zip real não está instalado neste Windows.");}

void OfficeTextTest::docxPreviewFindAndSourceRemainReadOnly(){
    QTemporaryDir root;const auto path=zip(root.path(),{{"word/document.xml",R"(<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body><w:p><w:r><w:t>Directory Open &amp; viagens</w:t></w:r></w:p><w:p><w:r><w:t>2027</w:t></w:r></w:p></w:body></w:document>)"},{"word/vbaProject.bin","not executed"}},"demo.docx");QVERIFY(!path.isEmpty());const auto before=bytes(path);const auto result=extractDocumentText(before,"docx",[]{return false;});QVERIFY2(result.complete,qPrintable(result.message));QVERIFY(result.text.contains("Directory Open & viagens"));
    PreviewWidget preview;preview.show();preview.setPath(path);QTRY_COMPARE_WITH_TIMEOUT(preview.result().kind,PreviewKind::Document,10000);QVERIFY(preview.result().text.contains("2027"));QVERIFY(preview.findChild<QComboBox*>("previewEncoding")->isHidden());QCOMPARE(bytes(path),before);
    const auto out=qEnvironmentVariable("DIROPEN_OFFICE_CAPTURES");if(!out.isEmpty()){preview.resize(940,680);QTest::qWait(80);QVERIFY(QDir().mkpath(out));QVERIFY(preview.grab().save(out+"/office-text.png"));}
}

void OfficeTextTest::workbookNamesHiddenSheetsSharedStringsAndCachedFormulas(){
    QTemporaryDir root;const auto path=zip(root.path(),{
        {"xl/workbook.xml",R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Viagem" sheetId="1" state="hidden" r:id="rId1"/></sheets></workbook>)"},
        {"xl/_rels/workbook.xml.rels",R"(<Relationships><Relationship Id="rId1" Target="worksheets/sheet1.xml"/></Relationships>)"},
        {"xl/sharedStrings.xml",R"(<sst><si><t>Lisboa</t></si></sst>)"},
        {"xl/worksheets/sheet1.xml",R"(<worksheet><sheetData><row><c r="A1" t="s"><v>0</v></c><c r="B1"><f>1+2</f><v>3</v></c></row></sheetData></worksheet>)"}},"book.xlsx");QVERIFY(!path.isEmpty());const auto result=extractDocumentText(bytes(path),"xlsx",[]{return false;});QVERIFY2(result.complete,qPrintable(result.message));QVERIFY(result.text.contains("Viagem (oculta)"));QVERIFY(result.text.contains("A1\tLisboa"));QVERIFY(result.text.contains("=1+2 [cache: 3]"));
}

void OfficeTextTest::presentationNumericOrderAndOpenDocumentSpacing(){
    QTemporaryDir root;const auto path=zip(root.path(),{{"ppt/slides/slide10.xml","<slide><p><t>Decimo</t></p></slide>"},{"ppt/slides/slide2.xml","<slide><p><t>Segundo</t></p></slide>"}},"slides.pptx");QVERIFY(!path.isEmpty());const auto result=extractDocumentText(bytes(path),"pptx",[]{return false;});QVERIFY2(result.complete,qPrintable(result.message));QVERIFY(result.text.indexOf("Segundo")<result.text.indexOf("Decimo"));
    QTemporaryDir odf;const auto other=zip(odf.path(),{{"content.xml",R"(<office xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"><text:p>Texto<text:s text:c="4"/>ODF</text:p></office>)"}},"document.odt");QVERIFY(!other.isEmpty());const auto value=extractDocumentText(bytes(other),"odt",[]{return false;});QVERIFY2(value.complete,qPrintable(value.message));QVERIFY(value.text.contains("Texto    ODF"));
}

void OfficeTextTest::malformedXmlExternalSheetsAndCancellationAreNotComplete(){
    QTemporaryDir root;const auto path=zip(root.path(),{{"word/document.xml","<!DOCTYPE x [<!ENTITY secret 'value'>]><document><t>&secret;</t></document>"}},"dtd.docx");QVERIFY(!path.isEmpty());const auto invalid=extractDocumentText(bytes(path),"docx",[]{return false;});QVERIFY(!invalid.complete);QVERIFY(invalid.message.contains("DTD"));const auto cancelled=extractDocumentText(bytes(path),"docx",[]{return true;});QVERIFY(cancelled.cancelled);QVERIFY(cancelled.text.isEmpty());
    QTemporaryDir sheet;const auto external=zip(sheet.path(),{{"xl/workbook.xml","<workbook xmlns:r='urn:relationships'><sheet name='Externa' r:id='rId1'/></workbook>"},{"xl/_rels/workbook.xml.rels","<Relationships><Relationship Id='rId1' TargetMode='External' Target='https://example.invalid/workbook.xml'/></Relationships>"}},"external.xlsx");QVERIFY(!external.isEmpty());const auto notFollowed=extractDocumentText(bytes(external),"xlsx",[]{return false;});QVERIFY(!notFollowed.complete);QVERIFY(notFollowed.text.isEmpty());
}
int main(int argc,char**argv){QApplication app(argc,argv);app.setStyle("Fusion");OfficeTextTest test;return QTest::qExec(&test,argc,argv);}
#include "office_text_test.moc"
