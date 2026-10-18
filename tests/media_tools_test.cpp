#include "mediatools.h"
#include "mediatoolsdialog.h"
#include "mainwindow.h"
#include "filepanel.h"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QScopeGuard>
#include <QTreeView>
#include <QTreeWidget>
#include <QUuid>

class MediaToolsTest:public QObject{
    Q_OBJECT
    static bool write(const QString &p,const QByteArray &b){QFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size();}
    static QByteArray read(const QString &p){QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
    static QByteArray hash(const QString &p){return QCryptographicHash::hash(read(p),QCryptographicHash::Sha256);}
    static MediaOptions options(MediaTask task,const QString &destination){MediaOptions o;o.task=task;o.destination=destination;o.executable=mediaToolExecutable(task==MediaTask::Metadata?"exiftool":task==MediaTask::Images?"magick":"ffmpeg");o.probeExecutable=mediaToolExecutable("ffprobe");if(task==MediaTask::AudioVideo)o.format="mp4";return o;}
    static bool image(const QString &p){QImage i(120,80,QImage::Format_RGB32);{QPainter paint(&i);paint.fillRect(0,0,60,40,QColor(20,80,160));paint.fillRect(60,0,60,40,QColor(220,80,50));paint.fillRect(0,40,60,40,QColor(240,190,70));paint.fillRect(60,40,60,40,QColor(30,150,120));}return i.save(p);}
    static QString guiTemplate(){const auto p=QDir::current().filePath(".cache/media-demo");QDir().mkpath(p);return p+"/demo-XXXXXX";}
    static bool clip(const QString &file){QProcess p;p.start(mediaToolExecutable("ffmpeg"),{"-v","error","-nostdin","-f","lavfi","-i","testsrc=size=120x80:rate=10","-f","lavfi","-i","sine=frequency=400:sample_rate=44100","-t","0.5","-c:v","mpeg4","-c:a","aac","-threads","2",file});return p.waitForFinished(20000)&&p.exitCode()==0;}
private slots:
    void validatesFieldsAndFormats(){QTemporaryDir d;const auto f=d.filePath("photo.jpg");QVERIFY(image(f));std::atomic_bool c{false};auto o=options(MediaTask::Metadata,d.filePath("new"));o.executable=QCoreApplication::applicationFilePath();o.tags["EXIF:DateTimeOriginal"]="2026:99:01 12:00:00";QVERIFY(!inspectMedia({f},o,c).valid());o.tags.clear();o.tags["FileName"]="danger";QVERIFY(!inspectMedia({f},o,c).valid());o=options(MediaTask::Images,d.filePath("new"));o.executable=QCoreApplication::applicationFilePath();o.format="pdf";QVERIFY(!inspectMedia({f},o,c).valid());o.format="png";o.width=20000;QVERIFY(!inspectMedia({f},o,c).valid());}
    void realImageConversionAndIdentity(){if(mediaToolExecutable("magick").isEmpty())QSKIP("ImageMagick ausente");QTemporaryDir d;const auto f=d.filePath("foto ação [1].jpg");QVERIFY(image(f));QVERIFY(write(f+":Zone.Identifier","[ZoneTransfer]\r\nZoneId=3\r\n"));const auto original=hash(f);std::atomic_bool c{false};auto o=options(MediaTask::Images,d.filePath("converted"));o.width=60;o.height=40;o.rotation=90;auto p=inspectMedia({f},o,c);QVERIFY2(p.valid(),qPrintable(p.error));auto r=convertMedia(p,c);QVERIFY2(r.state==ToolState::Completed,qPrintable(r.message+r.diagnostics));QCOMPARE(r.completed,1);QImage output(r.outputs.first());QCOMPARE(output.size(),QSize(40,60));QCOMPARE(hash(f),original);QCOMPARE(read(r.outputs.first()+":Zone.Identifier"),read(f+":Zone.Identifier"));QCOMPARE(convertMedia(p,c).state,ToolState::Failed);
        o.destination=d.filePath("changed");p=inspectMedia({f},o,c);QVERIFY(p.valid());QVERIFY(write(f,"replaced"));r=convertMedia(p,c);QCOMPARE(r.state,ToolState::Incomplete);QVERIFY(r.outputs.isEmpty());}
    void realMetadataCopiesAndUnsupported(){if(mediaToolExecutable("exiftool").isEmpty())QSKIP("ExifTool ausente");QTemporaryDir d;const auto f=d.filePath("foto ação.jpg");QVERIFY(image(f));const auto original=hash(f);std::atomic_bool c{false};auto o=options(MediaTask::Metadata,d.filePath("edited"));o.tags={{"EXIF:DateTimeOriginal","2026:10:16 12:30:00"},{"EXIF:Artist","Autor ação"},{"IPTC:Keywords","Viagem"},{"XMP-dc:Title","Título ação"}};const auto p=inspectMedia({f},o,c);QVERIFY2(p.valid(),qPrintable(p.error));const auto r=convertMedia(p,c);QVERIFY2(r.state==ToolState::Completed,qPrintable(r.message+r.diagnostics));QCOMPARE(hash(f),original);o.tags.clear();const auto after=inspectMedia(r.outputs,o,c);QVERIFY(after.valid());QVERIFY(after.items.first().details.contains("2026:10:16 12:30:00"));QVERIFY(after.items.first().details.contains("Título ação"));}
    void realAudioVideoAndExtraction(){if(mediaToolExecutable("ffmpeg").isEmpty()||mediaToolExecutable("ffprobe").isEmpty())QSKIP("FFmpeg/ffprobe ausentes");QTemporaryDir d;const auto f=d.filePath("vídeo de teste.mp4");QVERIFY(clip(f));const auto original=hash(f);std::atomic_bool c{false};for(const auto &format:{"mp4","mp3","wav","flac"}){auto o=options(MediaTask::AudioVideo,d.filePath(format));o.format=format;const auto p=inspectMedia({f},o,c);QVERIFY2(p.valid(),qPrintable(p.error));QVERIFY(p.items.first().details.contains("codec"));const auto r=convertMedia(p,c);QVERIFY2(r.state==ToolState::Completed,qPrintable(r.message+r.diagnostics));QCOMPARE(r.outputs.size(),1);QVERIFY(QFileInfo(r.outputs.first()).size()>0);}QCOMPARE(hash(f),original);}
    void cancelAndCollisions(){if(mediaToolExecutable("magick").isEmpty())QSKIP("ImageMagick ausente");QTemporaryDir d;QVERIFY(image(d.filePath("same.jpg")));QVERIFY(image(d.filePath("same.png")));std::atomic_bool c{false};auto o=options(MediaTask::Images,d.filePath("new"));QVERIFY(!inspectMedia({d.filePath("same.jpg"),d.filePath("same.png")},o,c).valid());const auto p=inspectMedia({d.filePath("same.jpg")},o,c);QVERIFY(p.valid());c=true;QCOMPARE(convertMedia(p,c).state,ToolState::Cancelled);QVERIFY(!QFileInfo::exists(o.destination));}
    void realUiPreviewConfirmAndSettings(){if(mediaToolExecutable("magick").isEmpty())QSKIP("ImageMagick ausente");QTemporaryDir d(guiTemplate());const auto f=d.filePath("foto.jpg");QVERIFY(image(f));MediaToolsDialog dialog(MediaTask::Images,{}, {f},d.path());dialog.show();dialog.findChild<QSpinBox *>("imageWidth")->setValue(60);auto *preview=dialog.findChild<QPushButton *>("previewMedia");auto *apply=dialog.findChild<QPushButton *>("applyMedia");QTest::mouseClick(preview,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(preview->isEnabled(),15000);QVERIFY2(apply->isEnabled(),qPrintable(dialog.findChild<QLabel *>("mediaStatus")->text()));if(qEnvironmentVariableIsSet("DIROPEN_CAPTURE_MEDIA"))QVERIFY(dialog.grab().save(qEnvironmentVariable("DIROPEN_CAPTURE_MEDIA")+"/image-conversion.png"));const auto output=dialog.findChild<QLineEdit *>("mediaDestination")->text();QTimer::singleShot(0,[&]{auto *box=dialog.findChild<QMessageBox *>();if(box&&box->text().contains(output))box->button(QMessageBox::Yes)->click();});QTest::mouseClick(apply,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(preview->isEnabled(),15000);QVERIFY2(QFileInfo::exists(output+"/foto.png"),qPrintable(dialog.findChild<QLabel *>("mediaStatus")->text()));QVERIFY(!dialog.settings().magick.isEmpty());}
    void realMetadataAndVideoDialogs(){
        if(mediaToolExecutable("exiftool").isEmpty()||mediaToolExecutable("ffmpeg").isEmpty())QSKIP("Ferramentas ausentes");QTemporaryDir d(guiTemplate());
        const auto photo=d.filePath("foto.jpg"),video=d.filePath("video.mp4");QVERIFY(image(photo));QVERIFY(clip(video));
        for(const auto task:{MediaTask::Metadata,MediaTask::AudioVideo}){
            MediaToolsDialog dialog(task,{}, {task==MediaTask::Metadata?photo:video},d.path());dialog.show();
            if(task==MediaTask::Metadata){auto *tag=dialog.findChild<QComboBox *>("metadataTag");tag->setCurrentIndex(tag->findData("XMP-dc:Title"));dialog.findChild<QLineEdit *>("metadataValue")->setText("Viagem de demonstração");dialog.findChild<QPushButton *>("addMetadataEdit")->click();}
            auto *preview=dialog.findChild<QPushButton *>("previewMedia");auto *apply=dialog.findChild<QPushButton *>("applyMedia");QTest::mouseClick(preview,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(preview->isEnabled(),15000);QVERIFY2(apply->isEnabled(),qPrintable(dialog.findChild<QLabel *>("mediaStatus")->text()));
            if(qEnvironmentVariableIsSet("DIROPEN_CAPTURE_MEDIA"))QVERIFY(dialog.grab().save(qEnvironmentVariable("DIROPEN_CAPTURE_MEDIA")+(task==MediaTask::Metadata?"/metadata.png":"/audio-video.png")));
            const auto output=dialog.findChild<QLineEdit *>("mediaDestination")->text();QTimer::singleShot(0,[&]{auto *box=dialog.findChild<QMessageBox *>();if(box&&box->text().contains(output))box->button(QMessageBox::Yes)->click();});QTest::mouseClick(apply,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(preview->isEnabled(),20000);QVERIFY2(QFileInfo::exists(output+(task==MediaTask::Metadata?"/foto.jpg":"/video.mp4")),qPrintable(dialog.findChild<QLabel *>("mediaStatus")->text()));
        }
    }
    void mainWindowMenusAndSessionPaths(){
        QTemporaryDir d;QVERIFY(image(d.filePath("file.jpg")));const auto session=d.filePath("session.json");
        MainWindow window(d.path(),d.path(),false,session);window.show();auto *view=window.leftPanel()->findChild<QTreeView *>("files");QTRY_VERIFY(window.leftPanel()->visibleCount()>=1);view->selectAll();
        for(const auto name:{"editMetadata","convertImages","convertMedia"}){
            view->selectAll();auto *action=window.findChild<QAction *>(name);QVERIFY(action);QTRY_VERIFY(action->isEnabled());bool seen=false;
            QTimer::singleShot(0,[&]{if(auto *dialog=window.findChild<MediaToolsDialog *>()){seen=true;dialog->findChild<QLineEdit *>("mediaExecutable")->setText("C:/Tools/custom-"+QString(name)+".exe");dialog->close();}});action->trigger();QVERIFY(seen);
        }
        const auto settings=window.session().tools;QVERIFY(settings.exiftool.contains("editMetadata"));QVERIFY(settings.magick.contains("convertImages"));QVERIFY(settings.ffmpeg.contains("convertMedia"));QVERIFY(window.close());SessionStore store(session);const auto saved=store.load();QCOMPARE(saved.state,SessionLoadState::Loaded);QCOMPARE(saved.session.tools.exiftool,settings.exiftool);QCOMPARE(saved.session.tools.ffprobe,settings.ffprobe);
    }
    void cancellationKeepsOnlyCompletedAndCleansWork(){
        if(mediaToolExecutable("magick").isEmpty())QSKIP("ImageMagick ausente");QTemporaryDir d;const auto a=d.filePath("a.jpg"),b=d.filePath("b.jpg");QVERIFY(image(a));QVERIFY(image(b));std::atomic_bool cancel{false};auto o=options(MediaTask::Images,d.filePath("output"));const auto plan=inspectMedia({a,b},o,cancel);QVERIFY(plan.valid());int step=0;const auto r=convertMedia(plan,cancel,[&](const QString &){if(++step==2)cancel=true;});QCOMPARE(r.state,ToolState::Cancelled);QCOMPARE(r.completed,1);QVERIFY(QFileInfo::exists(o.destination+"/a.png"));QVERIFY(!QFileInfo::exists(o.destination+"/b.png"));QVERIFY(QDir(o.destination).entryList(QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty());
    }
    void benchmarkWorkflow(){
        const auto root=qEnvironmentVariable("DIROPEN_MEDIA_BENCH_ROOT");if(root.isEmpty())QSKIP("Benchmark opcional, fixture marcada.");QVERIFY(read(root+"/.diropen-media-fixture")=="Directory Open media benchmark v1");
        const auto scenario=qEnvironmentVariable("DIROPEN_MEDIA_BENCH_SCENARIO");const auto method=qEnvironmentVariable("DIROPEN_MEDIA_BENCH_ENGINE");QVERIFY(method=="cli"||method=="diropen");
        const auto task=root+"/"+QUuid::createUuid().toString(QUuid::WithoutBraces);QVERIFY(QDir().mkdir(task));std::atomic_bool cancel{false};
        const auto kind=scenario=="metadata"?MediaTask::Metadata:scenario=="images"?MediaTask::Images:MediaTask::AudioVideo;auto o=options(kind,task+"/outputs");QStringList sources;
        if(kind==MediaTask::AudioVideo){o.format="mp4";sources={root+"/source/video.mp4"};}else for(int i=0;i<4;++i)sources<<root+"/source/image-"+QString::number(i)+".jpg";
        if(kind==MediaTask::Metadata)o.tags={{"XMP-dc:Title","Benchmark fixture"}};if(kind==MediaTask::Images){o.width=600;o.height=400;o.rotation=90;}
        if(method=="diropen"){const auto plan=inspectMedia(sources,o,cancel);QVERIFY2(plan.valid(),qPrintable(plan.error));const auto r=convertMedia(plan,cancel);QVERIFY2(r.state==ToolState::Completed,qPrintable(r.message+r.diagnostics));QCOMPARE(r.completed,sources.size());}
        else{
            QVERIFY(QDir().mkdir(o.destination));
            auto cli=[&](const QString &exe,const QStringList &args,const QByteArray &input=QByteArray()){QProcess p;p.start(exe,args);if(!p.waitForStarted())return false;if(!input.isEmpty())p.write(input);p.closeWriteChannel();if(!p.waitForFinished(30000)){p.kill();p.waitForFinished();return false;}return p.exitStatus()==QProcess::NormalExit&&p.exitCode()==0;};
            for(const auto &source:sources){const auto output=o.destination+"/"+QFileInfo(source).completeBaseName()+"."+(kind==MediaTask::Metadata?"jpg":o.format);
                if(kind==MediaTask::Metadata){const auto prefix=QStringList{"-config","","-charset","filename=UTF8","-@","-"};QVERIFY(cli(o.executable,prefix,("-j\n-G1\n-s\n--\n"+source+'\n').toUtf8()));QVERIFY(cli(o.executable,prefix,("-o\n"+output+"\n-XMP-dc:Title=Benchmark fixture\n--\n"+source+'\n').toUtf8()));QVERIFY(cli(o.executable,prefix,("-j\n-G1\n-s\n--\n"+output+'\n').toUtf8()));}
                else if(kind==MediaTask::Images){const QStringList limits{"-limit","thread","2","-limit","memory","256MiB","-limit","map","512MiB","-limit","disk","1GiB"};QVERIFY(cli(o.executable,QStringList{"identify","-ping","-format","%w %h"}+limits+QStringList{"jpeg:"+source+"[0]"}));const auto transform=limits+QStringList{"jpeg:"+source+"[0]","-auto-orient","-resize","600x400>","-rotate","90","-quality","90"};if(source==sources.first())QVERIFY(cli(o.executable,transform+QStringList{"-thumbnail","480x320>","png:-"}));QVERIFY(cli(o.executable,transform+QStringList{"png:"+output}));QVERIFY(cli(o.executable,QStringList{"identify"}+limits+QStringList{"png:"+output+"[0]"}));}
                else{const QString formats="mov,mp4,m4a,3gp,3g2,mj2,matroska,webm,avi,mp3,wav,flac,aac,ogg,asf,mpeg,mpegts";QVERIFY(cli(o.probeExecutable,{"-v","error","-protocol_whitelist","file,pipe","-format_whitelist",formats,"-show_format","-show_streams","-of","json",source}));QVERIFY(cli(o.executable,{"-hide_banner","-loglevel","error","-nostdin","-n","-threads","2","-protocol_whitelist","file,pipe","-format_whitelist",formats,"-i",source,"-map","0:v:0","-map","0:a:0?","-c:v","libx264","-preset","medium","-crf","23","-pix_fmt","yuv420p","-vf","pad=ceil(iw/2)*2:ceil(ih/2)*2","-c:a","aac","-b:a","192k","-movflags","+faststart","-map_metadata","0","-threads","2","-filter_threads","2",output}));QVERIFY(cli(o.probeExecutable,{"-v","error","-protocol_whitelist","file,pipe","-format_whitelist",formats,"-show_format","-show_streams","-of","json",output}));QVERIFY(cli(o.executable,{"-v","error","-xerror","-nostdin","-threads","2","-protocol_whitelist","file,pipe","-format_whitelist",formats,"-i",output,"-f","null","-"}));}
                QVERIFY(QFileInfo(output).size()>0);
            }
        }
    }
};
QTEST_MAIN(MediaToolsTest)
#include "media_tools_test.moc"
