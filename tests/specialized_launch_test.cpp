#include "specializedlaunch.h"
#include "specializedlaunchdialog.h"
#include "specializedtools.h"
#include "externalprocess.h"
#include "mainwindow.h"
#include "sessionstore.h"
#include "toolsdialog.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <windows.h>
#include <shellapi.h>
class SpecializedLaunchTest final:public QObject {
    Q_OBJECT
    void put(const QString &path,const QByteArray &bytes="fixture"){QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write(bytes),bytes.size());}
    QByteArray read(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
private slots:
    void plansPreserveArgumentsAndRefuseMissingInstallations(){
        QTemporaryDir root;QVERIFY(!duplicateFinderCommand(root.path()).error.isEmpty());put(root.filePath(".venv/Scripts/pythonw.exe"));put(root.filePath("src/find_dup_files/__main__.py"));const auto command=duplicateFinderCommand(root.path());QVERIFY(command.error.isEmpty());QCOMPARE(command.arguments,QStringList({"-I","-m","find_dup_files"}));QVERIFY(!command.console);QCOMPARE(command.directory,root.path());
        auto setup=remoteSetupCommand(QCoreApplication::applicationFilePath(),root.filePath("conta com espaços.conf"));QVERIFY(setup.error.isEmpty());QVERIFY(setup.console);QCOMPARE(setup.arguments,QStringList({"config","--config",root.filePath("conta com espaços.conf")}));
        setup.arguments.append(QString::fromUtf8("aspas\" e barra\\ final\\"));const auto encoded=delegatedCommandLine(setup);int count=0;auto **parsed=CommandLineToArgvW(reinterpret_cast<LPCWSTR>(encoded.utf16()),&count);QVERIFY(parsed);QCOMPARE(count,setup.arguments.size()+1);for(int i=1;i<count;++i)QCOMPARE(QString::fromWCharArray(parsed[i]),setup.arguments[i-1]);LocalFree(parsed);
        setup.program="relative.exe";QVERIFY(!delegateCommand(setup).delegated);
    }
    void backupPreservesOriginalAndNeverOverwritesEarlierBackup(){
        QTemporaryDir root;const auto config=root.filePath("rclone.conf");QCOMPARE(backupRemoteConfig(config).state,ConfigBackupState::Absent);const QByteArray contents="[fixture]\ntype = local\n";put(config,contents);auto backup=backupRemoteConfig(config);QCOMPARE(backup.state,ConfigBackupState::BackedUp);QCOMPARE(read(config),contents);QCOMPARE(read(backup.path),contents);put(config,"changed");const auto other=backupRemoteConfig(config);QCOMPARE(other.state,ConfigBackupState::BackedUp);QVERIFY(other.path!=backup.path);QCOMPARE(read(backup.path),contents);QCOMPARE(read(other.path),QByteArray("changed"));QCOMPARE(backupRemoteConfig(root.path()).state,ConfigBackupState::Failed);QCOMPARE(backupRemoteConfig(root.filePath("missing/rclone.conf")).state,ConfigBackupState::Failed);
        const auto native=QDir::toNativeSeparators(config);const auto lock=CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);QVERIFY(lock!=INVALID_HANDLE_VALUE);const auto failure=backupRemoteConfig(config);CloseHandle(lock);QCOMPARE(failure.state,ConfigBackupState::Failed);QVERIFY(failure.path.isEmpty());
    }
    void directDispatchHasLiteralArgumentsAndSeparateWorkingDirectory(){
        QTemporaryDir root;const auto output=root.filePath("result.json");DelegatedCommand command{QCoreApplication::applicationFilePath(),root.path(),{"--handoff-helper",output,"literal $() & ;",QString::fromUtf8("viagem ç \"final\\")}};const auto dispatched=delegateCommand(command);QVERIFY2(dispatched.delegated,qPrintable(dispatched.message));QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(output).size()>0,10000);const auto result=QJsonDocument::fromJson(read(output)).object();QCOMPARE(result["cwd"].toString(),root.path());QCOMPARE(result["arguments"].toArray()[0].toString(),command.arguments[2]);QCOMPARE(result["arguments"].toArray()[1].toString(),command.arguments[3]);
        command.console=true;command.arguments[1]=root.filePath("console-result.json");const auto console=delegateCommand(command);QVERIFY2(console.delegated,qPrintable(console.message));QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(command.arguments[1]).size()>0,10000);const auto native=QJsonDocument::fromJson(read(command.arguments[1])).object();QCOMPARE(native["arguments"].toArray(),result["arguments"].toArray());QCOMPARE(native["cwd"],result["cwd"]);
    }
    void dialogsAndMainWindowRoutesDoNotStartAnythingOnOpen(){
        QTemporaryDir root;const auto session=root.filePath("session.json");MainWindow window(root.path(),root.path(),false,session);window.show();
        QTimer::singleShot(0,&window,[&]{auto *d=window.findChild<DuplicateFinderDialog*>("duplicateFinderDialog");QVERIFY(d);auto close=qScopeGuard([d]{d->reject();});d->findChild<QLineEdit*>("duplicateFinderProject")->setText(root.path());d->findChild<QPushButton*>("copyDuplicateLocations")->click();QCOMPARE(qApp->clipboard()->text(),root.path());const auto out=qEnvironmentVariable("DIROPEN_HANDOFF_CAPTURES");if(!out.isEmpty()){QVERIFY(QDir().mkpath(out));QVERIFY(d->grab().save(out+"/duplicate-finder.png"));}});window.findChild<QAction*>("duplicateFinder")->trigger();QCOMPARE(window.session().tools.duplicateFinderProject,root.path());QVERIFY(window.close());MainWindow restored({}, {},false,session);QCOMPARE(restored.session().tools.duplicateFinderProject,root.path());QVERIFY(restored.close());
        ToolsDialog remote(ToolPage::Remote,{},root.path(),{});remote.show();QTimer::singleShot(0,&remote,[&]{auto *d=remote.findChild<RemoteSetupDialog*>("remoteSetupDialog");QVERIFY(d);auto close=qScopeGuard([d]{d->accept();});d->findChild<QLineEdit*>("setupRcloneConfig")->setText(root.filePath("fixture.conf"));d->findChild<QLineEdit*>("setupRcloneProgram")->setText(QCoreApplication::applicationFilePath());const auto out=qEnvironmentVariable("DIROPEN_HANDOFF_CAPTURES");if(!out.isEmpty())QVERIFY(d->grab().save(out+"/remote-setup.png"));});remote.findChild<QPushButton*>("configureRemoteAccounts")->click();QCOMPARE(remote.settings().rcloneConfig,root.filePath("fixture.conf"));QVERIFY(!QFileInfo::exists(root.filePath("fixture.conf")));QMetaObject::invokeMethod(&remote,"reject");
    }
    void installedRcloneConfigAcceptsInteractiveQuitWithFixtureOnly(){
        const auto program=discoverTool("rclone");if(program.isEmpty())QSKIP("rclone real ausente.");QTemporaryDir root;const auto config=root.filePath("fixture.conf");put(config,"[fixture]\ntype = local\n");ExternalCommand command;const auto plan=remoteSetupCommand(program,config);command.executable=plan.program;command.arguments=plan.arguments;command.workingDirectory=root.path();command.input="q\n";std::atomic_bool cancel=false;const auto result=runExternal(command,cancel);QCOMPARE(result.state,ExternalState::Exited);QCOMPARE(result.exitCode,0);QVERIFY(result.output.contains("fixture"));QVERIFY(result.output.contains("Quit"));QCOMPARE(read(config),QByteArray("[fixture]\ntype = local\n"));
    }
};
int main(int argc,char**argv){QApplication app(argc,argv);app.setStyle("Fusion");const auto args=app.arguments();if(args.size()>2&&args[1]=="--handoff-helper"){QJsonArray values;for(const auto &v:args.mid(3))values.append(v);QFile f(args[2]);if(!f.open(QIODevice::WriteOnly))return 2;f.write(QJsonDocument(QJsonObject{{"cwd",QDir::currentPath()},{"arguments",values}}).toJson());return 0;}SpecializedLaunchTest test;return QTest::qExec(&test,argc,argv);}
#include "specialized_launch_test.moc"
