#include "isolatedsettings.h"
#include "presentationprobe.h"
#include "foldersize.h"
#include "foldersizedialog.h"
#include "mainwindow.h"
#include "filepanel.h"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileSystemModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QSignalSpy>
#include <QTimer>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>
#include <QWindow>
#include <limits>
#include <windows.h>
#include <winioctl.h>
#include <aclapi.h>
#include <psapi.h>

namespace {
quint64 cpuTime() {
    FILETIME creation{}, exit{}, kernel{}, user{};
    GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user);
    auto value = [](FILETIME f) { return (quint64(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    return value(kernel) + value(user);
}
}
class FolderSizeTest final : public QObject {
    Q_OBJECT
    void write(const QString &path, int size = 7) {
        QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(QByteArray(size, 'x')), qint64(size));
    }
    FolderSizeResult read(FolderSizeRequest request) { std::atomic_bool cancel{false}; return calculateFolderSize(request, cancel); }
private slots:
    void knownTreeUnicodeEmptyHiddenAndHardLinks() {
        QTemporaryDir t; QVERIFY(t.isValid()); QVERIFY(QDir(t.path()).mkpath("Férias/vazia"));
        write(t.path()+"/Férias/ação.txt", 13); write(t.path()+"/oculto", 3);
        const auto file = QDir::toNativeSeparators(t.path()+"/oculto").toStdWString();
        QVERIFY(SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_HIDDEN));
        const auto link = QDir::toNativeSeparators(t.path()+"/hardlink").toStdWString();
        QVERIFY(CreateHardLinkW(link.c_str(), file.c_str(), nullptr));
        auto r=read({{t.path()}}); QCOMPARE(r.state, FolderSizeState::Complete);
        QCOMPARE(r.bytes, quint64(19)); QCOMPARE(r.files, quint64(3)); QCOMPARE(r.folders, quint64(3)); QCOMPARE(r.examined, quint64(6));
        QCOMPARE(r.errors, quint64(0)); QVERIFY(r.started.isValid()); QVERIFY(r.finished >= r.started);
        r=read({{t.path()+"/Férias/vazia"}}); QCOMPARE(r.state, FolderSizeState::Complete); QCOMPARE(r.bytes, quint64(0)); QCOMPARE(r.folders, quint64(1));
    }
    void rootsFormUnionRegardlessOfOrderAndDuplicates() {
        QTemporaryDir t; QVERIFY(QDir(t.path()).mkpath("a/sub")); QVERIFY(QDir(t.path()).mkdir("b"));
        write(t.path()+"/a/sub/file",11); write(t.path()+"/b/file",5);
        auto r=read({{t.path()+"/a/sub",t.path()+"/a",t.path()+"/b",t.path()+"/a",(t.path()+"/a").toUpper()}});
        QCOMPARE(r.state,FolderSizeState::Complete); QCOMPARE(r.bytes,quint64(16)); QCOMPARE(r.files,quint64(2));
        QCOMPARE(r.roots.size(),2); QCOMPARE(r.overlaps,quint64(3));
    }
    void junctionCycleSelectedLinkAndLinkedAncestorAreOmitted() {
        QTemporaryDir t; QVERIFY(QDir(t.path()).mkdir("root")); const auto root=t.path()+"/root";
        write(root+"/file",9); const auto link=root+"/cycle";
        QCOMPARE(QProcess::execute("cmd.exe",{"/d","/c","mklink","/J",QDir::toNativeSeparators(link),QDir::toNativeSeparators(root)}),0);
        const auto r=read({{root}}); const auto selected=read({{link}}); const auto ancestor=read({{link+"/cycle"}});
        QVERIFY(QDir().rmdir(link));
        QCOMPARE(r.state,FolderSizeState::Partial); QCOMPARE(r.bytes,quint64(9)); QCOMPARE(r.skippedLinks,quint64(1));
        QCOMPARE(selected.state,FolderSizeState::Partial); QCOMPARE(selected.bytes,quint64(0)); QCOMPARE(selected.skippedLinks,quint64(1));
        QCOMPARE(ancestor.state,FolderSizeState::Partial); QCOMPARE(ancestor.skippedLinks,quint64(1));
        QVERIFY(QFileInfo(root+"/file").exists());
    }
    void accessDeniedIsPartialAndDetailsAreBounded() {
        QTemporaryDir t; const auto restricted=t.path()+"/restrita"; QVERIFY(QDir().mkdir(restricted)); write(restricted+"/file",9); write(t.path()+"/ok",4);
        auto native=QDir::toNativeSeparators(restricted).toStdWString(); PACL original=nullptr; PSECURITY_DESCRIPTOR descriptor=nullptr;
        QCOMPARE(GetNamedSecurityInfoW(native.data(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&original,nullptr,&descriptor),DWORD(ERROR_SUCCESS));
        ACL deny{}; QVERIFY(InitializeAcl(&deny,sizeof(deny),ACL_REVISION));
        const auto denied=SetNamedSecurityInfoW(native.data(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,&deny,nullptr);
        if(denied!=ERROR_SUCCESS){LocalFree(descriptor);QFAIL("Could not prepare owned ACL fixture");}
        const auto r=read({{t.path()}});
        const auto restored=SetNamedSecurityInfoW(native.data(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|UNPROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,original,nullptr); LocalFree(descriptor);
        QCOMPARE(restored,DWORD(ERROR_SUCCESS)); QCOMPARE(r.state,FolderSizeState::Partial); QCOMPARE(r.bytes,quint64(4)); QVERIFY(r.errors>0);
        FolderSizeRequest missing; for(int i=0;i<30;++i)missing.roots.append(t.path()+QString("/missing%1").arg(i));
        const auto errors=read(missing); QCOMPARE(errors.errors,quint64(30)); QCOMPARE(errors.details.size(),20); QCOMPARE(errors.state,FolderSizeState::Partial);
    }
    void cancellationBeforeAndDuringRetainsObservedSubtotal() {
        QTemporaryDir t; QVERIFY(QDir(t.path()).mkdir("a")); QVERIFY(QDir(t.path()).mkdir("longer")); write(t.path()+"/a/file",17); write(t.path()+"/longer/file",23);
        std::atomic_bool cancel{true}; auto r=calculateFolderSize({{t.path()}},cancel); QCOMPARE(r.state,FolderSizeState::Cancelled); QCOMPARE(r.bytes,quint64(0));
        cancel=false;
        r=calculateFolderSize({{t.path()+"/a",t.path()+"/longer"}},cancel,[&](const FolderSizeResult &p){if(p.state==FolderSizeState::Running && p.files)cancel=true;});
        QCOMPARE(r.state,FolderSizeState::Cancelled); QCOMPARE(r.bytes,quint64(17)); QCOMPARE(r.files,quint64(1));
    }
    void traversalDepthTimeAndArithmeticLimits() {
        QTemporaryDir t; QVERIFY(QDir(t.path()).mkpath("sub/deep")); write(t.path()+"/file",5); write(t.path()+"/sub/deep/file",7);
        FolderSizeRequest request{{t.path()}}; request.maxEntries=1;
        auto r=read(request); QCOMPARE(r.state,FolderSizeState::Limited); QCOMPARE(r.examined,quint64(1)); QVERIFY(!r.limitReason.isEmpty());
        request.maxEntries=100; request.maxDepth=0; r=read(request); QCOMPARE(r.state,FolderSizeState::Limited);
        request.maxDepth=256; request.maxElapsedMs=1; std::atomic_bool cancel{false};
        r=calculateFolderSize(request,cancel,[](const FolderSizeResult &p){if(p.state==FolderSizeState::Running)QTest::qSleep(3);}); QCOMPARE(r.state,FolderSizeState::Limited);
        quint64 bytes=std::numeric_limits<quint64>::max()-2; QVERIFY(addLogicalBytes(bytes,2)); QVERIFY(!addLogicalBytes(bytes,1)); QCOMPARE(bytes,std::numeric_limits<quint64>::max());
        request.maxEntries=0; QCOMPARE(read(request).state,FolderSizeState::Partial);
    }
    void changesAreReportedAndDirectoryCannotBeReplacedDuringWalk() {
        QTemporaryDir t; write(t.path()+"/file",5); std::atomic_bool cancel{false}; bool changed=false, renamed=false;
        const auto r=calculateFolderSize({{t.path()}},cancel,[&](const FolderSizeResult &p){
            if(p.state==FolderSizeState::Running && !changed){
                QTest::qSleep(5); write(t.path()+"/new",8); changed=true;
                const auto from=QDir::toNativeSeparators(t.path()).toStdWString();
                const auto to=QDir::toNativeSeparators(t.path()+"-moved").toStdWString();
                renamed=MoveFileExW(from.c_str(),to.c_str(),0);
                const auto native=QDir::toNativeSeparators(t.path()).toStdWString();
                const auto writer=CreateFileW(native.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
                const bool writeBlocked=writer==INVALID_HANDLE_VALUE;
                if(!writeBlocked)CloseHandle(writer);
                QVERIFY(writeBlocked); // prevents in-place reparse mutation
            }
        });
        QVERIFY(changed); QVERIFY(!renamed); QCOMPARE(r.state,FolderSizeState::Partial); QVERIFY(r.changes>0); QCOMPARE(r.bytes,quint64(13));
    }
    void longPathsAndMissingOrFileSelection() {
        QTemporaryDir t; QString deep=t.path(); for(int i=0;i<9;++i)deep+="/pasta-de-demonstracao-com-nome-longo";
        QVERIFY(QDir().mkpath(deep)); write(deep+"/ação.txt",31);
        const auto r=read({{t.path()}}); QCOMPARE(r.state,FolderSizeState::Complete); QCOMPARE(r.bytes,quint64(31));
        QCOMPARE(read({{deep+"/ação.txt"}}).state,FolderSizeState::Partial);
        QCOMPARE(read({{"relative"}}).state,FolderSizeState::Partial);
    }
    void sparseFileUses64BitLogicalLength() {
        QTemporaryDir t; const auto path=QDir::toNativeSeparators(t.path()+"/sparse-demo").toStdWString();
        const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        QVERIFY(file!=INVALID_HANDLE_VALUE); DWORD returned=0;
        const bool sparse=DeviceIoControl(file,FSCTL_SET_SPARSE,nullptr,0,nullptr,0,&returned,nullptr);
        LARGE_INTEGER length{};length.QuadPart=quint64(64)*1024*1024*1024+123;
        const bool sized=sparse && SetFilePointerEx(file,length,nullptr,FILE_BEGIN) && SetEndOfFile(file);
        CloseHandle(file); QVERIFY(sparse); QVERIFY(sized);
        const auto r=read({{t.path()}}); QCOMPARE(r.state,FolderSizeState::Complete); QCOMPARE(r.bytes,quint64(length.QuadPart)); QCOMPARE(r.files,quint64(1));
    }
    void uiExplicitActionResultReopenAndCloseWhileWorking() {
        QTemporaryDir t; QVERIFY(QDir(t.path()).mkpath("Demonstração/sub")); write(t.path()+"/Demonstração/sub/arquivo.txt",123);
        MainWindow window(t.path(),t.path(),false); window.show();
        auto *action=window.findChild<QAction*>("calculateFolderSize"); QVERIFY(action); action->trigger();
        QVERIFY(!window.findChild<FolderSizeDialog*>()); // no implicit current-folder scan
        auto *view=window.leftPanel()->filesView(); QVERIFY(view);
        QTRY_VERIFY(view->model()->rowCount(view->rootIndex())>0);
        const auto index=view->model()->index(0,0,view->rootIndex());
        QCOMPARE(index.data().toString(),QString("Demonstração"));
        view->selectionModel()->select(index,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
        action->trigger(); QPointer<FolderSizeDialog> dialog=window.findChild<FolderSizeDialog*>(); QVERIFY(dialog);
        QVERIFY(!dialog->findChild<QThread*>()); // constructor does no recursive work
        auto *start=dialog->findChild<QPushButton*>("startFolderSize"); start->click(); QTRY_VERIFY(start->isEnabled());
        const auto text=dialog->findChild<QLabel*>("folderSizeStatus")->text(); QVERIFY(text.contains("Completo")); QVERIFY(text.contains("123 bytes")); QVERIFY(text.contains("Raízes sobrepostas omitidas"));
        start->click(); QTRY_VERIFY(start->isEnabled()); QVERIFY(dialog->findChild<QLabel*>("folderSizeStatus")->text().contains("123 bytes"));
        const auto capture=qEnvironmentVariable("DIROPEN_SIZE_CAPTURE");
        if(!capture.isEmpty()){QVERIFY(QTest::qWaitForWindowExposed(dialog));QVERIFY(dialog->grab().save(capture));}
        dialog->close(); QTRY_VERIFY(dialog.isNull());
        action->trigger(); dialog=window.findChild<FolderSizeDialog*>(); QVERIFY(dialog); dialog->findChild<QPushButton*>("startFolderSize")->click(); dialog->close(); QTRY_VERIFY(dialog.isNull());
        QVERIFY(window.close());
    }
    void uiPartialLimitedAndCancelledLabels() {
        QTemporaryDir t; write(t.path()+"/file",10);
        auto exercise=[&](FolderSizeRequest request,const QString &label,bool cancel){
            QPointer<FolderSizeDialog> dialog=new FolderSizeDialog(request);
            dialog->show(); auto *start=dialog->findChild<QPushButton*>("startFolderSize"); start->click();
            if(cancel)dialog->findChild<QPushButton*>("cancelFolderSize")->click();
            QTRY_VERIFY(start->isEnabled()); const auto text=dialog->findChild<QLabel*>("folderSizeStatus")->text();
            QVERIFY2(text.contains(label),qPrintable(text)); QVERIFY(text.contains("não é o total"));
            QVERIFY(dialog->findChild<QProgressBar*>("folderSizeProgress")->isHidden());
            dialog->close(); QTRY_VERIFY(dialog.isNull());
        };
        exercise({{t.path()+"/missing"}},"Parcial",false);
        FolderSizeRequest limited{{t.path()}};limited.maxEntries=1;exercise(limited,"Limitado",false);
        exercise({{t.path()}},"Cancelado",true);
    }
    void repeatedPresentationPreservesDetailsSelection() {
        QTemporaryDir t; write(t.path()+"/demo",9);
        QPointer<FolderSizeDialog> dialog=new FolderSizeDialog({{t.path()}}); dialog->show();
        auto *start=dialog->findChild<QPushButton*>("startFolderSize");start->click();QTRY_VERIFY(start->isEnabled());
        auto *details=dialog->findChild<QPlainTextEdit*>("folderSizeDetails");
        auto cursor=details->textCursor();cursor.setPosition(0);cursor.setPosition(8,QTextCursor::KeepAnchor);details->setTextCursor(cursor);
        const auto selection=details->textCursor().selectedText(); QSignalSpy changes(details,&QPlainTextEdit::textChanged);
        auto *poll=dialog->findChild<QTimer*>();QVERIFY(poll);
        // Deliver the same last poll/terminal presentation again, without reading the source.
        QVERIFY(QMetaObject::invokeMethod(poll,"timeout",Qt::DirectConnection));
        const bool preserved=details->textCursor().selectedText()==selection;const auto count=changes.size();
        dialog->close();QTRY_VERIFY(dialog.isNull());QVERIFY(preserved);QCOMPARE(count,0);
    }
    void representativeMeasurements() {
        const auto output=qEnvironmentVariable("DIROPEN_SIZE_BENCHMARK"); if(output.isEmpty())QSKIP("Optional isolated performance run");
        QTemporaryDir t; QJsonArray records;
        for(int shape=0;shape<3;++shape){
            const auto selectedShape=qEnvironmentVariable("DIROPEN_SIZE_BENCHMARK_SHAPE");
            if(!selectedShape.isEmpty() && selectedShape.toInt()!=shape)continue;
            const QString root=t.path()+QString("/case%1").arg(shape); QVERIFY(QDir().mkdir(root));
            const int count=shape==0?4000:shape==1?8000:80; QString deep=root;
            for(int i=0;i<count;++i){
                QString dir=root;
                if(shape==1){dir+=QString("/d%1").arg(i/40);QVERIFY(QDir().mkpath(dir));}
                if(shape==2){deep+="/nível";dir=deep;QVERIFY(QDir().mkpath(dir));}
                write(dir+QString("/arquivo%1.txt").arg(i),1024);
            }
            for(int mode=0;mode<2;++mode)for(int run=0;run<2;++run){
#ifdef DIROPEN_PRESENTATION_PROBE
                presentationProbe::samples={};
#endif
                const auto cpu=cpuTime(); QElapsedTimer timer;timer.start();FolderSizeResult r;bool exposed=false;
                QJsonObject taskPhases;
                auto phase=[&](const char *name,const auto &work){QElapsedTimer clock;clock.start();work();taskPhases[name]=clock.nsecsElapsed()/1e6;};
                if(mode==0)r=read({{root}});
                else {
                    QPointer<FolderSizeDialog> dialog;
                    phase("construct",[&]{dialog=new FolderSizeDialog({{root}});});
                    phase("show",[&]{dialog->show();});
                    auto *start=dialog->findChild<QPushButton*>("startFolderSize");
                    phase("start",[&]{start->click();});
                    phase("waitEvents",[&]{while(!start->isEnabled() && timer.elapsed()<60000)QTest::qWait(1);});
                    QVERIFY(start->isEnabled());
                    exposed=dialog->windowHandle() && dialog->windowHandle()->isExposed();
                    auto *job=dynamic_cast<FolderSizeJob*>(dialog->findChild<QThread*>());QVERIFY(job);
                    phase("terminalQuery",[&]{r=job->result();});
                    phase("close",[&]{dialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);});QVERIFY(dialog.isNull());
                }
                const auto wall=timer.nsecsElapsed()/1000000.0; const auto cpuMs=(cpuTime()-cpu)/10000.0;
                PROCESS_MEMORY_COUNTERS memory{};memory.cb=sizeof(memory);QVERIFY(GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory)));
                QCOMPARE(r.state,FolderSizeState::Complete);QCOMPARE(r.bytes,quint64(count)*1024);
#ifdef DIROPEN_PRESENTATION_PROBE
                QJsonObject stages; const char *names[]={"query","format","widgets","layout","paint"};
                for(int i=0;i<presentationProbe::Count;++i)stages[names[i]]=QJsonObject{{"ms",presentationProbe::samples[i].ns/1e6},{"calls",presentationProbe::samples[i].calls}};
                records.append(QJsonObject{{"stages",stages},{"mode",mode==0?"core":"dialog"},{"run",run}});
#endif
                records.append(QJsonObject{{"case",shape==0?"flat-4000":shape==1?"nested-8000":"deep-80"},{"mode",mode==0?"core":"dialog"},{"windowExposed",exposed},{"run",run},{"elapsedMs",wall},{"cpuMs",cpuMs},{"peakWorkingSetBytes",double(memory.PeakWorkingSetSize)},{"taskPhases",taskPhases},{"engineElapsedMs",double(r.elapsedMs)},{"files",double(r.files)},{"folders",double(r.folders)}});
            }
        }
        QFile file(output);QVERIFY(file.open(QIODevice::WriteOnly));QVERIFY(file.write(QJsonDocument(records).toJson())>0);
    }
};
int main(int argc,char **argv) {
#ifdef DIROPEN_PRESENTATION_PROBE
    return isolatedGuiTest<FolderSizeTest,presentationProbe::Application>(argc,argv);
#else
    return isolatedGuiTest<FolderSizeTest>(argc,argv);
#endif
}
#include "folder_size_test.moc"
