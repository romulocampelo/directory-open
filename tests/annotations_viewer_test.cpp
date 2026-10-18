#include "annotationsdialog.h"
#include "batchrename.h"
#include "filepanel.h"
#include "mainwindow.h"
#include "previewwidget.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QLockFile>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <QFileSystemModel>
#include <QItemSelectionModel>
class AnnotationsViewerTest final:public QObject{
    Q_OBJECT
    void text(const QString &path){QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write("original content");}
    AnnotationRecord record(const QString &path){AnnotationRecord r;r.path=path;r.key=annotationIdentity(inspectRenameItem(path).identity);r.before.path=path;r.value=r.before;return r;}
private slots:
    void catalogueTracksIdentityPreservesOriginalAndMergesOtherFiles(){
        QTemporaryDir root;const auto a=root.filePath("a.txt"),b=root.filePath("b.txt"),catalog=root.filePath("notes.json");text(a);text(b);auto first=record(a),second=record(b);first.value.label="Revisar";first.value.tags={"viagem","2027"};first.value.rating=4;first.value.status="Manter";first.value.description="Nota independente";first.value.color="#225588";std::atomic_bool cancel=false;
        QVERIFY(saveAnnotations(catalog,{first},cancel).isEmpty());QCOMPARE(loadAnnotations(catalog).catalog.entries[first.key].rating,4);second.value.label="Outro";QVERIFY(saveAnnotations(catalog,{second},cancel).isEmpty());QCOMPARE(loadAnnotations(catalog).catalog.entries.size(),2);QVERIFY(QFile::exists(catalog+".bak"));QFile original(a);QVERIFY(original.open(QIODevice::ReadOnly));QCOMPARE(original.readAll(),QByteArray("original content"));original.close();const auto renamed=root.filePath("renamed.txt");QVERIFY(QFile::rename(a,renamed));const auto identity=annotationIdentity(inspectRenameItem(renamed).identity);QCOMPARE(identity,first.key);QCOMPARE(loadAnnotations(catalog).catalog.entries[identity].label,QString("Revisar"));text(a);QVERIFY(annotationIdentity(inspectRenameItem(a).identity)!=first.key);
    }
    void staleNotesReplacedSourcesLocksAndMalformedFailWithoutOverwrite(){
        QTemporaryDir root;const auto path=root.filePath("item.txt"),catalog=root.filePath("notes.json");text(path);auto before=record(path);auto changed=before;changed.value.label="Primeiro";std::atomic_bool cancel=false;QVERIFY(saveAnnotations(catalog,{changed},cancel).isEmpty());before.value.label="Obsoleto";QVERIFY(!saveAnnotations(catalog,{before},cancel).isEmpty());QCOMPARE(loadAnnotations(catalog).catalog.entries[before.key].label,QString("Primeiro"));
        auto current=record(path);current.before=loadAnnotations(catalog).catalog.entries[current.key];current.value=current.before;current.value.rating=5;{QLockFile lock(catalog+".lock");QVERIFY(lock.tryLock(0));QVERIFY(!saveAnnotations(catalog,{current},cancel).isEmpty());}
        QVERIFY(QFile::rename(path,root.filePath("old.txt")));text(path);QVERIFY(!saveAnnotations(catalog,{current},cancel).isEmpty());QFile corrupt(catalog);QVERIFY(corrupt.open(QIODevice::WriteOnly|QIODevice::Truncate));corrupt.write("{invalid");corrupt.close();QVERIFY(!loadAnnotations(catalog).error.isEmpty());QVERIFY(!saveAnnotations(catalog,{record(path)},cancel).isEmpty());QVERIFY(corrupt.open(QIODevice::ReadOnly));QCOMPARE(corrupt.readAll(),QByteArray("{invalid"));
    }
    void cancellationAndVolatileNotesAreExplicit(){
        QTemporaryDir root;const auto path=root.filePath("a.txt");text(path);auto r=record(path);r.value.rating=3;std::atomic_bool cancel=true;AnnotationCatalog memory;QVERIFY(!saveAnnotations({}, {r},cancel,&memory).isEmpty());QVERIFY(memory.entries.isEmpty());cancel=false;QVERIFY(saveAnnotations({}, {r},cancel,&memory).isEmpty());QCOMPARE(memory.entries[r.key].rating,3);QVERIFY(!QFile::exists(root.filePath("notes.json")));r.value.tags={"tag","TAG"};QVERIFY(!validateAnnotation(r.value).isEmpty());
    }
    void triageStagesAcrossNavigationAndSavesOnlyOnConfirmation(){
        QTemporaryDir root;const auto a=root.filePath("foto1.png"),b=root.filePath("foto2.png"),catalog=root.filePath("notes.json");QImage image(200,120,QImage::Format_RGB32);image.fill(Qt::blue);QVERIFY(image.save(a));QVERIFY(image.save(b));
        {AnnotationsDialog d(catalog,{a,b},{},{},nullptr);d.show();auto *list=d.findChild<QListWidget*>("annotationFiles");QTRY_COMPARE(list->count(),2);QTRY_VERIFY(list->isEnabled());QTRY_COMPARE(d.findChild<PreviewWidget*>("filePreview")->result().kind,PreviewKind::Image);d.findChild<QLineEdit*>("annotationTags")->setText("viagem; paisagem");list->setCurrentRow(1);d.findChild<QSpinBox*>("annotationRating")->setValue(5);d.findChild<QPushButton*>("markKeep")->click();QVERIFY(!QFile::exists(catalog));d.findChild<QPushButton*>("saveAnnotations")->click();QTRY_COMPARE(d.result(),int(QDialog::Accepted));}
        const auto values=loadAnnotations(catalog);QVERIFY(values.error.isEmpty());QCOMPARE(values.catalog.entries[record(a).key].tags,QStringList({"viagem","paisagem"}));QCOMPARE(values.catalog.entries[record(b).key].rating,5);QCOMPARE(values.catalog.entries[record(b).key].status,QString("Manter"));
        {AnnotationsDialog d(catalog,{a,b},{},{},nullptr);d.show();QTRY_COMPARE(d.findChild<QListWidget*>("annotationFiles")->count(),2);d.findChild<QPushButton*>("markReject")->click();QMetaObject::invokeMethod(&d,"reject");}QCOMPARE(loadAnnotations(catalog).catalog.entries[record(a).key].status,QString{});QVERIFY(QFile::exists(a));QVERIFY(QFile::exists(b));
    }
    void viewerPreferencesApplyToTextAndPersistThroughSession(){
        QTemporaryDir root;const auto path=root.filePath("session.json");ViewerSettings settings;settings.wrapText=true;settings.textSize=15;settings.imageEdge=1536;const auto parsed=decodeViewerSettings(encodeViewerSettings(settings));QVERIFY(parsed);auto bad=encodeViewerSettings(settings);bad["textSize"]=11.5;QVERIFY(!decodeViewerSettings(bad));
        {MainWindow w(root.path(),root.path(),false,path);w.show();QTimer::singleShot(0,&w,[&]{auto *d=w.findChild<QDialog*>("viewerSettingsDialog");QVERIFY(d);auto guard=qScopeGuard([d]{if(d->isVisible())d->reject();});d->findChild<QCheckBox*>("viewerWrapText")->setChecked(true);d->findChild<QSpinBox*>("viewerTextSize")->setValue(15);d->findChild<QComboBox*>("viewerImageQuality")->setCurrentIndex(0);d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});w.findChild<QAction*>("viewerSettings")->trigger();QCOMPARE(w.session().ui.viewer.textSize,15);auto *text=w.findChild<QPlainTextEdit*>("previewText");QCOMPARE(text->lineWrapMode(),QPlainTextEdit::WidgetWidth);QCOMPARE(text->font().pointSize(),15);QVERIFY(w.close());}
        MainWindow restored({}, {},false,path);QCOMPARE(restored.session().ui.viewer.imageEdge,1536);QVERIFY(restored.session().ui.viewer.wrapText);QVERIFY(restored.close());
    }
    void configuredImageQualityReachesDecoder(){
        QTemporaryDir root;const auto path=root.filePath("large.png");QImage image(3000,2200,QImage::Format_RGB32);image.fill(Qt::red);QVERIFY(image.save(path));PreviewWidget preview;ViewerSettings options;options.imageEdge=1536;preview.setSettings(options);preview.setPath(path);QTRY_COMPARE_WITH_TIMEOUT(preview.result().kind,PreviewKind::Image,10000);QVERIFY(preview.result().image.width()<=1536);QVERIFY(preview.result().image.height()<=1536);QCOMPARE(preview.result().originalSize,QSize(3000,2200));
    }
    void defaultEncodingIsUsedWhenOpeningAnotherFile(){
        QTemporaryDir root;const auto path=root.filePath("legacy.txt");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QByteArray::fromHex("636166e9"));file.close();PreviewWidget preview;ViewerSettings settings;settings.encoding=int(PreviewEncoding::Windows1252);preview.setSettings(settings);preview.setPath(path);QTRY_COMPARE(preview.result().kind,PreviewKind::Text);QCOMPARE(preview.result().encoding,QString("Windows-1252"));QCOMPARE(preview.result().text,QString::fromUtf8("café"));
    }
    void nativeCapture(){
        const auto out=qEnvironmentVariable("DIROPEN_ANNOTATION_CAPTURES");if(out.isEmpty())QSKIP("Captura opcional.");QVERIFY(QDir().mkpath(out));QTemporaryDir root(QDir(out).filePath("demonstracao-XXXXXX"));QImage image(800,500,QImage::Format_RGB32);image.fill(QColor("#477b9f"));const auto a=root.filePath("Paisagem.png"),b=root.filePath("Outra-imagem.png");QVERIFY(image.save(a));image.fill(QColor("#567e56"));QVERIFY(image.save(b));MainWindow w(root.path(),root.path(),false);w.show();QTimer::singleShot(0,&w,[&]{auto *d=w.findChild<QDialog*>("viewerSettingsDialog");QVERIFY(d);auto guard=qScopeGuard([d]{if(d->isVisible())d->reject();});QTest::qWait(80);QVERIFY(d->grab().save(out+"/viewer-settings.png"));d->reject();});w.findChild<QAction*>("viewerSettings")->trigger();AnnotationsDialog d({}, {a,b},{},{},&w);d.show();QTRY_COMPARE(d.findChild<PreviewWidget*>("filePreview")->result().kind,PreviewKind::Image);d.findChild<QSpinBox*>("annotationRating")->setValue(4);d.findChild<QLineEdit*>("annotationLabel")->setText("Viagem");d.findChild<QLineEdit*>("annotationTags")->setText("paisagem; revisar");d.findChild<QPushButton*>("stageAnnotations")->click();QTest::qWait(150);QVERIFY(d.grab().save(out+"/classification-triage.png"));QMetaObject::invokeMethod(&d,"reject");
    }
};
int main(int argc,char**argv){QApplication app(argc,argv);app.setStyle("Fusion");AnnotationsViewerTest test;return QTest::qExec(&test,argc,argv);}
#include "annotations_viewer_test.moc"
