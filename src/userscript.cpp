#include "userscript.h"
#include "fileoperation.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QScopeGuard>
#include <QStringConverter>
#include <QTemporaryDir>
#include <windows.h>
namespace {
bool string(const QString &value,int maximum){return value.size()<=maximum&&!value.contains(QChar(0));}
bool integer(const QJsonValue &value,int minimum,int maximum){return value.isDouble()&&value.toDouble()>=minimum&&value.toDouble()<=maximum&&value.toDouble()==value.toInt();}
bool keys(const QJsonObject &object,const QStringList &allowed){for(auto i=object.begin();i!=object.end();++i)if(!allowed.contains(i.key()))return false;return true;}
QString pathError(const QString &value){if(!QDir::isAbsolutePath(value)||!string(value,32767))return "Use um caminho completo de até 32767 caracteres.";return {};}
QByteArray read(const QString &path,qint64 limit,QString &error){const QFileInfo info(path);if(!info.isFile()||info.isSymLink()||info.isJunction()){error="Escolha um arquivo normal, sem link: "+path;return {};}QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>limit){error="Arquivo indisponível ou acima do limite: "+path;return {};}const auto bytes=file.read(limit+1);if(bytes.size()>limit||file.error()!=QFileDevice::NoError){error="Leitura incompleta ou acima dos limites: "+path;return {};}return bytes;}
struct ScriptSnapshot {
    HANDLE handle=INVALID_HANDLE_VALUE;BY_HANDLE_FILE_INFORMATION identity{};bool owned=false;
    ~ScriptSnapshot(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
    bool create(const QString &path,const QByteArray &source){const auto native=QDir::toNativeSeparators(path);handle=CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,nullptr);if(handle==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(handle,&identity)||!(identity.nFileIndexHigh||identity.nFileIndexLow))return false;owned=true;qsizetype written=0;while(written<source.size()){DWORD count=0;if(!WriteFile(handle,source.constData()+written,DWORD(source.size()-written),&count,nullptr)||!count)return false;written+=count;}return FlushFileBuffers(handle);}
    bool matches(const QByteArray &digest){LARGE_INTEGER start{};if(!SetFilePointerEx(handle,start,nullptr,FILE_BEGIN))return false;QByteArray bytes;char buffer[65536];DWORD received=0;do{if(!ReadFile(handle,buffer,sizeof(buffer),&received,nullptr)||bytes.size()+received>512*1024)return false;bytes.append(buffer,received);}while(received);return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256)==digest;}
};
bool removeOwnedSnapshot(const QString &path,const BY_HANDLE_FILE_INFORMATION &expected){
    const auto native=QDir::toNativeSeparators(path);const auto handle=CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),DELETE|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(handle==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND;const auto release=qScopeGuard([&]{CloseHandle(handle);});BY_HANDLE_FILE_INFORMATION current{};if(!GetFileInformationByHandle(handle,&current)||current.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)||current.dwVolumeSerialNumber!=expected.dwVolumeSerialNumber||current.nFileIndexHigh!=expected.nFileIndexHigh||current.nFileIndexLow!=expected.nFileIndexLow||CompareFileTime(&current.ftCreationTime,&expected.ftCreationTime)!=0)return false;FILE_DISPOSITION_INFO disposition{TRUE};return SetFileInformationByHandle(handle,FileDispositionInfo,&disposition,sizeof(disposition));
}
}
UserScriptLoad readUserScript(const QString &manifest){
    UserScriptLoad result;result.error=pathError(manifest);if(!result.error.isEmpty())return result;const auto bytes=read(manifest,32768,result.error);if(!result.error.isEmpty())return result;QJsonParseError parse;const auto document=QJsonDocument::fromJson(bytes,&parse);if(parse.error!=QJsonParseError::NoError||!document.isObject()){result.error="O formulário precisa ser um objeto JSON válido de até 32 KiB.";return result;}const auto root=document.object();
    if(!keys(root,{"format","version","title","description","script","fields"})||root["format"]!="DirectoryOpen.Script"||root["version"]!=1||!root["title"].isString()||!root["script"].isString()||!root["fields"].isArray()||(root.contains("description")&&!root["description"].isString())){result.error="Formato, versão ou campo de formulário não suportado.";return result;}
    UserScript script;script.manifest=manifest;script.title=root["title"].toString();script.description=root["description"].toString();const auto name=root["script"].toString();if(script.title.trimmed().isEmpty()||!string(script.title,128)||!string(script.description,2048)||!validateItemName(name).isEmpty()||QFileInfo(name).suffix().compare("py",Qt::CaseInsensitive)!=0||root["fields"].toArray().size()>16){result.error="Use título até 128 caracteres, script .py na mesma pasta e até 16 campos.";return result;}
    const QMap<QString,ScriptFieldType> types{{"text",ScriptFieldType::Text},{"integer",ScriptFieldType::Integer},{"boolean",ScriptFieldType::Boolean},{"choice",ScriptFieldType::Choice},{"file",ScriptFieldType::File},{"folder",ScriptFieldType::Folder}};QSet<QString>ids;static const QRegularExpression identifier("^[a-z][a-z0-9_]{0,47}$");
    for(const auto &value:root["fields"].toArray()){
        if(!value.isObject()){result.error="Campo de formulário inválido.";return result;}const auto object=value.toObject();if(!keys(object,{"id","label","type","default","required","minimum","maximum","maxLength","choices"})||!object["id"].isString()||!object["label"].isString()||!object["type"].isString()||!types.contains(object["type"].toString())){result.error="Propriedade ou tipo de campo não suportado.";return result;}
        ScriptField field;field.id=object["id"].toString();field.label=object["label"].toString();field.type=types[object["type"].toString()];if(!identifier.match(field.id).hasMatch()||ids.contains(field.id)||field.label.trimmed().isEmpty()||!string(field.label,128)||(object.contains("required")&&!object["required"].isBool())){result.error="Identificador/rótulo inválido ou repetido.";return result;}ids.insert(field.id);field.required=object["required"].toBool();
        for(const auto key:{"minimum","maximum"})if(object.contains(key)&&!integer(object[key],-1000000000,1000000000)){result.error="Limite numérico inválido.";return result;}field.minimum=object["minimum"].toInt(-1000000000);field.maximum=object["maximum"].toInt(1000000000);if(field.minimum>field.maximum){result.error="Limites numéricos invertidos.";return result;}if(object.contains("maxLength")&&!integer(object["maxLength"],1,32767)){result.error="Limite de texto inválido.";return result;}field.maxLength=object["maxLength"].toInt(field.type==ScriptFieldType::File||field.type==ScriptFieldType::Folder?32767:1024);
        if(field.type==ScriptFieldType::Choice){if(!object["choices"].isArray()||object["choices"].toArray().isEmpty()||object["choices"].toArray().size()>64){result.error="Escolha precisa de 1 a 64 opções.";return result;}for(const auto &choice:object["choices"].toArray()){if(!choice.isString()||choice.toString().isEmpty()||!string(choice.toString(),256)||field.choices.contains(choice.toString())){result.error="Opção de escolha inválida/repetida.";return result;}field.choices.append(choice.toString());}}
        field.initial=object.contains("default")?object["default"]:field.type==ScriptFieldType::Boolean?QJsonValue(false):field.type==ScriptFieldType::Integer?QJsonValue(qBound(field.minimum,0,field.maximum)):field.type==ScriptFieldType::Choice?QJsonValue(field.choices.first()):QJsonValue(QString{});
        const auto &initial=field.initial;if(field.type==ScriptFieldType::Boolean?!initial.isBool():field.type==ScriptFieldType::Integer?!integer(initial,field.minimum,field.maximum):!initial.isString()||!string(initial.toString(),field.maxLength)||(field.type==ScriptFieldType::Choice&&!field.choices.contains(initial.toString()))){result.error="Valor inicial incompatível com o campo.";return result;}script.fields.append(field);
    }
    script.sourcePath=QDir(QFileInfo(manifest).absolutePath()).filePath(name);script.source=read(script.sourcePath,512*1024,result.error);if(!result.error.isEmpty())return result;QStringDecoder decoder(QStringDecoder::Utf8);const QString source=decoder(script.source);if(decoder.hasError()||source.contains(QChar(0))){result.error="O script precisa ser texto UTF-8 sem caracteres nulos.";return result;}
    static const QRegularExpression coding("^[ \\t\\f]*#.*?coding[:=][ \\t]*([-_.a-zA-Z0-9]+)");const auto lines=source.split('\n');for(int i=0;i<qMin(2,int(lines.size()));++i){const auto match=coding.match(lines[i]);if(match.hasMatch()){auto encoding=match.captured(1).toLower();encoding.remove('-');encoding.remove('_');if(encoding!="utf8"&&encoding!="utf8sig"){result.error="A declaração de codificação do script precisa ser UTF-8.";return result;}}}
    script.digest=QCryptographicHash::hash(script.source,QCryptographicHash::Sha256);result.script=std::move(script);return result;
}
ScriptExecution prepareScriptExecution(const UserScript &script,const QString &interpreter,const QString &folder,const QStringList &selection,const QJsonObject &values){
    ScriptExecution result;result.interpreter=interpreter;result.folder=folder;result.sourcePath=script.sourcePath;result.source=script.source;result.digest=script.digest;
    if(!pathError(interpreter).isEmpty()||!QFileInfo(interpreter).isFile()||QFileInfo(interpreter).suffix().compare("exe",Qt::CaseInsensitive)!=0||!pathError(folder).isEmpty()||!QFileInfo(folder).isDir()){result.error="Escolha um interpretador Python .exe e uma pasta de trabalho existentes, com caminhos completos.";return result;}
    if(script.source.size()>512*1024||script.digest.size()!=32||QCryptographicHash::hash(script.source,QCryptographicHash::Sha256)!=script.digest||script.fields.size()>16||selection.size()>256){result.error="Snapshot do código ou seleção fora dos limites. Carregue novamente o formulário.";return result;}
    qsizetype characters=0;for(const auto &path:selection){characters+=path.size();if(!pathError(path).isEmpty()||characters>1024*1024){result.error="A seleção excede 256 caminhos / 2 MiB UTF-16 ou contém caminho inválido.";return result;}}
    QJsonObject normalized;QStringList identifiers;for(const auto &field:script.fields){identifiers.append(field.id);const auto value=values.contains(field.id)?values[field.id]:field.initial;bool valid=true;
        if(field.type==ScriptFieldType::Boolean)valid=value.isBool();else if(field.type==ScriptFieldType::Integer)valid=integer(value,field.minimum,field.maximum);else{const auto text=value.toString();valid=value.isString()&&string(text,field.maxLength)&&(!field.required||!text.trimmed().isEmpty());if(field.type==ScriptFieldType::Choice)valid=valid&&field.choices.contains(text);else if((field.type==ScriptFieldType::File||field.type==ScriptFieldType::Folder)&&!text.isEmpty())valid=valid&&pathError(text).isEmpty()&&(field.type==ScriptFieldType::File?QFileInfo(text).isFile():QFileInfo(text).isDir());}
        if(!valid){result.error="Valor inválido no campo: "+field.label;return result;}normalized[field.id]=value;
    }
    for(auto i=values.begin();i!=values.end();++i)if(!identifiers.contains(i.key())){result.error="Campo não declarado no formulário: "+i.key();return result;}if(QJsonDocument(normalized).toJson(QJsonDocument::Compact).size()>65536){result.error="Os valores do formulário excedem 64 KiB.";return result;}
    result.input=QJsonDocument(QJsonObject{{"contract","DirectoryOpen.ScriptInput"},{"version",1},{"folder",folder},{"selection",QJsonArray::fromStringList(selection)},{"values",normalized}}).toJson(QJsonDocument::Compact)+'\n';if(result.input.size()>2*1024*1024){result.error="A entrada serializada do script excede 2 MiB.";return result;}return result;
}
ScriptResult executeUserScript(const ScriptExecution &execution,const std::atomic_bool &cancel){
    ScriptResult result;
    if(cancel){result.state=ExternalState::Cancelled;result.message="Script cancelado antes de iniciar.";return result;}
    if(!execution.error.isEmpty()){result.message=execution.error;return result;}
    if(execution.source.size()>512*1024||execution.input.size()>2*1024*1024||execution.digest.size()!=32||QCryptographicHash::hash(execution.source,QCryptographicHash::Sha256)!=execution.digest||!QFileInfo(execution.interpreter).isFile()||!pathError(execution.interpreter).isEmpty()||QFileInfo(execution.interpreter).suffix().compare("exe",Qt::CaseInsensitive)!=0||!QFileInfo(execution.folder).isDir()||!pathError(execution.folder).isEmpty()){result.message="A execução aprovada não pôde ser revalidada.";return result;}
    QTemporaryDir work(QDir::tempPath()+"/.diropen-script-XXXXXX");
    if(!work.isValid()){result.message="Não foi possível reservar a cópia temporária do código.";return result;}
    work.setAutoRemove(false);const auto codePath=work.filePath("script.py");std::optional<BY_HANDLE_FILE_INFORMATION> ownedSnapshot;
    result=[&]{
        ScriptResult value;ScriptSnapshot snapshot;const bool created=snapshot.create(codePath,execution.source);if(snapshot.owned)ownedSnapshot=snapshot.identity;
        if(!created||!snapshot.matches(execution.digest)){value.message="A cópia do código não corresponde ao snapshot aprovado ou não pôde ser reservada.";return value;}
        // Keep this creation handle throughout execution: other handles may read,
        // but cannot write, replace or delete the approved code.
        ExternalCommand command;command.executable=execution.interpreter;command.arguments={"-I","-B","-u","-X","utf8",codePath};command.workingDirectory=execution.folder;command.input=execution.input;
        const auto process=runExternal(command,cancel);value.state=process.state;value.exitCode=process.exitCode;
        QStringDecoder output(QStringDecoder::Utf8),errors(QStringDecoder::Utf8);value.output=output(process.output);value.errors=errors(process.errors);
        if(value.output.size()>512*1024){value.output.truncate(512*1024);value.output+="\n[Exibição limitada a 512 mil caracteres.]";}
        if(value.errors.size()>64*1024)value.errors=value.errors.left(64*1024)+"\n[Diagnóstico de exibição limitado.]";
        value.message=process.state==ExternalState::Exited?QString("Script encerrado com código %1. Confira a saída e os arquivos; o código de saída não comprova alterações.").arg(process.exitCode):process.state==ExternalState::Cancelled?"Script cancelado; confira alterações parciais já realizadas.":"Execução não concluída. "+process.message;
        if(output.hasError()||errors.hasError())value.message+=" Saída não UTF-8 foi exibida com substituições.";
        return value;
    }();
    // Remove only the file whose creation identity this owner retained. Scripts
    // may create additional artifacts next to __file__; preserve those items.
    const bool codeRemoved=!ownedSnapshot||removeOwnedSnapshot(codePath,*ownedSnapshot);
    if(!codeRemoved||!QDir().rmdir(work.path())){result.state=ExternalState::Incomplete;result.preservedDirectory=work.path();result.message+="\nPasta temporária contém itens adicionais ou não pôde ser removida; preservada: "+work.path();}
    return result;
}
