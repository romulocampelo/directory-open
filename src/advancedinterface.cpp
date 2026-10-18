#include "advancedinterface.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequence>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
namespace {
bool text(const QString&s,int max){return s.size()<=max&&!s.contains(QChar(0));}
bool color(const QString&s){static QRegularExpression re("^#[0-9a-fA-F]{6}$");return s.isEmpty()||re.match(s).hasMatch();}
bool commands(const QStringList &ids){if(ids.size()>128)return false;QSet<QString>seen;for(const auto&id:ids){if(id.isEmpty()||!text(id,80)||(id!="-"&&seen.contains(id)))return false;if(id!="-")seen.insert(id);}return true;}
QJsonArray strings(const QStringList&s){QJsonArray a;for(const auto &v:s)a.append(v);return a;}
std::optional<QStringList> strings(const QJsonValue &v){if(!v.isArray())return {};QStringList result;for(const auto &x:v.toArray()){if(!x.isString())return {};result.append(x.toString());}return result;}
QJsonObject maps(const QMap<QString,QStringList>&m){QJsonObject o;for(auto i=m.begin();i!=m.end();++i)o[i.key()]=strings(i.value());return o;}
QJsonObject maps(const QMap<QString,QString>&m){QJsonObject o;for(auto i=m.begin();i!=m.end();++i)o[i.key()]=i.value();return o;}
}
QStringList interfaceComponentIds(){return {"files","trees","menus","toolbars","tabs","viewer"};}
QStringList interfaceMenuIds(){return {"fileMenu","editMenu","viewMenu","folderMenu","toolsMenu","settingsMenu","helpMenu"};}
QString validateAdvancedInterface(const AdvancedInterface &v){
    if(v.toolbars.size()>16||v.launchers.size()>32||v.icons.size()>128||v.globalKeys.size()>32||v.components.size()>6||v.menus.size()>7||v.contexts.size()>3)return "A personalização excede os limites de barras, comandos ou componentes.";
    QSet<QString>ids,names;
    for(const auto &bar:v.toolbars){if(QUuid(bar.id).isNull()||ids.contains(QUuid(bar.id).toString())||bar.name.trimmed().isEmpty()||!text(bar.name,128)||names.contains(bar.name.toCaseFolded())||!commands(bar.commands))return "Barra inválida ou repetida.";ids.insert(QUuid(bar.id).toString());names.insert(bar.name.toCaseFolded());}
    for(auto i=v.menus.begin();i!=v.menus.end();++i)if(!interfaceMenuIds().contains(i.key())||!commands(i.value()))return "Menu inválido.";
    for(auto i=v.contexts.begin();i!=v.contexts.end();++i)if(!QStringList{"files","tree","tabs"}.contains(i.key())||!commands(i.value()))return "Menu de contexto inválido.";
    for(auto i=v.icons.begin();i!=v.icons.end();++i)if(i.key().isEmpty()||!text(i.key(),80)||!QDir::isAbsolutePath(i.value())||!text(i.value(),32768))return "Caminho de ícone inválido.";
    for(auto i=v.components.begin();i!=v.components.end();++i)if(!interfaceComponentIds().contains(i.key())||!color(i->background)||!color(i->foreground)||(i->fontSize!=0&&(i->fontSize<8||i->fontSize>24)))return "Cor ou fonte de componente inválida.";
    ids.clear();names.clear();for(const auto &c:v.launchers){if(QUuid(c.id).isNull()||ids.contains(QUuid(c.id).toString())||c.name.trimmed().isEmpty()||!text(c.name,128)||names.contains(c.name.toCaseFolded())||!QDir::isAbsolutePath(c.program)||!text(c.program,32768)||c.arguments.size()>64)return "Lançador inválido ou repetido.";for(const auto &a:c.arguments)if(!text(a,4096))return "Argumento de lançador inválido.";ids.insert(QUuid(c.id).toString());names.insert(c.name.toCaseFolded());}
    for(auto i=v.globalKeys.begin();i!=v.globalKeys.end();++i){const auto key=QKeySequence::fromString(i.value(),QKeySequence::PortableText);if(i.key().isEmpty()||!text(i.key(),80)||i.value().size()>128||key.count()!=1||key.isEmpty()||key.toString(QKeySequence::PortableText)!=i.value())return "Use uma combinação simples em cada atalho global.";}
    if(!v.languageFile.isEmpty()&&(!QDir::isAbsolutePath(v.languageFile)||!text(v.languageFile,32768)))return "Caminho de tradução inválido.";
    if(QJsonDocument(encodeAdvancedInterface(v)).toJson(QJsonDocument::Compact).size()>48*1024)return "A personalização avançada excede 48 KiB.";return {};
}
QJsonObject encodeAdvancedInterface(const AdvancedInterface &v){
    auto o=v.extra;QJsonArray bars,launchers;for(const auto &b:v.toolbars)bars.append(QJsonObject{{"id",b.id},{"name",b.name},{"commands",strings(b.commands)},{"visible",b.visible},{"movable",b.movable}});
    for(const auto &c:v.launchers)launchers.append(QJsonObject{{"id",c.id},{"name",c.name},{"program",c.program},{"arguments",strings(c.arguments)}});
    QJsonObject styles;for(auto i=v.components.begin();i!=v.components.end();++i)styles[i.key()]=QJsonObject{{"background",i->background},{"foreground",i->foreground},{"fontSize",i->fontSize}};
    o["toolbars"]=bars;o["launchers"]=launchers;o["menus"]=maps(v.menus);o["contexts"]=maps(v.contexts);o["icons"]=maps(v.icons);o["globalKeys"]=maps(v.globalKeys);o["components"]=styles;o["languageFile"]=v.languageFile;o["animations"]=v.animations;return o;
}
std::optional<AdvancedInterface> decodeAdvancedInterface(const QJsonValue &value){
    if(!value.isObject())return {};const auto o=value.toObject();AdvancedInterface v;v.extra=o;
    for(const auto &key:{"toolbars","launchers","menus","contexts","icons","globalKeys","components","languageFile","animations"})v.extra.remove(key);
    for(const auto &key:{"toolbars","launchers"})if(o.contains(key)&&!o[key].isArray())return {};
    for(const auto &x:o["toolbars"].toArray()){if(!x.isObject())return {};const auto b=x.toObject();const auto ids=strings(b["commands"]);if(!b["id"].isString()||!b["name"].isString()||!b["visible"].isBool()||!b["movable"].isBool()||!ids)return {};v.toolbars.append({b["id"].toString(),b["name"].toString(),*ids,b["visible"].toBool(),b["movable"].toBool()});}
    for(const auto &x:o["launchers"].toArray()){if(!x.isObject())return {};const auto c=x.toObject();const auto args=strings(c["arguments"]);if(!c["id"].isString()||!c["name"].isString()||!c["program"].isString()||!args)return {};v.launchers.append({c["id"].toString(),c["name"].toString(),c["program"].toString(),*args});}
    for(const auto &key:{"menus","contexts","icons","globalKeys","components"})if(o.contains(key)&&!o[key].isObject())return {};
    for(const auto &key:{"menus","contexts"}){const auto entries=o[key].toObject();for(auto i=entries.begin();i!=entries.end();++i){const auto list=strings(i.value());if(!list)return {};if(QString(key)=="menus")v.menus[i.key()]=*list;else v.contexts[i.key()]=*list;}}
    for(const auto &key:{"icons","globalKeys"}){const auto entries=o[key].toObject();for(auto i=entries.begin();i!=entries.end();++i){if(!i.value().isString())return {};if(QString(key)=="icons")v.icons[i.key()]=i.value().toString();else v.globalKeys[i.key()]=i.value().toString();}}
    const auto styles=o["components"].toObject();for(auto i=styles.begin();i!=styles.end();++i){if(!i.value().isObject())return {};const auto c=i.value().toObject();if(!c["background"].isString()||!c["foreground"].isString()||!c["fontSize"].isDouble()||c["fontSize"].toDouble()!=c["fontSize"].toInt(-1))return {};v.components[i.key()]={c["background"].toString(),c["foreground"].toString(),c["fontSize"].toInt()};}
    if(o.contains("languageFile")&&!o["languageFile"].isString())return {};if(o.contains("animations")&&!o["animations"].isBool())return {};v.languageFile=o["languageFile"].toString();v.animations=o["animations"].toBool();if(!validateAdvancedInterface(v).isEmpty())return {};return v;
}
QString componentStyleSheet(const AdvancedInterface &v){
    const QMap<QString,QString> selectors{{"files","QWidget#filePanel QTreeView#files, QWidget#filePanel QListView#fileThumbnails"},{"trees","QTreeView#folderTree, QTreeView#panelTree"},{"menus","QMenu, QMenuBar"},{"toolbars","QToolBar, QToolBar QToolButton"},{"tabs","QTabBar::tab"},{"viewer","QWidget#filePreview, QWidget#filePreview QTextEdit, QWidget#filePreview QPlainTextEdit"}};
    QString sheet;for(auto i=v.components.begin();i!=v.components.end();++i){sheet+=selectors.value(i.key())+" {";if(!i->background.isEmpty())sheet+="background:"+i->background+";";if(!i->foreground.isEmpty())sheet+="color:"+i->foreground+";";if(i->fontSize)sheet+=QString("font-size:%1pt;").arg(i->fontSize);sheet+="}\n";}return sheet;
}
