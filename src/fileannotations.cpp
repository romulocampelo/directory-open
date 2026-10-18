#include "fileannotations.h"
#include "batchrename.h"
#include "windowspath.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <memory>
#include <windows.h>
namespace{
constexpr qsizetype budget=8*1024*1024;
bool text(const QString &s,int max){return s.size()<=max&&!s.contains(QChar(0));}
bool validKey(const QString &s){static QRegularExpression pattern("^[0-9a-f]{40}$");return pattern.match(s).hasMatch();}
QByteArray serialize(const AnnotationCatalog &catalog){auto o=catalog.extra;QJsonObject entries;for(auto i=catalog.entries.begin();i!=catalog.entries.end();++i)entries[i.key()]=encodeAnnotation(i.value());o["format"]="DirectoryOpen.Annotations";o["version"]=1;o["entries"]=entries;return QJsonDocument(o).toJson(QJsonDocument::Compact);}
QString write(const QString &path,const QByteArray &bytes){QSaveFile file(path);file.setDirectWriteFallback(false);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())return file.errorString();if(!file.commit())return file.errorString();return {};}
}
QString annotationIdentity(const RenameIdentity &id){
    if((!id.indexHigh&&!id.indexLow)||!id.created)return {};
    return QString("%1%2%3%4").arg(id.volume,8,16,QChar('0')).arg(id.indexHigh,8,16,QChar('0')).arg(id.indexLow,8,16,QChar('0')).arg(id.created,16,16,QChar('0'));
}
QString validateAnnotation(const FileAnnotation &v){
    if(!QDir::isAbsolutePath(v.path)||!text(v.path,32768)||!text(v.label,64)||!text(v.status,64)||!text(v.description,4096)||v.rating<0||v.rating>5||v.tags.size()>32)return "Classificação inválida ou acima dos limites.";
    static QRegularExpression color("^#[0-9a-fA-F]{6}$");if(!v.color.isEmpty()&&!color.match(v.color).hasMatch())return "Cor da etiqueta inválida.";
    QSet<QString>tags;for(const auto &tag:v.tags){if(tag.trimmed().isEmpty()||tag!=tag.trimmed()||!text(tag,64)||tags.contains(tag.toCaseFolded()))return "Tags inválidas ou repetidas.";tags.insert(tag.toCaseFolded());}return {};
}
QJsonObject encodeAnnotation(const FileAnnotation &v){auto o=v.extra;QJsonArray tags;for(const auto &tag:v.tags)tags.append(tag);o["path"]=v.path;o["label"]=v.label;o["status"]=v.status;o["description"]=v.description;o["color"]=v.color;o["rating"]=v.rating;o["tags"]=tags;return o;}
std::optional<FileAnnotation> decodeAnnotation(const QJsonValue &value){
    if(!value.isObject())return {};const auto o=value.toObject();FileAnnotation v;v.extra=o;
    for(const auto &key:{"path","label","status","description","color","rating","tags"})v.extra.remove(key);
    for(const auto &key:{"path","label","status","description","color"})if(!o[key].isString())return {};
    v.path=o["path"].toString();v.label=o["label"].toString();v.status=o["status"].toString();v.description=o["description"].toString();v.color=o["color"].toString();
    if(!o["rating"].isDouble()||o["rating"].toDouble()!=o["rating"].toInt(-1)||!o["tags"].isArray())return {};v.rating=o["rating"].toInt();for(const auto &tag:o["tags"].toArray()){if(!tag.isString())return {};v.tags.append(tag.toString());}if(!validateAnnotation(v).isEmpty())return {};return v;
}
AnnotationLoad loadAnnotations(const QString &path){
    AnnotationLoad result;if(path.isEmpty())return result;
    auto fail=[&](QString error,AnnotationState state=AnnotationState::Invalid){result.state=state;result.error=std::move(error);return result;};
    const auto attributes=GetFileAttributesW(WindowsPath::forFileApi(path).toStdWString().c_str());if(attributes==INVALID_FILE_ATTRIBUTES){const auto code=GetLastError();if(code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND)return result;return fail(QString("Não foi possível consultar o catálogo (Windows %1).").arg(code));}
    if(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))return fail("O catálogo não é um arquivo normal.");QFile file(path);if(!file.open(QIODevice::ReadOnly))return fail(file.errorString());if(file.size()>budget)return fail("O catálogo excede 8 MiB.");const auto bytes=file.read(budget+1);if(bytes.size()>budget||file.error()!=QFileDevice::NoError)return fail("Leitura incompleta do catálogo.");result.digest=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
    QJsonParseError parse;const auto doc=QJsonDocument::fromJson(bytes,&parse);const auto o=doc.object();if(parse.error!=QJsonParseError::NoError||!doc.isObject()||o["format"]!="DirectoryOpen.Annotations")return fail("Catálogo de classificações inválido; o arquivo foi preservado.");if(o["version"]!=1)return fail("Versão de catálogo não suportada; o arquivo foi preservado.",AnnotationState::Unsupported);
    if(!o["entries"].isObject()||o["entries"].toObject().size()>10000)return fail("O catálogo excede 10.000 registros ou é inválido.");const auto entries=o["entries"].toObject();for(auto i=entries.begin();i!=entries.end();++i){const auto value=decodeAnnotation(i.value());if(!validKey(i.key())||!value)return fail("Há uma classificação inválida; nada foi substituído.");result.catalog.entries[i.key()]=*value;}
    result.catalog.extra=o;for(const auto &key:{"format","version","entries"})result.catalog.extra.remove(key);result.state=AnnotationState::Loaded;return result;
}
QString saveAnnotations(const QString &path,const QVector<AnnotationRecord> &changes,const std::atomic_bool &cancel,AnnotationCatalog *memory){
    if(changes.isEmpty()||changes.size()>256)return "Escolha de 1 a 256 itens para guardar.";
    std::unique_ptr<QLockFile> lock;if(!path.isEmpty()){for(const auto &name:{path,path+".bak"}){const QFileInfo info(name);if(info.isSymLink()||(info.exists()&&!info.isFile()))return "Catálogo ou backup não é um arquivo normal.";}lock=std::make_unique<QLockFile>(path+".lock");if(!lock->tryLock(0))return "Outro processo está guardando classificações. Tente novamente.";}
    const auto loaded=loadAnnotations(path);if(!loaded.error.isEmpty())return loaded.error;auto catalog=path.isEmpty()&&memory?*memory:loaded.catalog;
    QSet<QString>keys;for(const auto &change:changes){if(keys.contains(change.key))return "A seleção repete a mesma identidade (possível hard link). Classifique uma ocorrência.";keys.insert(change.key);if(cancel)return "Gravação cancelada antes de publicar.";const auto error=validateAnnotation(change.value);if(!error.isEmpty())return error;const auto probe=inspectRenameItem(change.path);if(probe.state!=RenameProbeState::Present||annotationIdentity(probe.identity)!=change.key)return "Um item mudou ou não pôde ser confirmado. Reabra a classificação.";
        auto existing=catalog.entries.value(change.key);if(existing.path.isEmpty())existing.path=change.path;
        // The cached location is display metadata. Renaming a file retains its
        // identity; a different path by itself does not manufacture a conflict.
        auto before=change.before;before.path=existing.path;if(encodeAnnotation(existing)!=encodeAnnotation(before))return "Uma classificação foi alterada em outra janela. Reabra antes de guardar.";
        auto note=change.value;note.path=change.path;catalog.entries[change.key]=note;
    }
    if(catalog.entries.size()>10000)return "O catálogo atingiu 10.000 registros.";const auto bytes=serialize(catalog);if(bytes.size()>budget)return "O catálogo atingiu o limite de 8 MiB.";if(cancel)return "Gravação cancelada antes de publicar.";
    if(path.isEmpty()){if(!memory)return "Catálogo temporário indisponível.";*memory=std::move(catalog);return {};}
    const auto fresh=loadAnnotations(path);if(fresh.state!=loaded.state||fresh.digest!=loaded.digest||!fresh.error.isEmpty())return "O catálogo mudou durante a gravação; reabra a classificação.";
    QFile previous(path);if(loaded.state==AnnotationState::Loaded){if(!previous.open(QIODevice::ReadOnly)||previous.size()>budget)return "Não foi possível preservar o catálogo anterior.";const auto old=previous.read(budget+1);if(previous.error()!=QFileDevice::NoError||old.size()>budget||QCryptographicHash::hash(old,QCryptographicHash::Sha256)!=loaded.digest)return "O catálogo mudou antes do backup.";previous.close();if(old==bytes)return {};const auto error=write(path+".bak",old);if(!error.isEmpty())return error;}
    return write(path,bytes);
}
AnnotationJob::AnnotationJob(QString path,QStringList files,AnnotationCatalog memory,QObject*parent):QThread(parent),m_path(std::move(path)),m_files(std::move(files)),m_memory(std::move(memory)){}
AnnotationJob::AnnotationJob(QString path,QVector<AnnotationRecord>changes,AnnotationCatalog memory,QObject*parent):QThread(parent),m_path(std::move(path)),m_changes(std::move(changes)),m_memory(std::move(memory)),m_saving(true){}
AnnotationJob::~AnnotationJob(){cancel();wait();}
AnnotationResult AnnotationJob::takeResult(){return std::move(m_result);}
void AnnotationJob::run(){
    if(m_saving){m_result.error=saveAnnotations(m_path,m_changes,m_cancel,&m_memory);m_result.catalog=std::move(m_memory);m_result.saved=m_result.error.isEmpty();m_result.cancelled=!m_result.saved&&m_cancel;return;}
    if(m_files.isEmpty()||m_files.size()>256){m_result.error="Escolha de 1 a 256 itens.";return;}const auto loaded=loadAnnotations(m_path);if(!loaded.error.isEmpty()){m_result.error=loaded.error;return;}m_result.catalog=m_path.isEmpty()?std::move(m_memory):loaded.catalog;
    for(const auto &path:m_files){if(m_cancel){m_result.cancelled=true;return;}AnnotationRecord record;record.path=path;const auto probe=inspectRenameItem(path);if(probe.state!=RenameProbeState::Present)record.error=probe.error;else{record.key=annotationIdentity(probe.identity);if(record.key.isEmpty())record.error="Identidade estável/data de criação indisponível; classificação recusada.";}record.before=m_result.catalog.entries.value(record.key);if(record.before.path.isEmpty())record.before.path=path;record.value=record.before;record.value.path=path;m_result.records.append(record);}
}
