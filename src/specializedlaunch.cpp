#include "specializedlaunch.h"
#include "archive.h"
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QScopeGuard>
#include <QUuid>
#include <windows.h>
namespace {
bool path(const QString &value){return QDir::isAbsolutePath(value)&&value.size()<32768&&!value.contains(QChar(0));}
QString quoted(QString value){
    QString out="\"";int slashes=0;
    for(auto ch:value){if(ch=='\\'){++slashes;continue;}if(ch=='\"')out+=QString(slashes*2+1,'\\');else out+=QString(slashes,'\\');slashes=0;out+=ch;}
    return out+QString(slashes*2,'\\')+'\"';
}
}
DelegatedCommand duplicateFinderCommand(const QString &project){
    DelegatedCommand command;const QDir root(project);command.directory=root.absolutePath();command.program=root.filePath(".venv/Scripts/pythonw.exe");command.arguments={"-I","-m","find_dup_files"};
    if(!path(project)||!QFileInfo(command.program).isFile()||!QFileInfo(root.filePath("src/find_dup_files/__main__.py")).isFile())command.error="Escolha a pasta do duplicador Python já configurado, contendo src/find_dup_files e .venv/Scripts/pythonw.exe. Nenhum componente será instalado.";
    return command;
}
DelegatedCommand remoteSetupCommand(const QString &program,const QString &config){
    DelegatedCommand command;command.program=program;command.directory=QFileInfo(config).absolutePath();command.arguments={"config","--config",config};command.console=true;
    if(!path(program)||!QFileInfo(program).isFile()||!path(config)||!QFileInfo(command.directory).isDir()||QFileInfo(config).fileName().isEmpty()||QFileInfo(config).isDir())command.error="Escolha o executável rclone e um arquivo de configuração em pasta existente.";
    return command;
}
QString delegatedCommandLine(const DelegatedCommand &command){QStringList words{quoted(QDir::toNativeSeparators(command.program))};for(const auto &a:command.arguments)words.append(quoted(a));return words.join(' ');}
DelegatedCommand archiveManagerCommand(const QString &location){
    DelegatedCommand command;const auto cli=sevenZipExecutable();command.program=cli.isEmpty()?QString{}:QDir(QFileInfo(cli).absolutePath()).filePath("7zFM.exe");command.directory=QFileInfo(location).isDir()?location:QFileInfo(location).absolutePath();command.arguments={location};
    if(!path(location)||!QFileInfo(location).exists()||!QFileInfo(command.program).isFile())command.error="Gerenciador 7zFM.exe não encontrado junto ao 7z.exe, ou local indisponível.";
    return command;
}
DelegatedResult delegateCommand(const DelegatedCommand &command){
    if(!command.error.isEmpty())return {false,command.error};
    if(!path(command.program)||!QFileInfo(command.program).isFile()||!path(command.directory)||!QFileInfo(command.directory).isDir()||command.arguments.size()>64)return {false,"Programa ou pasta de execução indisponível."};
    for(const auto &a:command.arguments)if(a.contains(QChar(0))||a.size()>32768)return {false,"Argumento fora dos limites."};
    auto line=delegatedCommandLine(command);if(line.size()>=30000)return {false,"A chamada excede o limite de argumentos."};
    if(command.console){
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};auto app=QDir::toNativeSeparators(command.program),cwd=QDir::toNativeSeparators(command.directory);
        if(!CreateProcessW(reinterpret_cast<LPCWSTR>(app.utf16()),reinterpret_cast<LPWSTR>(line.data()),nullptr,nullptr,FALSE,CREATE_NEW_CONSOLE,nullptr,reinterpret_cast<LPCWSTR>(cwd.utf16()),&startup,&process))return {false,QString("O Windows não iniciou a ferramenta (código %1).").arg(GetLastError())};
        CloseHandle(process.hThread);CloseHandle(process.hProcess);
    }else{
        QProcess process;process.setProgram(command.program);process.setArguments(command.arguments);process.setWorkingDirectory(command.directory);
        if(!process.startDetached())return {false,process.errorString()};
    }
    return {true,"Ferramenta aberta. A execução, o cancelamento e o resultado são controlados na janela externa."};
}
ConfigBackup backupRemoteConfig(const QString &config){
    ConfigBackup result;if(!path(config)){result.error="Caminho de configuração inválido.";return result;}
    const auto native=QDir::toNativeSeparators(config);const auto attributes=GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()));
    if(attributes==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();if(error==ERROR_FILE_NOT_FOUND){result.state=ConfigBackupState::Absent;return result;}result.error=QString("Não foi possível confirmar a configuração (código %1).").arg(error);return result;}
    if(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)){result.error="A configuração precisa ser um arquivo normal, sem link.";return result;}
    const auto handle=CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle==INVALID_HANDLE_VALUE){result.error="Não foi possível reservar a configuração para backup. Feche outros editores/rclone e tente novamente.";return result;}
    const auto release=qScopeGuard([&]{CloseHandle(handle);});BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(handle,&info)||info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)||info.nFileSizeHigh||info.nFileSizeLow>16*1024*1024){result.error="Configuração indisponível, não regular ou acima de 16 MiB.";return result;}
    result.path=config+".diropen-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".bak";const auto target=QDir::toNativeSeparators(result.path);
    if(!CopyFileW(reinterpret_cast<LPCWSTR>(native.utf16()),reinterpret_cast<LPCWSTR>(target.utf16()),TRUE)){result.error=QString("Backup não confirmado (código %1). Se houver cópia parcial, foi preservada em: %2").arg(GetLastError()).arg(result.path);return result;}
    result.state=ConfigBackupState::BackedUp;return result;
}
