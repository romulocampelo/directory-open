#include "windowsintegration.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QStringConverter>
#include <QUuid>
#include <array>
#include <cmath>
#include <optional>
#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>
namespace {
constexpr auto marker=L"DirectoryOpenOwner";
constexpr auto currentRecord=L"Current.v1";
constexpr auto backupRecord=L"Previous.v1";
constexpr DWORD recordLimit=512*1024;
struct RegKey {HKEY value=nullptr;RegKey()=default;RegKey(const RegKey&)=delete;~RegKey(){if(value)RegCloseKey(value);}};
struct RegValue {bool present=false;DWORD type=REG_NONE;QByteArray bytes;bool operator==(const RegValue&)const=default;};
QString failure(const QString &operation,LSTATUS code){return operation+QString(" (Windows %1).").arg(code);}
QStringList paths(const FolderRegistry &scope){return {scope.classes+"\\Directory",scope.classes+"\\Directory\\shell",scope.classes+"\\Directory\\shell\\open",scope.classes+"\\Directory\\shell\\open\\command"};}
bool open(RegKey &key,HKEY root,const QString &path,REGSAM access,QString &error,bool create=false){
    const auto text=path.toStdWString();LSTATUS result;
    if(create)result=RegCreateKeyExW(root,text.c_str(),0,nullptr,REG_OPTION_NON_VOLATILE,access|KEY_WOW64_64KEY,nullptr,&key.value,nullptr);
    else result=RegOpenKeyExW(root,text.c_str(),0,access|KEY_WOW64_64KEY,&key.value);
    if(result==ERROR_FILE_NOT_FOUND||result==ERROR_PATH_NOT_FOUND)return false;
    if(result!=ERROR_SUCCESS){error=failure("Não foi possível abrir o registro "+path,result);return false;}return true;
}
RegValue read(HKEY key,LPCWSTR name,DWORD limit,QString &error){
    RegValue value;if(!key)return value;DWORD size=0,type=0;
    auto code=RegQueryValueExW(key,name,nullptr,&type,nullptr,&size);
    if(code==ERROR_FILE_NOT_FOUND)return value;
    if(code!=ERROR_SUCCESS||size>limit){error=failure("Valor de registro indisponível ou acima do limite",code==ERROR_SUCCESS?ERROR_BUFFER_OVERFLOW:code);return value;}
    value.bytes.resize(size);code=RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<LPBYTE>(value.bytes.data()),&size);
    if(code!=ERROR_SUCCESS||size>limit){error=failure("Valor de registro mudou ou não pôde ser lido",code);return {};}
    value.bytes.resize(size);value.present=true;value.type=type;return value;
}
RegValue textValue(const QString &text){RegValue value;value.present=true;value.type=REG_SZ;for(const auto ch:text){value.bytes.append(char(ch.unicode()&255));value.bytes.append(char(ch.unicode()>>8));}value.bytes.append('\0');value.bytes.append('\0');return value;}
QString describe(const RegValue &value){if(!value.present)return "herdado / sem valor neste usuário";if(value.type==REG_SZ||value.type==REG_EXPAND_SZ){QStringDecoder decoder(QStringDecoder::Utf16LE);QString text=decoder(value.bytes);if(!decoder.hasError()){if(text.endsWith(QChar(0)))text.chop(1);if(!text.contains(QChar(0)))return text.isEmpty()?"(valor vazio)":text.left(1024)+(text.size()>1024?"…":"");}}return QString("valor não textual preservado (%1 bytes, tipo %2)").arg(value.bytes.size()).arg(value.type);}
QJsonObject encode(const RegValue &v){return {{"present",v.present},{"type",double(v.type)},{"data",QString::fromLatin1(v.bytes.toBase64())}};}
std::optional<RegValue> decode(const QJsonValue &value,DWORD limit=65536){
    if(!value.isObject())return {};const auto o=value.toObject();const auto number=o["type"].toDouble(-1);
    if(!o["present"].isBool()||!o["type"].isDouble()||number<0||number>4294967295.0||std::floor(number)!=number||!o["data"].isString())return {};
    const auto encoded=o["data"].toString();if(encoded.size()>(limit+2)/3*4)return {};const auto bytes=QByteArray::fromBase64(encoded.toLatin1());
    if(bytes.size()>limit||QString::fromLatin1(bytes.toBase64())!=encoded)return {};
    RegValue result{o["present"].toBool(),DWORD(number),bytes};if(!result.present&&(result.type!=REG_NONE||!result.bytes.isEmpty()))return {};return result;
}
struct Record {
    QString owner,executable,operation;bool enabled=false,pending=false;
    // Key-presence facts are backup provenance, never authority to delete keys.
    std::array<RegValue,3> previous;std::array<bool,4> keys{},beforeKeys{};
    std::array<RegValue,4> beforeValues;RegValue priorRecord;QJsonObject extra;
};
QByteArray encodeRecord(const Record &record){
    auto o=record.extra;QJsonArray old,keys;for(const auto &v:record.previous)old.append(encode(v));for(bool present:record.keys)keys.append(present);
    o["format"]="DirectoryOpen.FolderOpen";o["version"]=1;o["owner"]=record.owner;o["operation"]=record.operation;o["executable"]=record.executable;o["enabled"]=record.enabled;o["previous"]=old;o["keys"]=keys;
    if(record.pending){QJsonArray values,priorKeys;for(const auto &v:record.beforeValues)values.append(encode(v));for(bool key:record.beforeKeys)priorKeys.append(key);o["pending"]=QJsonObject{{"values",values},{"keys",priorKeys},{"record",encode(record.priorRecord)}};}else o.remove("pending");
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}
bool decodeKeys(const QJsonValue &value,std::array<bool,4> &keys){
    if(!value.isArray()||value.toArray().size()!=4)return false;for(int i=0;i<4;++i){const auto v=value.toArray()[i];if(!v.isBool())return false;keys[i]=v.toBool();if(i&&keys[i]&&!keys[i-1])return false;}return true;
}
std::optional<Record> decodeRecord(const RegValue &raw,bool allowPending=true){
    if(!raw.present||raw.type!=REG_BINARY||raw.bytes.size()>recordLimit)return {};QJsonParseError parse;const auto doc=QJsonDocument::fromJson(raw.bytes,&parse);if(parse.error!=QJsonParseError::NoError||!doc.isObject())return {};const auto o=doc.object();
    if(o["format"]!="DirectoryOpen.FolderOpen"||o["version"]!=1||!o["owner"].isString()||QUuid(o["owner"].toString()).isNull()||!o["operation"].isString()||QUuid(o["operation"].toString()).isNull()||!o["executable"].isString()||folderOpenCommand(o["executable"].toString()).isEmpty()||!o["enabled"].isBool()||!o["previous"].isArray()||o["previous"].toArray().size()!=3)return {};
    Record result;result.owner=o["owner"].toString();result.operation=o["operation"].toString();result.executable=o["executable"].toString();result.enabled=o["enabled"].toBool();
    if(!decodeKeys(o["keys"],result.keys))return {};
    for(int i=0;i<3;++i){const auto previous=decode(o["previous"].toArray()[i]);if(!previous)return {};result.previous[i]=*previous;}
    if(o.contains("pending")){
        if(!allowPending||!o["pending"].isObject())return {};const auto pending=o["pending"].toObject();if(!decodeKeys(pending["keys"],result.beforeKeys)||!pending["values"].isArray()||pending["values"].toArray().size()!=4)return {};
        for(int i=0;i<4;++i){const auto v=decode(pending["values"].toArray()[i]);if(!v)return {};result.beforeValues[i]=*v;}
        const auto prior=decode(pending["record"],recordLimit);if(!prior||(prior->present&&!decodeRecord(*prior,false)))return {};result.priorRecord=*prior;result.pending=true;
    }
    result.extra=o;return result;
}
std::array<RegValue,4> desiredValues(const Record &record){
    if(record.enabled)return {textValue("open"),textValue(folderOpenCommand(record.executable)),textValue({}),textValue(record.owner)};
    return {record.previous[0],record.previous[1],record.previous[2],RegValue{}};
}
struct Source {std::array<bool,4> keys{};std::array<RegValue,4> values;RegValue rawRecord;std::optional<Record> record;QByteArray revision;QString error;bool aligned=false;};
QString noRegistryLinks(const FolderRegistry &scope){
    if(!scope.hive)return "Escopo de registro inválido.";
    for(const auto &path:QStringList{paths(scope).last(),scope.state}){QString prefix;for(const auto &part:path.split('\\')){prefix+=prefix.isEmpty()?part:"\\"+part;HKEY key=nullptr;const auto code=RegOpenKeyExW(reinterpret_cast<HKEY>(scope.hive),prefix.toStdWString().c_str(),REG_OPTION_OPEN_LINK,KEY_QUERY_VALUE|KEY_WOW64_64KEY,&key);if(code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND)break;if(code!=ERROR_SUCCESS)return failure("Registro indisponível",code);const auto release=qScopeGuard([&]{RegCloseKey(key);});DWORD type=0,size=0;const auto queried=RegQueryValueExW(key,L"SymbolicLinkValue",nullptr,&type,nullptr,&size);if(queried!=ERROR_SUCCESS&&queried!=ERROR_FILE_NOT_FOUND)return failure("Não foi possível confirmar o tipo da chave",queried);if(queried==ERROR_SUCCESS&&type==REG_LINK)return "Links de registro não são suportados para esta integração.";}}
    return {};
}
Source inspect(const FolderRegistry &scope){
    Source source;source.error=noRegistryLinks(scope);if(!source.error.isEmpty())return source;const auto names=paths(scope);
    for(int i=0;i<4;++i){RegKey key;source.keys[i]=open(key,reinterpret_cast<HKEY>(scope.hive),names[i],KEY_QUERY_VALUE,source.error);if(i==1)source.values[0]=read(key.value,nullptr,65536,source.error);if(i==3){source.values[1]=read(key.value,nullptr,65536,source.error);source.values[2]=read(key.value,L"DelegateExecute",65536,source.error);source.values[3]=read(key.value,marker,65536,source.error);}if(!source.error.isEmpty())return source;}
    {RegKey key;open(key,reinterpret_cast<HKEY>(scope.hive),scope.state,KEY_QUERY_VALUE,source.error);source.rawRecord=read(key.value,currentRecord,recordLimit,source.error);}if(!source.error.isEmpty())return source;
    if(source.rawRecord.present){source.record=decodeRecord(source.rawRecord);if(!source.record){source.error="Registro de integração inválido ou versão não suportada; dados preservados.";return source;}}
    if(source.record&&source.record->pending){
        const auto desired=desiredValues(*source.record);source.aligned=true;
        for(int i=0;i<4;++i)if(source.values[i]!=source.record->beforeValues[i]&&source.values[i]!=desired[i])source.aligned=false;
    }else{
        if(source.values[3].present&&(!source.record||source.values[3]!=textValue(source.record->owner))){source.error="A marca de propriedade do comando não corresponde ao registro. Nenhuma alteração autorizada.";return source;}
        if(source.record&&source.record->enabled)source.aligned=source.values==desiredValues(*source.record);
        if(source.record&&!source.record->enabled&&source.values[3].present){source.error="Integração desativada com marca inesperada; atualize/revise o registro.";return source;}
    }
    QJsonArray keys,values;for(bool present:source.keys)keys.append(present);for(const auto &v:source.values)values.append(encode(v));source.revision=QCryptographicHash::hash(QJsonDocument(QJsonObject{{"keys",keys},{"values",values},{"record",encode(source.rawRecord)}}).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256);return source;
}
bool put(const FolderRegistry &scope,const QString &path,LPCWSTR name,const RegValue &value,QString &error,const std::optional<RegValue> &expected={}){
    const DWORD limit=path==scope.state?recordLimit:65536;const auto root=reinterpret_cast<HKEY>(scope.hive);RegValue prior;
    {RegKey key;open(key,root,path,KEY_QUERY_VALUE,error);prior=read(key.value,name,limit,error);}if(!error.isEmpty())return false;
    if(expected&&prior!=*expected){error="O valor de registro mudou durante a alteração; dados externos preservados.";return false;}if(prior==value)return true;
    RegKey key;if(!open(key,root,path,KEY_QUERY_VALUE|KEY_SET_VALUE,error,value.present))return false;
    if(read(key.value,name,limit,error)!=prior||!error.isEmpty()){if(error.isEmpty())error="O valor mudou antes da gravação; alteração interrompida.";return false;}
    const auto code=value.present?RegSetValueExW(key.value,name,0,value.type,reinterpret_cast<const BYTE*>(value.bytes.constData()),DWORD(value.bytes.size())):RegDeleteValueW(key.value,name);
    if(code!=ERROR_SUCCESS&&!(code==ERROR_FILE_NOT_FOUND&&!value.present)){error=failure("Não foi possível gravar o registro",code);return false;}
    if(read(key.value,name,limit,error)!=value||!error.isEmpty()){if(error.isEmpty())error="O Windows não confirmou o valor gravado.";return false;}return true;
}
bool flushState(const FolderRegistry &scope,QString &error){RegKey key;if(!open(key,reinterpret_cast<HKEY>(scope.hive),scope.state,KEY_QUERY_VALUE,error))return false;const auto code=RegFlushKey(key.value);if(code!=ERROR_SUCCESS){error=failure("Não foi possível confirmar a persistência do backup",code);return false;}return true;}
// Serializes this application's writers across windows/processes. Registry reads
// still revalidate values because other applications do not share this mutex.
class MutationLock {
    HANDLE m_handle=nullptr;bool m_owned=false;
public:
    QString error;
    explicit MutationLock(const FolderRegistry &scope){
        QByteArray identity;
        if(scope.isolated)identity=QByteArray::number(GetCurrentProcessId())+":"+QByteArray::number(scope.hive);
        else{HANDLE token=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)){error=failure("Identidade de usuário indisponível",GetLastError());return;}const auto close=qScopeGuard([&]{CloseHandle(token);});DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);if(size==0||size>65536){error="Identidade de usuário acima do limite ou indisponível.";return;}QByteArray bytes(size,'\0');if(!GetTokenInformation(token,TokenUser,bytes.data(),size,&size)){error=failure("Identidade de usuário indisponível",GetLastError());return;}const auto sid=reinterpret_cast<const TOKEN_USER*>(bytes.constData())->User.Sid;if(!IsValidSid(sid)){error="Identidade de usuário inválida.";return;}identity=QByteArray(static_cast<const char*>(sid),GetLengthSid(sid));}
        identity+=scope.classes.toUtf8()+scope.state.toUtf8();const auto name=QString("Local\\DirectoryOpen-FolderIntegration-%1").arg(QString::fromLatin1(QCryptographicHash::hash(identity,QCryptographicHash::Sha256).toHex())).toStdWString();m_handle=CreateMutexW(nullptr,FALSE,name.c_str());if(!m_handle){error=failure("Controle de concorrência indisponível",GetLastError());return;}const auto wait=WaitForSingleObject(m_handle,0);m_owned=wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED;if(!m_owned)error="Outra janela está alterando esta integração. Atualize e tente depois.";
    }
    ~MutationLock(){if(m_owned)ReleaseMutex(m_handle);if(m_handle)CloseHandle(m_handle);}
};
bool applyValues(const FolderRegistry &scope,const Record &record,bool reverse,QString &error){
    const auto desired=desiredValues(record);const auto names=paths(scope);const std::array<LPCWSTR,4> valueNames{nullptr,nullptr,L"DelegateExecute",marker};
    const auto target=reverse?record.beforeValues:desired;
    const auto order=reverse||!record.enabled?std::array<int,4>{0,1,2,3}:std::array<int,4>{3,2,1,0};
    for(int i:order){const auto state=inspect(scope);if(!state.error.isEmpty()||!state.record||!state.record->pending||state.record->operation!=record.operation||!state.aligned){error=state.error.isEmpty()?"Configuração mudou durante a alteração. O registro de recuperação foi preservado.":state.error;return false;}if(!put(scope,i==0?names[1]:names[3],valueNames[i],target[i],error,state.values[i]))return false;}
    return true;
}
IntegrationChange change(const FolderRegistry &scope,const QString &executable,const QByteArray &expected,bool enable){
    MutationLock lock(scope);if(!lock.error.isEmpty())return {IntegrationChangeState::Refused,lock.error};
    auto before=inspect(scope);if(!before.error.isEmpty())return {IntegrationChangeState::Refused,before.error};if(expected.size()!=32||before.revision!=expected)return {IntegrationChangeState::Refused,"A configuração mudou desde a revisão. Atualize antes de tentar novamente."};
    if(enable&&(folderOpenCommand(executable).isEmpty()||!QFileInfo(executable).isFile()))return {IntegrationChangeState::Refused,"O executável atual não está disponível ou seu caminho não pode ser representado no comando Shell."};
    const bool recovery=before.record&&before.record->pending;
    if(recovery&&enable)return {IntegrationChangeState::Refused,"Existe alteração incompleta. Reverta-a antes de outra ativação."};
    if(!enable&&!recovery&&(!before.record||!before.record->enabled))return {IntegrationChangeState::Unchanged,"Esta integração não está ativada."};
    if((recovery||(!enable&&before.record->enabled)||(enable&&before.record&&before.record->enabled))&&!before.aligned)return {IntegrationChangeState::Refused,"Outro ajuste alterou a abertura de pastas. O padrão atual e o backup foram preservados; alteração automática recusada."};
    if(enable&&!recovery&&before.aligned&&before.record->executable==executable)return {IntegrationChangeState::Unchanged,"Integração já ativada para esta cópia; nenhum registro alterado."};
    QString error;Record record=before.record.value_or(Record{});RegValue pendingRecord=before.rawRecord;
    if(!recovery){
        record.operation=QUuid::createUuid().toString(QUuid::WithoutBraces);
        if(enable){if(!before.aligned){record.owner=QUuid::createUuid().toString(QUuid::WithoutBraces);for(int i=0;i<3;++i)record.previous[i]=before.values[i];record.keys=before.keys;}record.enabled=true;record.executable=executable;}else record.enabled=false;
        record.pending=true;record.beforeKeys=before.keys;record.beforeValues=before.values;record.priorRecord=before.rawRecord;
        const auto payload=encodeRecord(record);if(payload.size()>recordLimit)return {IntegrationChangeState::Refused,"Backup de configuração acima de 512 KiB; alteração recusada."};pendingRecord={true,REG_BINARY,payload};
        if(before.rawRecord.present&&!put(scope,scope.state,backupRecord,before.rawRecord,error))return {IntegrationChangeState::Refused,error};
        // Persist intent and exact rollback facts before changing Shell values.
        if(!put(scope,scope.state,currentRecord,pendingRecord,error,before.rawRecord)||!flushState(scope,error))return {IntegrationChangeState::Unconfirmed,error+" Atualize o estado; nenhuma abertura de pasta foi alterada nesta etapa."};
    }
    const auto notify=qScopeGuard([&]{if(!scope.isolated)SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST|SHCNF_FLUSHNOWAIT,nullptr,nullptr);});
    if(!applyValues(scope,record,recovery,error))return {IntegrationChangeState::Unconfirmed,error+" Pode haver alterações parciais. Use Atualizar e Reverta a alteração incompleta após revisar."};
    // Keep structural keys: deleting a key after an empty check could erase a concurrent writer's values.
    RegValue finalRecord=record.priorRecord;
    if(!recovery){record.pending=false;finalRecord={true,REG_BINARY,encodeRecord(record)};}
    if(!put(scope,scope.state,currentRecord,finalRecord,error,pendingRecord)||!flushState(scope,error))return {IntegrationChangeState::Unconfirmed,error+" Atualize o estado antes de repetir."};
    return {IntegrationChangeState::Applied,(recovery?QString("Alteração incompleta revertida ao estado anterior à tentativa. Revise se deseja também desativar uma integração já existente."):enable?QString("Abertura de pastas físicas ativada para esta cópia do DirOpen."):QString("Valores anteriores restaurados. Chaves de estrutura e backup foram preservadas."))};
}
struct AssocString {QString text,error;bool missing=false;};
AssocString associationString(const QString &extension,ASSOCSTR field){DWORD size=0;const auto flags=ASSOCF_INIT_IGNOREUNKNOWN;auto code=AssocQueryStringW(flags,field,reinterpret_cast<LPCWSTR>(extension.utf16()),nullptr,nullptr,&size);if(code==HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION)||code==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))return {{},{},true};if((code!=S_FALSE&&code!=E_POINTER&&FAILED(code))||size==0||size>32768)return {{},QString("Consulta indisponível (código %1).").arg(quint32(code)),false};std::wstring buffer(size,L'\0');code=AssocQueryStringW(flags,field,reinterpret_cast<LPCWSTR>(extension.utf16()),nullptr,buffer.data(),&size);if(code!=S_OK||size==0||size>buffer.size())return {{},QString("Consulta incompleta (código %1).").arg(quint32(code)),false};return {QString::fromWCharArray(buffer.c_str(),int(size-1)),{},false};}
}
QString validateAssociationExtension(const QString &value){if(value.size()<2||value.size()>255||!value.startsWith('.')||value.endsWith('.')||value.endsWith(' '))return "Informe uma extensão como .txt (até 255 caracteres).";for(const auto ch:value)if(ch.unicode()<32||QString("<>:\"/\\|?*").contains(ch))return "Extensão contém caractere não suportado.";return {};}
FileAssociation queryFileAssociation(const QString &extension){FileAssociation result;result.error=validateAssociationExtension(extension);if(!result.error.isEmpty())return result;const auto program=associationString(extension,ASSOCSTR_PROGID),name=associationString(extension,ASSOCSTR_FRIENDLYAPPNAME),executable=associationString(extension,ASSOCSTR_EXECUTABLE);result.programId=program.text;result.application=name.text;result.executable=executable.text;QStringList errors;for(const auto &v:{program,name,executable})if(!v.error.isEmpty())errors.append(v.error);result.error=errors.join(' ');if(!result.programId.isEmpty()||!result.application.isEmpty()||!result.executable.isEmpty())result.state=AssociationState::Assigned;else if(program.missing&&name.missing&&executable.missing)result.state=AssociationState::Missing;return result;}
QUrl defaultAppsSettingsUrl(){return QUrl("ms-settings:defaultapps");}
FolderRegistry currentUserFolderRegistry(){return {reinterpret_cast<quintptr>(HKEY_CURRENT_USER),"Software\\Classes","Software\\DirectoryOpen\\FolderIntegration",false};}
FolderRegistry isolatedFolderRegistry(quintptr key){return {key,"Classes","State",true};}
QString folderOpenCommand(const QString &executable){if(!QDir::isAbsolutePath(executable)||executable.contains('%')||executable.contains('"')||executable.contains(QChar(0))||QFileInfo(executable).suffix().compare("exe",Qt::CaseInsensitive)!=0)return {};const auto command='"'+QDir::toNativeSeparators(executable)+"\" --left \"%1\\.\"";return command.size()<32767?command:QString{};}
FolderIntegrationSnapshot inspectFolderIntegration(const FolderRegistry &scope){
    FolderIntegrationSnapshot result;const auto source=inspect(scope);result.error=source.error;if(!result.error.isEmpty())return result;
    result.revision=source.revision;result.current="Verbo: "+describe(source.values[0])+"\nComando de abertura: "+describe(source.values[1])+"\nDelegação: "+describe(source.values[2]);result.aligned=source.aligned;
    if(source.record){result.recorded=true;result.enabled=source.record->enabled;result.pending=source.record->pending;result.executable=source.record->executable;const auto &record=*source.record;result.previous="Verbo anterior: "+describe(record.pending?record.beforeValues[0]:record.previous[0])+"\nComando anterior: "+describe(record.pending?record.beforeValues[1]:record.previous[1])+"\nDelegação anterior: "+describe(record.pending?record.beforeValues[2]:record.previous[2]);}return result;
}
IntegrationChange enableFolderIntegration(const FolderRegistry &scope,const QString &executable,const QByteArray &revision){return change(scope,executable,revision,true);}
IntegrationChange restoreFolderIntegration(const FolderRegistry &scope,const QByteArray &revision){return change(scope,{},revision,false);}
