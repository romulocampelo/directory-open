#include "renamerecipe.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
namespace {
const QStringList names{"prefix","suffix","replace","slice","lower","upper","trim"};
bool text(const QString &value,int limit){return value.size()<=limit&&!value.contains(QChar(0))&&!value.contains('\r')&&!value.contains('\n');}
QString write(const QString &path,const QByteArray &bytes){QSaveFile file(path);file.setDirectWriteFallback(false);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())return file.errorString();return {};}
}
QString validateRenameRules(const QVector<RenameRule>&rules){
    if(rules.size()>16)return "Use até 16 etapas na receita.";
    for(const auto &rule:rules){const auto kind=int(rule.kind);if(kind<0||kind>=names.size()||!text(rule.text,255)||!text(rule.replacement,255)||rule.start<1||rule.start>255||rule.count<0||rule.count>255)return "Etapa fora dos tipos ou limites da receita.";
        if((rule.kind==RenameRuleKind::Prefix||rule.kind==RenameRuleKind::Suffix||rule.kind==RenameRuleKind::Replace)&&rule.text.isEmpty())return "Informe o texto do prefixo, sufixo ou substituição.";}
    return {};
}
RenameText applyRenameRules(QString value,const QVector<RenameRule>&rules){
    auto error=validateRenameRules(rules);if(!error.isEmpty())return {{},error};if(value.size()>255)return {{},"Nome de entrada acima de 255 caracteres."};
    for(const auto &rule:rules){
        switch(rule.kind){
        case RenameRuleKind::Prefix:value.prepend(rule.text);break;
        case RenameRuleKind::Suffix:value.append(rule.text);break;
        case RenameRuleKind::Replace:value.replace(rule.text,rule.replacement,rule.caseSensitive?Qt::CaseSensitive:Qt::CaseInsensitive);break;
        case RenameRuleKind::Slice:{const auto points=value.toStdU32String();const auto first=qMin(rule.start-1,int(points.size()));const auto length=rule.count==0?points.size()-first:qMin<qsizetype>(rule.count,points.size()-first);value=length?QString::fromUcs4(points.data()+first,length):QString{};break;}
        case RenameRuleKind::Lower:value=value.toLower();break;
        case RenameRuleKind::Upper:value=value.toUpper();break;
        case RenameRuleKind::Trim:value=value.trimmed();break;
        }
        if(value.size()>255)return {{},"Uma etapa produziu nome acima de 255 caracteres. Ajuste a receita."};
    }
    return {value,{}};
}
QJsonObject encodeRenameRecipe(const RenameRecipe&recipe){QJsonArray rules;for(const auto &r:recipe.rules)rules.append(QJsonObject{{"kind",names.value(int(r.kind))},{"text",r.text},{"replacement",r.replacement},{"start",r.start},{"count",r.count},{"caseSensitive",r.caseSensitive}});return {{"format","DirectoryOpen.RenameRecipe"},{"version",1},{"name",recipe.name},{"rules",rules}};}
std::optional<RenameRecipe> decodeRenameRecipe(const QJsonValue&value){
    if(!value.isObject())return {};const auto o=value.toObject();if(o["format"]!="DirectoryOpen.RenameRecipe"||o["version"]!=1||!o["name"].isString()||!o["rules"].isArray())return {};RenameRecipe recipe;recipe.name=o["name"].toString();if(recipe.name.trimmed().isEmpty()||!text(recipe.name,128)||o["rules"].toArray().size()>16)return {};
    for(const auto &entry:o["rules"].toArray()){if(!entry.isObject())return {};const auto r=entry.toObject();if(!r["kind"].isString()||!names.contains(r["kind"].toString())||!r["text"].isString()||!r["replacement"].isString()||!r["caseSensitive"].isBool())return {};for(const auto key:{"start","count"})if(!r[key].isDouble()||r[key].toDouble()!=r[key].toInt(-1))return {};recipe.rules.append({RenameRuleKind(names.indexOf(r["kind"].toString())),r["text"].toString(),r["replacement"].toString(),r["start"].toInt(),r["count"].toInt(),r["caseSensitive"].toBool()});}
    if(!validateRenameRules(recipe.rules).isEmpty())return {};return recipe;
}
RenameRecipeFile readRenameRecipe(const QString&path){QFile f(path);if(!f.open(QIODevice::ReadOnly)||f.size()>16384)return {{},"Não foi possível ler uma receita de até 16 KiB."};const auto bytes=f.read(16385);if(f.error()!=QFileDevice::NoError||bytes.size()>16384)return {{},"Leitura incompleta ou acima dos limites."};const auto value=decodeRenameRecipe(QJsonDocument::fromJson(bytes).object());if(!value)return {{},"Receita inválida ou versão não suportada."};return {*value,{}};}
QString saveRenameRecipe(const QString&path,const RenameRecipe&recipe){
    const auto o=encodeRenameRecipe(recipe);if(!decodeRenameRecipe(o))return "Nome ou etapas inválidos.";const auto bytes=QJsonDocument(o).toJson();if(bytes.size()>16384)return "A receita excede 16 KiB.";QLockFile lock(path+".lock");if(!lock.tryLock(0))return "Outra gravação está em andamento.";
    for(const auto &p:{path,path+".bak"}){const QFileInfo info(p);if(info.isSymLink()||info.isJunction()||(info.exists()&&!info.isFile()))return "Receita/backup precisa ser um arquivo normal.";}
    QFile old(path);if(old.exists()){if(!old.open(QIODevice::ReadOnly)||old.size()>16384)return "A receita anterior não pôde ser preservada.";const auto prior=old.read(16385);if(old.error()!=QFileDevice::NoError||prior.size()>16384)return "A receita anterior não pôde ser preservada.";old.close();if(prior==bytes)return {};const auto error=write(path+".bak",prior);if(!error.isEmpty())return error;}
    return write(path,bytes);
}
