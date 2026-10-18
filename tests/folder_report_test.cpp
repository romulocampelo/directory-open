#include "foldersize.h"
#include "isolatedsettings.h"
#include <QScopeGuard>
#include "foldersizedialog.h"
#include "folderreport.h"
#include "folderreportpreview.h"
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPdfDocument>
#include <QPdfWriter>
#include <QPointer>
#include <QPrintPreviewWidget>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>
#include <QWindow>
#include <windows.h>
#include <psapi.h>

namespace {
quint64 cpuTime() {
    FILETIME c{},e{},k{},u{}; GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);
    auto value=[](FILETIME f){return (quint64(f.dwHighDateTime)<<32)|f.dwLowDateTime;};return value(k)+value(u);
}
}
class FolderReportTest final : public QObject {
    Q_OBJECT
    void write(const QString &path,int bytes=7) {QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly|QIODevice::NewOnly));QCOMPARE(f.write(QByteArray(bytes,'x')),qint64(bytes));}
    FolderSizeResult read(FolderSizeRequest request){std::atomic_bool cancel{false};return calculateFolderSize(request,cancel);}
    QByteArray contents(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
    QString artifact(const QString &name){const auto dir=qEnvironmentVariable("DIROPEN_REPORT_ARTIFACTS");return dir.isEmpty()?QString{}:dir+'/'+name;}
private slots:
    void settingsInitializationFailuresAndAbruptReceipt() {
        QTemporaryDir work(QDir::currentPath()+"/settings-fault-XXXXXX"); QVERIFY(work.isValid());
        for (const auto &fault : {"open","create","redirect","probe","app","cleanup","delete","abrupt"}) {
            QProcess child; child.setWorkingDirectory(work.path());
            child.start(QCoreApplication::applicationFilePath(),{QString("--settings-fault=")+fault});
            QVERIFY(child.waitForFinished(30000));
            const int expected=QByteArray(fault)=="abrupt"?76:QByteArray(fault)=="app"?75:(QByteArray(fault)=="cleanup" || QByteArray(fault)=="delete")?74:73;
            QCOMPARE(child.exitCode(),expected);
            const auto dir=work.path()+"/.cache/test-settings";
            const auto receipts=QDir(dir).entryList({"*.json"},QDir::Files);
            if (QByteArray(fault)!="abrupt") { QVERIFY(receipts.isEmpty()); continue; }
            QCOMPARE(receipts.size(),1);
            const auto path=dir+'/'+receipts.front();
            QFile receipt(path); QVERIFY(receipt.open(QIODevice::ReadOnly)); const auto original=receipt.readAll(); receipt.close();
            auto wrong=QJsonDocument::fromJson(original).object(); wrong["owner"]=QUuid::createUuid().toString(QUuid::WithoutBraces);
            QVERIFY(receipt.open(QIODevice::WriteOnly|QIODevice::Truncate)); receipt.write(QJsonDocument(wrong).toJson()); receipt.close();
            QVERIFY(!IsolatedSettings::recover(path)); QVERIFY(QFile::exists(path));
            QVERIFY(receipt.open(QIODevice::WriteOnly|QIODevice::Truncate)); receipt.write(original); receipt.close();
            QVERIFY(IsolatedSettings::recover(path)); QVERIFY(!QFile::exists(path));
            const auto key=QJsonDocument::fromJson(original).object()["key"].toString();
            HKEY user=nullptr, absent=nullptr; QCOMPARE(RegOpenCurrentUser(KEY_READ,&user),LSTATUS(ERROR_SUCCESS));
            const auto status=RegOpenKeyExW(user,key.toStdWString().c_str(),0,KEY_READ,&absent);
            if(absent)RegCloseKey(absent); RegCloseKey(user); QCOMPARE(status,LSTATUS(ERROR_FILE_NOT_FOUND));
        }
    }
    void cancelledSelectorsLeaveStableResultAndNoOutput() {
        QTemporaryDir t; write(t.path()+"/demo",21);
        QPointer<FolderSizeDialog> dialog=new FolderSizeDialog({{t.path()}});
        const auto cleanup=qScopeGuard([&]{delete dialog.data();});
        dialog->show(); auto *start=dialog->findChild<QPushButton*>("startFolderSize"); start->click(); QTRY_VERIFY(start->isEnabled());
        auto *job=dynamic_cast<FolderSizeJob*>(dialog->findChild<QThread*>()); QVERIFY(job); const auto stable=job->result();
        const bool native=QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        const auto restore=qScopeGuard([=]{QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs,native);});
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
        int cancelled=0; QTimer cancel;
        connect(&cancel,&QTimer::timeout,this,[&]{for(auto *widget:QApplication::topLevelWidgets())
            if(auto *file=qobject_cast<QFileDialog*>(widget);file && file->isVisible()){++cancelled;file->reject();}});
        cancel.start(30);
        dialog->findChild<QPushButton*>("exportFolderCsv")->click();
        dialog->findChild<QPushButton*>("exportFolderText")->click();
        FolderReportPreview preview(stable); preview.show(); preview.findChild<QPushButton*>("saveFolderPdf")->click(); preview.close(); cancel.stop();
        QCOMPARE(cancelled,3); QCOMPARE(job->result().bytes,stable.bytes); QCOMPARE(QDir(t.path()).entryList(QDir::Files).size(),1);
    }
    void optInAndSeparateRetentionCoverage() {
        QTemporaryDir t;write(t.path()+"/ação.txt",13);QVERIFY(QDir(t.path()).mkdir("vazia"));
        auto r=read({{t.path()}});QCOMPARE(r.listingState,FolderListingState::NotRequested);QVERIFY(r.listing.isEmpty());
        FolderSizeRequest request{{t.path()}};request.collectListing=true;
        r=read(request);QCOMPARE(r.state,FolderSizeState::Complete);QCOMPARE(r.listingState,FolderListingState::Complete);
        QCOMPARE(r.listing.size(),3);QCOMPARE(r.listingObserved,quint64(3));QCOMPARE(r.bytes,quint64(13));
        quint64 sum=0;int dirs=0;for(const auto &e:r.listing){if(e.directory){++dirs;QCOMPARE(e.bytes,quint64(0));}else sum+=e.bytes;}
        QCOMPARE(sum,r.bytes);QCOMPARE(dirs,2);
        request.maxListingEntries=1;r=read(request);QCOMPARE(r.state,FolderSizeState::Complete);QCOMPARE(r.listingState,FolderListingState::Limited);
        QCOMPARE(r.bytes,quint64(13));QCOMPARE(r.listing.size(),1);QCOMPARE(r.listingObserved,quint64(3));QVERIFY(!r.listingLimitReason.isEmpty());
        QVERIFY(folderReportCoverage(r).contains("INCOMPLETA DA LISTAGEM"));
        request.maxListingEntries=10000;request.maxListingBytes=1;r=read(request);
        QCOMPARE(r.state,FolderSizeState::Complete);QCOMPARE(r.listingState,FolderListingState::Limited);QVERIFY(r.listing.isEmpty());QCOMPARE(r.listingBytes,quint64(0));
    }
    void listingHasSameRootUnionAndHardLinkPolicy() {
        QTemporaryDir t;QVERIFY(QDir(t.path()).mkdir("sub"));write(t.path()+"/sub/file",11);
        const auto original=QDir::toNativeSeparators(t.path()+"/sub/file").toStdWString();const auto link=QDir::toNativeSeparators(t.path()+"/hard").toStdWString();
        QVERIFY(CreateHardLinkW(link.c_str(),original.c_str(),nullptr));
        FolderSizeRequest q{{t.path()+"/sub",t.path(),t.path()}};q.collectListing=true;auto r=read(q);
        QCOMPARE(r.bytes,quint64(22));QCOMPARE(r.files,quint64(2));QCOMPARE(r.listing.size(),4);QCOMPARE(r.roots.size(),1);QCOMPARE(r.overlaps,quint64(2));
        QCOMPARE(r.request.roots,q.roots);QCOMPARE(r.listingState,FolderListingState::Complete);
        const auto junction=t.path()+"/cycle";
        QCOMPARE(QProcess::execute("cmd.exe",{"/d","/c","mklink","/J",QDir::toNativeSeparators(junction),QDir::toNativeSeparators(t.path())}),0);
        r=read(q);QVERIFY(QDir().rmdir(junction));
        QCOMPARE(r.state,FolderSizeState::Partial);QCOMPARE(r.listingState,FolderListingState::Partial);
        QCOMPARE(r.skippedLinks,quint64(1));QCOMPARE(r.listing.size(),4);QCOMPARE(r.bytes,quint64(22));
    }
    void terminalStatesStayExplicitInBothFormats() {
        QTemporaryDir t;write(t.path()+"/demo",9);
        FolderSizeRequest q{{t.path()}};q.collectListing=true;std::atomic_bool cancel{true};
        const auto cancelled=calculateFolderSize(q,cancel);QCOMPARE(cancelled.listingState,FolderListingState::Cancelled);
        q.maxEntries=1;const auto limited=read(q);QCOMPARE(limited.listingState,FolderListingState::Limited);
        q.maxEntries=100;q.roots.append(t.path()+"/missing");const auto partial=read(q);QCOMPARE(partial.listingState,FolderListingState::Partial);
        int n=0;for(const auto &r:{cancelled,limited,partial})for(const auto format:{FolderReportFormat::Csv,FolderReportFormat::Text}){
            QString error;const auto path=t.path()+QString("/out%1").arg(n++);QVERIFY2(writeFolderReport(r,path,format,&error),qPrintable(error));
            const auto bytes=contents(path);QVERIFY(bytes.contains("SUBTOTAL"));QVERIFY(bytes.contains("não é total"));QVERIFY(bytes.contains("Início UTC"));QVERIFY(bytes.contains("Limite entradas"));
            QVERIFY(!bytes.contains("Tamanho lógico observado bytes"));
        }
        FolderSizeResult running;QString error;QVERIFY(!writeFolderReport(running,t.path()+"/running",FolderReportFormat::Csv,&error));QVERIFY(!QFileInfo::exists(t.path()+"/running"));
    }
    void stableJobResultSurvivesSourceRemovalAndRecalculation() {
        QTemporaryDir t;QVERIFY(QDir(t.path()).mkdir("demo"));write(t.path()+"/demo/file",17);
        FolderSizeRequest q{{t.path()+"/demo"}};q.collectListing=true;FolderSizeJob job(q);job.start();QVERIFY(job.wait(10000));
        const auto stable=job.result();QCOMPARE(stable.bytes,quint64(17));QVERIFY(QFile::remove(t.path()+"/demo/file"));QVERIFY(QDir().rmdir(t.path()+"/demo"));
        QString error;QVERIFY(writeFolderReport(stable,t.path()+"/before.txt",FolderReportFormat::Text,&error));
        const auto now=read(q);QCOMPARE(now.state,FolderSizeState::Partial);
        QVERIFY(writeFolderReport(stable,t.path()+"/after.txt",FolderReportFormat::Text,&error));QCOMPARE(contents(t.path()+"/before.txt"),contents(t.path()+"/after.txt"));
        QVERIFY(writeFolderReportPdf(stable,t.path()+"/stable.pdf",&error));QCOMPARE(job.result().bytes,quint64(17));
    }
    void unicodeDelimitersControlsFormulasAndExclusiveOutputs() {
        QTemporaryDir t;FolderSizeRequest q{{t.path()}};q.collectListing=true;auto r=read(q);
        // Synthetic text exercises characters Windows filenames cannot contain.
        r.details={QString::fromUtf8("=HYPERLINK(\"x\");\r\nação,测试"),"   +SUM(1,2)","\t@cmd", "-1+2", "'literal",QString(QChar(0x202e))+"=cmd"};
        r.listing.append({QString::fromUtf8("C:/ação,\"测试\"/linha\r\nsegunda.txt"),false,123});
        const auto csvPath=artifact("escaping.csv").isEmpty()?t.path()+"/out.csv":artifact("escaping.csv");
        QString error;QVERIFY2(writeFolderReport(r,csvPath,FolderReportFormat::Csv,&error),qPrintable(error));
        const auto csv=contents(csvPath);QVERIFY(csv.startsWith(QByteArray::fromHex("efbbbf")));QVERIFY(csv.contains("\"'=HYPERLINK(\"\"x\"\")"));
        QVERIFY(csv.contains("\"'   +SUM(1,2)\""));QVERIFY(csv.contains("\"'\t@cmd\""));QVERIFY(csv.contains("\"'-1+2\""));QVERIFY(csv.contains("\"''literal\""));
        QVERIFY(csv.contains("ação,\"\"测试\"\"/linha\r\nsegunda.txt"));
        QVERIFY(!writeFolderReport(r,csvPath,FolderReportFormat::Csv,&error));QCOMPARE(contents(csvPath),csv);
        const auto text=t.path()+"/out.txt";QVERIFY(writeFolderReport(r,text,FolderReportFormat::Text,&error));
        QVERIFY(contents(text).contains("linha\\r\\nsegunda.txt"));QVERIFY(contents(text).contains("\\u202e=cmd"));
        QVERIFY(contents(text).contains("'   +SUM(1,2)"));
        const auto pdf=t.path()+"/out.pdf";QVERIFY(writeFolderReportPdf(r,pdf,&error));const auto saved=contents(pdf);
        QVERIFY(!writeFolderReportPdf(r,pdf,&error));QCOMPARE(contents(pdf),saved);
        QVERIFY(!writeFolderReport(r,t.path()+"/missing/out",FolderReportFormat::Text,&error));
        QVERIFY(!writeFolderReport(r,t.path()+"/NUL.txt",FolderReportFormat::Text,&error));
        QVERIFY(!writeFolderReport(r,csvPath+":stream",FolderReportFormat::Csv,&error));QCOMPARE(contents(csvPath),csv);
    }
    void boundedPrintingRejectsExcessivePagesAndSmallPaper() {
        QTemporaryDir t;auto r=read({{t.path()}});QString error;
        QPdfWriter tiny(t.path()+"/tiny.pdf");tiny.setPageSize(QPageSize(QSizeF(40,40),QPageSize::Millimeter));
        QVERIFY(!paintFolderReport(r,tiny,&error));QVERIFY(error.contains("pequena"));
        r.listingState=FolderListingState::Complete;
        const QString longPath="C:/"+QString(4000,'a');
        for(int i=0;i<1000;++i)r.listing.append({longPath,false,1});
        QPdfWriter huge(t.path()+"/huge.pdf");huge.setPageSize(QPageSize(QPageSize::A4));
        QVERIFY(!paintFolderReport(r,huge,&error));QVERIFY(error.contains("500 páginas"));
    }
    void paginatedPdfCoverageAndPreviewNative() {
        QTemporaryDir t;for(int i=0;i<180;++i)write(t.path()+QString("/ação-%1-comprimento-para-demonstrar-a-quebra-de-linhas.txt").arg(i),i+1);
        FolderSizeRequest q{{t.path(),t.path()+"/ausente"}};q.collectListing=true;auto r=read(q);QCOMPARE(r.state,FolderSizeState::Partial);
        const auto pdf=artifact("report-partial.pdf").isEmpty()?t.path()+"/report.pdf":artifact("report-partial.pdf");
        QString error;QVERIFY2(writeFolderReportPdf(r,pdf,&error),qPrintable(error));QPdfDocument document;
        QCOMPARE(document.load(pdf),QPdfDocument::Error::None);QVERIFY(document.pageCount()>3);
        for(int i=0;i<document.pageCount();++i){const auto text=document.getAllText(i).text();QVERIFY2(text.contains("COBERTURA INCOMPLETA"),qPrintable(text));QVERIFY(text.contains(QString("Página %1").arg(i+1)));}
        FolderReportPreview preview(r);preview.show();auto *pages=preview.findChild<QPrintPreviewWidget*>("reportPages");QVERIFY(pages);QTRY_VERIFY(pages->pageCount()>3);
        QCOMPARE(pages->pageCount(),document.pageCount());
        QCOMPARE(preview.findChild<QLabel*>("reportCoverage")->text(),folderReportCoverage(r));
        if(QGuiApplication::platformName()=="windows"){
            QVERIFY(QTest::qWaitForWindowExposed(&preview)); pages->fitInView(); QTest::qWait(250);
            if(!artifact("preview-native.png").isEmpty())QVERIFY(preview.grab().save(artifact("preview-native.png")));
            auto *view=pages->findChild<QGraphicsView*>();QVERIFY(view);
            const auto image=view->viewport()->grab().toImage();int ink=0;
            for(int y=image.height()/5;y<image.height()*3/4;++y)for(int x=image.width()*45/100;x<image.width()*55/100;++x){
                const auto pixel=image.pixelColor(x,y);if(pixel.red()<80 && pixel.green()<80 && pixel.blue()<80)++ink;
            }
            QVERIFY2(ink>100,"Preview has pages but no visible report text in the central paper area");
        }
        pages->setCurrentPage(pages->pageCount());QCOMPARE(pages->currentPage(),pages->pageCount());
        preview.close();
        q.roots={t.path()};q.maxListingEntries=2;const auto limited=read(q);QCOMPARE(limited.state,FolderSizeState::Complete);
        const auto limitedPdf=artifact("report-listing-limited.pdf").isEmpty()?t.path()+"/limited.pdf":artifact("report-listing-limited.pdf");
        QVERIFY(writeFolderReportPdf(limited,limitedPdf,&error));QPdfDocument d;QCOMPARE(d.load(limitedPdf),QPdfDocument::Error::None);
        QVERIFY(d.getAllText(0).text().contains("INCOMPLETA DA LISTAGEM"));
    }
    void dialogOptInExportAndPreviewUseFinishedResult() {
        QTemporaryDir t;write(t.path()+"/demonstração.txt",123);
        QPointer<FolderSizeDialog> dialog=new FolderSizeDialog({{t.path()}});
        const auto cleanupDialog=qScopeGuard([&]{delete dialog.data();});dialog->show();
        auto *csv=dialog->findChild<QPushButton*>("exportFolderCsv");auto *start=dialog->findChild<QPushButton*>("startFolderSize");
        auto *check=dialog->findChild<QCheckBox*>("collectFolderListing");QVERIFY(!check->isChecked());QVERIFY(!csv->isEnabled());QVERIFY(!dialog->findChild<QThread*>());
        check->setChecked(true);start->click();QVERIFY(!csv->isEnabled());QVERIFY(!check->isEnabled());QTRY_VERIFY(start->isEnabled());QVERIFY(csv->isEnabled());
        QVERIFY(dialog->findChild<QLabel*>("folderSizeStatus")->text().contains("Completa no escopo"));
        if(QGuiApplication::platformName()=="windows" && !artifact("dialog-native.png").isEmpty()){
            QVERIFY(QTest::qWaitForWindowExposed(dialog));QVERIFY(dialog->grab().save(artifact("dialog-native.png")));
        }
        auto *job=dynamic_cast<FolderSizeJob*>(dialog->findChild<QThread*>());QVERIFY(job);const auto stable=job->result();
        QVERIFY(QFile::remove(t.path()+"/demonstração.txt"));
        // Exercise real save/preview button connections using only fixture paths.
        const bool previousDialogs=QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        const auto restoreDialogs=qScopeGuard([=]{QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs,previousDialogs);});
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
        const auto target=t.path()+"/ui.csv";QString chosenTarget=target;bool accepted=false, prepared=false;QStringList trace;QTimer automate;
        connect(&automate,&QTimer::timeout,this,[&]{
            for(auto *widget:QApplication::topLevelWidgets()){
                if(auto *file=qobject_cast<QFileDialog*>(widget);file && file->isVisible()){
                    if(!prepared){
                        file->setDirectory(QFileInfo(chosenTarget).absolutePath());
                        if(auto *name=file->findChild<QLineEdit*>("fileNameEdit"))name->setText(QFileInfo(chosenTarget).fileName());
                        else file->selectFile(chosenTarget);
                        prepared=true;
                    }
                    else {trace.append(file->selectedFiles());QMetaObject::invokeMethod(file,"accept");accepted=true;}
                }
                if(auto *message=qobject_cast<QMessageBox*>(widget);message && message->isVisible()){trace.append(message->text());message->accept();}
            }
        });automate.start(50);csv->click();automate.stop();
        QVERIFY(accepted);QVERIFY2(contents(target).contains("demonstração.txt"),qPrintable(trace.join(" | ")+" files="+QDir(t.path()).entryList().join(',')));QCOMPARE(job->result().bytes,stable.bytes);
        chosenTarget=t.path()+"/ui.txt";prepared=false;accepted=false;automate.start(50);
        dialog->findChild<QPushButton*>("exportFolderText")->click();automate.stop();
        QVERIFY(accepted);QVERIFY(contents(chosenTarget).contains("demonstração.txt"));
        {
            FolderReportPreview pdfPreview(stable);pdfPreview.show();chosenTarget=t.path()+"/ui.pdf";prepared=false;accepted=false;
            automate.start(50);pdfPreview.findChild<QPushButton*>("saveFolderPdf")->click();automate.stop();
            QVERIFY(accepted);QPdfDocument saved;QCOMPARE(saved.load(chosenTarget),QPdfDocument::Error::None);
            QString all;for(int i=0;i<saved.pageCount();++i)all+=saved.getAllText(i).text();QVERIFY(all.contains("demonstração.txt"));pdfPreview.close();
        }

        bool previewed=false;QTimer closePreview;
        connect(&closePreview,&QTimer::timeout,this,[&]{if(auto *preview=dialog->findChild<FolderReportPreview*>()){
            if(preview->isVisible()){previewed=true;preview->reject();}
        }});closePreview.start(50);dialog->findChild<QPushButton*>("previewFolderReport")->click();closePreview.stop();QVERIFY(previewed);
        start->click();QVERIFY(!csv->isEnabled());QTRY_VERIFY(start->isEnabled());dialog->close();QTRY_VERIFY(dialog.isNull());
    }
    void representativeMeasurements() {
        const auto output=qEnvironmentVariable("DIROPEN_REPORT_BENCHMARK");if(output.isEmpty())QSKIP("Optional process-isolated benchmark");
        QTemporaryDir t;QVERIFY(QDir(t.path()).mkdir("source"));const auto root=t.path()+"/source";
        const int count=qEnvironmentVariableIntValue("DIROPEN_REPORT_COUNT");QVERIFY(count>0);
        for(int i=0;i<count;++i){const auto dir=root+QString("/pasta%1").arg(i/40);QVERIFY(QDir().mkpath(dir));write(dir+QString("/ação%1.txt").arg(i),1024);}
        QJsonArray records;FolderSizeResult result;
        auto measure=[&](const QString &mode,int run,const std::function<void()> &work){const auto cpu=cpuTime();QElapsedTimer clock;clock.start();work();const auto ms=clock.nsecsElapsed()/1e6;const auto cpuMs=(cpuTime()-cpu)/1e4;
            PROCESS_MEMORY_COUNTERS memory{};memory.cb=sizeof(memory);QVERIFY(GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory)));
            records.append(QJsonObject{{"files",count},{"mode",mode},{"run",run},{"elapsedMs",ms},{"cpuMs",cpuMs},{"peakWorkingSetBytes",double(memory.PeakWorkingSetSize)},{"rows",double(result.listing.size())}});
        };
        for(int mode=0;mode<2;++mode)for(int run=0;run<2;++run)measure(mode?"calculate-listing":"calculate-totals",run,[&]{FolderSizeRequest q{{root}};q.collectListing=mode;result=read(q);QCOMPARE(result.state,FolderSizeState::Complete);QCOMPARE(result.bytes,quint64(count)*1024);});
        for(int run=0;run<2;++run){QString error;
            measure("csv",run,[&]{QVERIFY2(writeFolderReport(result,t.path()+QString("/out%1.csv").arg(run),FolderReportFormat::Csv,&error),qPrintable(error));});
            measure("txt",run,[&]{QVERIFY(writeFolderReport(result,t.path()+QString("/out%1.txt").arg(run),FolderReportFormat::Text,&error));});
            measure("pdf",run,[&]{QVERIFY2(writeFolderReportPdf(result,t.path()+QString("/out%1.pdf").arg(run),&error),qPrintable(error));});
            measure("preview",run,[&]{FolderReportPreview preview(result);preview.show();QVERIFY(QTest::qWaitForWindowExposed(&preview));auto *pages=preview.findChild<QPrintPreviewWidget*>();QTRY_VERIFY(pages->pageCount()>0);QCOMPARE(preview.findChild<QLabel*>("reportPrintStatus")->text().contains("Prévia paginada"),true);preview.close();});
        }
        QFile file(output);QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::NewOnly));QVERIFY(file.write(QJsonDocument(records).toJson())>0);
    }
};
int main(int argc,char **argv) { return isolatedGuiTest<FolderReportTest>(argc,argv); }
#include "folder_report_test.moc"
