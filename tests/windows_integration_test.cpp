#include "windowsintegration.h"
#include "windowsintegrationdialog.h"
#include "mainwindow.h"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

namespace {
struct Value {LSTATUS status=ERROR_FILE_NOT_FOUND;DWORD type=REG_NONE;QByteArray bytes;bool operator==(const Value&)const=default;};
Value value(HKEY root,const wchar_t *path,const wchar_t *name=nullptr){
    HKEY key=nullptr;Value result;result.status=RegOpenKeyExW(root,path,0,KEY_READ|KEY_WOW64_64KEY,&key);if(result.status!=ERROR_SUCCESS)return result;const auto close=qScopeGuard([&]{RegCloseKey(key);});DWORD size=0;result.status=RegQueryValueExW(key,name,nullptr,&result.type,nullptr,&size);if(result.status!=ERROR_SUCCESS)return result;result.bytes.resize(size);result.status=RegQueryValueExW(key,name,nullptr,&result.type,reinterpret_cast<BYTE*>(result.bytes.data()),&size);result.bytes.resize(size);return result;
}
QByteArray utf16(const QString &text){QByteArray result;for(const auto c:text){result+=char(c.unicode()&255);result+=char(c.unicode()>>8);}return result+QByteArray(2,'\0');}
bool put(HKEY root,const wchar_t *path,const wchar_t *name,DWORD type,const QByteArray &bytes){HKEY key=nullptr;const auto code=RegCreateKeyExW(root,path,0,nullptr,0,KEY_ALL_ACCESS|KEY_WOW64_64KEY,nullptr,&key,nullptr);if(code!=ERROR_SUCCESS)return false;const auto close=qScopeGuard([&]{RegCloseKey(key);});return RegSetValueExW(key,name,0,type,reinterpret_cast<const BYTE*>(bytes.constData()),DWORD(bytes.size()))==ERROR_SUCCESS;}
constexpr auto shell=L"Classes\\Directory\\shell";
constexpr auto command=L"Classes\\Directory\\shell\\open\\command";
class DenySetValue {
    HKEY m_key=nullptr;QByteArray m_saved;bool m_changed=false;
public:
    bool valid=false;
    DenySetValue(HKEY root,const wchar_t *path){
        if(RegOpenKeyExW(root,path,0,READ_CONTROL|WRITE_DAC|KEY_WOW64_64KEY,&m_key)!=ERROR_SUCCESS)return;DWORD size=0;RegGetKeySecurity(m_key,DACL_SECURITY_INFORMATION,nullptr,&size);if(!size)return;m_saved.resize(size);if(RegGetKeySecurity(m_key,DACL_SECURITY_INFORMATION,reinterpret_cast<PSECURITY_DESCRIPTOR>(m_saved.data()),&size)!=ERROR_SUCCESS)return;BYTE sid[SECURITY_MAX_SID_SIZE];DWORD sidSize=sizeof(sid);if(!CreateWellKnownSid(WinWorldSid,nullptr,sid,&sidSize))return;QByteArray bytes(sizeof(ACL)+sizeof(ACCESS_DENIED_ACE)+sizeof(ACCESS_ALLOWED_ACE)+2*sidSize,'\0');auto *acl=reinterpret_cast<PACL>(bytes.data());SECURITY_DESCRIPTOR descriptor{};if(!InitializeAcl(acl,DWORD(bytes.size()),ACL_REVISION)||!AddAccessDeniedAce(acl,ACL_REVISION,KEY_SET_VALUE,sid)||!AddAccessAllowedAce(acl,ACL_REVISION,KEY_ALL_ACCESS,sid)||!InitializeSecurityDescriptor(&descriptor,SECURITY_DESCRIPTOR_REVISION)||!SetSecurityDescriptorDacl(&descriptor,TRUE,acl,FALSE))return;m_changed=RegSetKeySecurity(m_key,DACL_SECURITY_INFORMATION,&descriptor)==ERROR_SUCCESS;valid=m_changed;
    }
    bool reset(){if(!m_changed)return true;if(RegSetKeySecurity(m_key,DACL_SECURITY_INFORMATION,reinterpret_cast<PSECURITY_DESCRIPTOR>(m_saved.data()))!=ERROR_SUCCESS)return false;m_changed=false;return true;}
    ~DenySetValue(){reset();if(m_key)RegCloseKey(m_key);}
};
struct RegistryFixture {
    QString path="Software\\DirectoryOpen.Tests\\"+QUuid::createUuid().toString(QUuid::WithoutBraces);HKEY key=nullptr;
    RegistryFixture(){RegCreateKeyExW(HKEY_CURRENT_USER,path.toStdWString().c_str(),0,nullptr,0,KEY_ALL_ACCESS|KEY_WOW64_64KEY,nullptr,&key,nullptr);}
    ~RegistryFixture(){if(!key)return;const auto id=path.section('\\',-1);if(path=="Software\\DirectoryOpen.Tests\\"+id&&!QUuid(id).isNull()){RegDeleteTreeW(key,nullptr);RegCloseKey(key);key=nullptr;RegDeleteKeyExW(HKEY_CURRENT_USER,path.toStdWString().c_str(),KEY_WOW64_64KEY,0);}}
    FolderRegistry scope()const{return isolatedFolderRegistry(reinterpret_cast<quintptr>(key));}
};
}
class WindowsIntegrationTest final:public QObject {
    Q_OBJECT
private slots:
    void absentValuesRoundTripAndActivationIsIdempotent();
    void exactTypesAndAdditionalDataSurviveRestore();
    void originalBackupSurvivesExecutableUpdatesAndRemoval();
    void staleOrForeignStateRefusesWithoutOverwriting();
    void malformedRecordAndMissingOwnershipAreNotAbsence();
    void interruptedChangeKeepsJournalAndCanBeReverted();
    void incompleteUpdateReviewsAndRestoresThePriorActiveCommand();
    void shellCommandPreservesRootAndUnicodeArguments();
    void nativeShellExecutesOnlyTheIsolatedClassCommand();
    void dialogRequiresConfirmationAndCanRestore();
    void mainWindowOpensReadOnlyAndAssociationsUseWindows();
};
void WindowsIntegrationTest::absentValuesRoundTripAndActivationIsIdempotent(){
    RegistryFixture fixture;QVERIFY(fixture.key);const auto before=inspectFolderIntegration(fixture.scope());QVERIFY2(before.error.isEmpty(),qPrintable(before.error));QVERIFY(!before.recorded);const auto change=enableFolderIntegration(fixture.scope(),QCoreApplication::applicationFilePath(),before.revision);QVERIFY2(change.state==IntegrationChangeState::Applied,qPrintable(change.message));auto active=inspectFolderIntegration(fixture.scope());QVERIFY(active.enabled&&active.aligned);const auto raw=value(fixture.key,L"State",L"Current.v1");QCOMPARE(enableFolderIntegration(fixture.scope(),QCoreApplication::applicationFilePath(),active.revision).state,IntegrationChangeState::Unchanged);QVERIFY(value(fixture.key,L"State",L"Current.v1")==raw);QCOMPARE(inspectFolderIntegration(fixture.scope()).revision,active.revision);const auto restored=restoreFolderIntegration(fixture.scope(),active.revision);QVERIFY2(restored.state==IntegrationChangeState::Applied,qPrintable(restored.message));auto inactive=inspectFolderIntegration(fixture.scope());QVERIFY(inactive.error.isEmpty());QVERIFY(inactive.recorded&&!inactive.enabled);QCOMPARE(value(fixture.key,L"Classes\\Directory").status,LSTATUS(ERROR_FILE_NOT_FOUND));QCOMPARE(value(fixture.key,L"State",L"Previous.v1").status,LSTATUS(ERROR_SUCCESS));QCOMPARE(restoreFolderIntegration(fixture.scope(),inactive.revision).state,IntegrationChangeState::Unchanged);
}
void WindowsIntegrationTest::exactTypesAndAdditionalDataSurviveRestore(){
    RegistryFixture fixture;QVERIFY(fixture.key);QVERIFY(put(fixture.key,shell,nullptr,REG_SZ,utf16("oldverb")));QVERIFY(put(fixture.key,command,nullptr,REG_EXPAND_SZ,utf16("%SystemRoot%\\explorer.exe /e,%1")));QVERIFY(put(fixture.key,command,L"DelegateExecute",REG_BINARY,QByteArray::fromHex("0001feff")));QVERIFY(put(fixture.key,command,L"OtherProgram",REG_SZ,utf16("preserve")));const auto oldVerb=value(fixture.key,shell),oldCommand=value(fixture.key,command),oldDelegate=value(fixture.key,command,L"DelegateExecute"),other=value(fixture.key,command,L"OtherProgram");QCOMPARE(enableFolderIntegration(fixture.scope(),QCoreApplication::applicationFilePath(),inspectFolderIntegration(fixture.scope()).revision).state,IntegrationChangeState::Applied);QCOMPARE(restoreFolderIntegration(fixture.scope(),inspectFolderIntegration(fixture.scope()).revision).state,IntegrationChangeState::Applied);QVERIFY(value(fixture.key,shell)==oldVerb);QVERIFY(value(fixture.key,command)==oldCommand);QVERIFY(value(fixture.key,command,L"DelegateExecute")==oldDelegate);QVERIFY(value(fixture.key,command,L"OtherProgram")==other);QCOMPARE(value(fixture.key,command,L"DirectoryOpenOwner").status,LSTATUS(ERROR_FILE_NOT_FOUND));
    RegistryFixture fresh;QCOMPARE(enableFolderIntegration(fresh.scope(),QCoreApplication::applicationFilePath(),inspectFolderIntegration(fresh.scope()).revision).state,IntegrationChangeState::Applied);QVERIFY(put(fresh.key,command,L"OtherProgram",REG_SZ,utf16("keep added data")));const auto restoration=restoreFolderIntegration(fresh.scope(),inspectFolderIntegration(fresh.scope()).revision);QCOMPARE(restoration.state,IntegrationChangeState::Applied);QVERIFY(restoration.message.contains("preservadas"));QCOMPARE(value(fresh.key,command,L"OtherProgram").bytes,utf16("keep added data"));
}
void WindowsIntegrationTest::originalBackupSurvivesExecutableUpdatesAndRemoval(){
    RegistryFixture fixture;QTemporaryDir root;QVERIFY(fixture.key);QVERIFY(put(fixture.key,shell,nullptr,REG_SZ,utf16("before")));const auto original=value(fixture.key,shell);const auto fake=root.filePath("old.exe");QFile f(fake);QVERIFY(f.open(QIODevice::WriteOnly));f.write("fixture, never executed");f.close();QCOMPARE(enableFolderIntegration(fixture.scope(),fake,inspectFolderIntegration(fixture.scope()).revision).state,IntegrationChangeState::Applied);QCOMPARE(enableFolderIntegration(fixture.scope(),QCoreApplication::applicationFilePath(),inspectFolderIntegration(fixture.scope()).revision).state,IntegrationChangeState::Applied);QCOMPARE(restoreFolderIntegration(fixture.scope(),inspectFolderIntegration(fixture.scope()).revision).state,IntegrationChangeState::Applied);QVERIFY(value(fixture.key,shell)==original);QCOMPARE(enableFolderIntegration(fixture.scope(),fake,inspectFolderIntegration(fixture.scope()).revision).state,IntegrationChangeState::Applied);QVERIFY(QFile::remove(fake));QCOMPARE(restoreFolderIntegration(fixture.scope(),inspectFolderIntegration(fixture.scope()).revision).state,IntegrationChangeState::Applied);QVERIFY(value(fixture.key,shell)==original);
}
void WindowsIntegrationTest::staleOrForeignStateRefusesWithoutOverwriting(){
    RegistryFixture fixture;QVERIFY(fixture.key);auto reviewed=inspectFolderIntegration(fixture.scope());QVERIFY(put(fixture.key,shell,nullptr,REG_SZ,utf16("external")));QCOMPARE(enableFolderIntegration(fixture.scope(),QCoreApplication::applicationFilePath(),reviewed.revision).state,IntegrationChangeState::Refused);QCOMPARE(value(fixture.key,L"State",L"Current.v1").status,LSTATUS(ERROR_FILE_NOT_FOUND));QCOMPARE(enableFolderIntegration(fixture.scope(),QCoreApplication::applicationFilePath(),inspectFolderIntegration(fixture.scope()).revision).state,IntegrationChangeState::Applied);const auto backup=value(fixture.key,L"State",L"Current.v1");QVERIFY(put(fixture.key,command,nullptr,REG_SZ,utf16("foreign command")));reviewed=inspectFolderIntegration(fixture.scope());QVERIFY(reviewed.enabled&&!reviewed.aligned);QCOMPARE(restoreFolderIntegration(fixture.scope(),reviewed.revision).state,IntegrationChangeState::Refused);QCOMPARE(enableFolderIntegration(fixture.scope(),QCoreApplication::applicationFilePath(),reviewed.revision).state,IntegrationChangeState::Refused);QVERIFY(value(fixture.key,L"State",L"Current.v1")==backup);QCOMPARE(value(fixture.key,command).bytes,utf16("foreign command"));
}
void WindowsIntegrationTest::malformedRecordAndMissingOwnershipAreNotAbsence(){
    RegistryFixture fixture;QVERIFY(fixture.key);QVERIFY(put(fixture.key,L"State",L"Current.v1",REG_BINARY,"{broken"));const auto before=value(fixture.key,L"State",L"Current.v1");QVERIFY(!inspectFolderIntegration(fixture.scope()).error.isEmpty());QCOMPARE(enableFolderIntegration(fixture.scope(),QCoreApplication::applicationFilePath(),{}).state,IntegrationChangeState::Refused);QVERIFY(value(fixture.key,L"State",L"Current.v1")==before);RegistryFixture foreign;QVERIFY(put(foreign.key,command,L"DirectoryOpenOwner",REG_SZ,utf16(QUuid::createUuid().toString())));QVERIFY(!inspectFolderIntegration(foreign.scope()).error.isEmpty());QCOMPARE(restoreFolderIntegration(foreign.scope(),{}).state,IntegrationChangeState::Refused);QVERIFY(!inspectFolderIntegration({}).error.isEmpty());
}
void WindowsIntegrationTest::shellCommandPreservesRootAndUnicodeArguments(){
    for(const auto &folder:QStringList{"C:\\","C:\\Fotos ação 日本","\\\\server\\share\\"}){auto line=folderOpenCommand("C:/Programs/Directory Open/DirOpen.exe");QVERIFY(!line.isEmpty());line.replace("%1",folder);int count=0;auto **args=CommandLineToArgvW(line.toStdWString().c_str(),&count);QVERIFY(args);const auto release=qScopeGuard([&]{LocalFree(args);});QCOMPARE(count,3);QCOMPARE(QString::fromWCharArray(args[1]),QString("--left"));QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(QString::fromWCharArray(args[2]))),QDir::cleanPath(QDir::fromNativeSeparators(folder)));}QVERIFY(folderOpenCommand("relative.exe").isEmpty());QVERIFY(folderOpenCommand("C:/bad%1/app.exe").isEmpty());
}
void WindowsIntegrationTest::interruptedChangeKeepsJournalAndCanBeReverted(){
    RegistryFixture fixture;QVERIFY(fixture.key);QVERIFY(put(fixture.key,shell,nullptr,REG_SZ,utf16("before")));const auto original=value(fixture.key,shell);
    DenySetValue denied(fixture.key,shell);QVERIFY(denied.valid);
    const auto result=enableFolderIntegration(fixture.scope(),QCoreApplication::applicationFilePath(),inspectFolderIntegration(fixture.scope()).revision);QCOMPARE(result.state,IntegrationChangeState::Unconfirmed);auto incomplete=inspectFolderIntegration(fixture.scope());QVERIFY2(incomplete.error.isEmpty(),qPrintable(incomplete.error));QVERIFY(incomplete.pending&&incomplete.aligned);QVERIFY(value(fixture.key,shell)==original);QCOMPARE(value(fixture.key,command).bytes,utf16(folderOpenCommand(QCoreApplication::applicationFilePath())));QCOMPARE(enableFolderIntegration(fixture.scope(),QCoreApplication::applicationFilePath(),incomplete.revision).state,IntegrationChangeState::Refused);
    QVERIFY(denied.reset());const auto reverted=restoreFolderIntegration(fixture.scope(),incomplete.revision);QVERIFY2(reverted.state==IntegrationChangeState::Applied,qPrintable(reverted.message));QVERIFY(value(fixture.key,shell)==original);QCOMPARE(value(fixture.key,command).status,LSTATUS(ERROR_FILE_NOT_FOUND));QVERIFY(!inspectFolderIntegration(fixture.scope()).recorded);
}
void WindowsIntegrationTest::incompleteUpdateReviewsAndRestoresThePriorActiveCommand(){
    RegistryFixture fixture;QVERIFY(fixture.key);const auto executable=QCoreApplication::applicationFilePath();QCOMPARE(enableFolderIntegration(fixture.scope(),executable,inspectFolderIntegration(fixture.scope()).revision).state,IntegrationChangeState::Applied);const auto original=value(fixture.key,command);QTemporaryDir root;const auto replacement=root.filePath("replacement.exe");QFile fake(replacement);QVERIFY(fake.open(QIODevice::WriteOnly));fake.write("fixture, never executed");fake.close();DenySetValue denied(fixture.key,command);QVERIFY(denied.valid);const auto result=enableFolderIntegration(fixture.scope(),replacement,inspectFolderIntegration(fixture.scope()).revision);QCOMPARE(result.state,IntegrationChangeState::Unconfirmed);const auto pending=inspectFolderIntegration(fixture.scope());QVERIFY(pending.pending&&pending.aligned);QVERIFY(pending.previous.contains(QDir::toNativeSeparators(executable)));QVERIFY(!pending.previous.contains("replacement.exe"));QCOMPARE(restoreFolderIntegration(fixture.scope(),pending.revision).state,IntegrationChangeState::Applied);const auto recovered=inspectFolderIntegration(fixture.scope());QVERIFY(!recovered.pending&&recovered.enabled&&recovered.aligned);QCOMPARE(recovered.executable,executable);QVERIFY(value(fixture.key,command)==original);QVERIFY(denied.reset());QCOMPARE(restoreFolderIntegration(fixture.scope(),recovered.revision).state,IntegrationChangeState::Applied);
}
void WindowsIntegrationTest::nativeShellExecutesOnlyTheIsolatedClassCommand(){
    RegistryFixture fixture;QTemporaryDir root;QVERIFY(fixture.key);const auto folder=root.filePath("ação 日本");QVERIFY(QDir().mkpath(folder));QCOMPARE(enableFolderIntegration(fixture.scope(),QCoreApplication::applicationFilePath(),inspectFolderIntegration(fixture.scope()).revision).state,IntegrationChangeState::Applied);HKEY klass=nullptr;QCOMPARE(RegOpenKeyExW(fixture.key,L"Classes\\Directory",0,KEY_READ|KEY_WOW64_64KEY,&klass),LSTATUS(ERROR_SUCCESS));const auto close=qScopeGuard([&]{RegCloseKey(klass);});const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);const auto uninit=qScopeGuard([&]{if(SUCCEEDED(com))CoUninitialize();});SHELLEXECUTEINFOW call{};call.cbSize=sizeof(call);call.fMask=SEE_MASK_CLASSKEY|SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI;call.hkeyClass=klass;call.lpVerb=L"open";const auto file=QDir::toNativeSeparators(folder).toStdWString();call.lpFile=file.c_str();call.nShow=SW_HIDE;QVERIFY2(ShellExecuteExW(&call),qPrintable(QString("ShellExecuteEx Windows %1").arg(GetLastError())));QVERIFY(call.hProcess);const auto release=qScopeGuard([&]{if(WaitForSingleObject(call.hProcess,0)!=WAIT_OBJECT_0){TerminateProcess(call.hProcess,1);WaitForSingleObject(call.hProcess,5000);}CloseHandle(call.hProcess);});QCOMPARE(WaitForSingleObject(call.hProcess,10000),DWORD(WAIT_OBJECT_0));DWORD code=1;QVERIFY(GetExitCodeProcess(call.hProcess,&code));QCOMPARE(code,DWORD(0));QFile receipt(folder+"/shell-open-arguments.json");QVERIFY(receipt.open(QIODevice::ReadOnly));const auto args=QJsonDocument::fromJson(receipt.readAll()).array();QCOMPARE(args.size(),3);QCOMPARE(args[1].toString(),QString("--left"));QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(args[2].toString())),folder);
}
void WindowsIntegrationTest::dialogRequiresConfirmationAndCanRestore(){
    RegistryFixture fixture;QVERIFY(fixture.key);WindowsIntegrationDialog dialog(fixture.scope(),QCoreApplication::applicationFilePath(),".txt");dialog.show();dialog.findChild<QTabWidget*>()->setCurrentIndex(1);QVERIFY(!inspectFolderIntegration(fixture.scope()).recorded);auto *enable=dialog.findChild<QPushButton*>("enableFolderIntegration"),*restore=dialog.findChild<QPushButton*>("restoreFolderIntegration");QVERIFY(enable->isEnabled());QVERIFY(!restore->isEnabled());QTimer::singleShot(0,&dialog,[&]{auto *question=dialog.findChild<QMessageBox*>("confirmWindowsIntegration");QVERIFY(question);QCOMPARE(question->defaultButton(),question->button(QMessageBox::No));question->button(QMessageBox::No)->click();});enable->click();QVERIFY(!inspectFolderIntegration(fixture.scope()).recorded);QTimer::singleShot(0,&dialog,[&]{auto *q=dialog.findChild<QMessageBox*>("confirmWindowsIntegration");if(q)q->button(QMessageBox::Yes)->click();});enable->click();QVERIFY(inspectFolderIntegration(fixture.scope()).aligned);QVERIFY(restore->isEnabled());const auto out=qEnvironmentVariable("DIROPEN_WINDOWS_CAPTURES");if(!out.isEmpty()){QVERIFY(QDir().mkpath(out));QTest::qWait(100);QVERIFY(dialog.findChild<QLabel*>("folderIntegrationPrevious")->text().contains("herdado"));QVERIFY(dialog.grab().save(out+"/windows-folders.png"));dialog.findChild<QTabWidget*>()->setCurrentIndex(0);QTest::qWait(50);QVERIFY(dialog.grab().save(out+"/windows-associations.png"));dialog.findChild<QTabWidget*>()->setCurrentIndex(1);}QTimer::singleShot(0,&dialog,[&]{auto *q=dialog.findChild<QMessageBox*>("confirmWindowsIntegration");if(q)q->button(QMessageBox::Yes)->click();});restore->click();QVERIFY(!inspectFolderIntegration(fixture.scope()).enabled);dialog.reject();
}
void WindowsIntegrationTest::mainWindowOpensReadOnlyAndAssociationsUseWindows(){
    QVERIFY(validateAssociationExtension(".txt").isEmpty());QVERIFY(!validateAssociationExtension("txt").isEmpty());QVERIFY(!validateAssociationExtension("../txt").isEmpty());QCOMPARE(defaultAppsSettingsUrl(),QUrl("ms-settings:defaultapps"));QCOMPARE(queryFileAssociation("invalid").state,AssociationState::Failed);const auto association=queryFileAssociation(".txt");QVERIFY(association.state!=AssociationState::Failed||!association.error.isEmpty());const auto before=inspectFolderIntegration(currentUserFolderRegistry());QTemporaryDir root;MainWindow window(root.path(),root.path(),false,{});window.show();QTimer::singleShot(0,&window,[&]{auto *dialog=window.findChild<WindowsIntegrationDialog*>("windowsIntegrationDialog");QVERIFY(dialog);dialog->reject();});auto *action=window.findChild<QAction*>("windowsIntegration");QVERIFY(action);action->trigger();const auto after=inspectFolderIntegration(currentUserFolderRegistry());QCOMPARE(after.revision,before.revision);QCOMPARE(after.error,before.error);QVERIFY(window.close());
}
int main(int argc,char **argv){
    if(argc==3&&QByteArray(argv[1])=="--left"){QCoreApplication app(argc,argv);const auto args=app.arguments();const auto folder=QDir::cleanPath(QDir::fromNativeSeparators(args[2]));QFile receipt(folder+"/shell-open-arguments.json");if(!receipt.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 2;QJsonArray array;for(const auto &arg:args)array.append(arg);const auto bytes=QJsonDocument(array).toJson();return receipt.write(bytes)==bytes.size()?0:3;}
    QApplication app(argc,argv);app.setStyle("Fusion");WindowsIntegrationTest test;return QTest::qExec(&test,argc,argv);
}
#include "windows_integration_test.moc"
