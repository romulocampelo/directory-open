#include "panelsession.h"
#include <QDir>
#include <QJsonArray>
namespace {
constexpr int maxTabs=256;
bool integer(const QJsonValue&v,int min,int max){return v.isDouble()&&v.toDouble()==v.toInt(-1)&&v.toInt(-1)>=min&&v.toInt(-1)<=max;}
bool parsePanel(const QJsonValue &value, PanelSession &panel) {
    if (!value.isObject()) return false; const auto object = value.toObject();
    if (!object["tabs"].isArray()) return false; const auto tabs = object["tabs"].toArray();
    if (tabs.isEmpty() || tabs.size() > maxTabs || !integer(object["currentTab"], 0, int(tabs.size()) - 1)
        || !integer(object["sortColumn"], 0, 3) || !object["descending"].isBool()) return false;
    for (const auto &entry : tabs) {
        if (!entry.isObject()) return false; const auto tab = entry.toObject();
        if (!tab["path"].isString() || !QDir::isAbsolutePath(tab["path"].toString()) || tab["path"].toString().contains(QChar(0)) || !tab["filter"].isString()) return false;
        TabSession saved{tab["path"].toString(),tab["filter"].toString()};
        if(tab.contains("locked")){if(!tab["locked"].isBool())return false;saved.locked=tab["locked"].toBool();}
        if(tab.contains("rules")){const auto rules=decodeFolderRules(tab["rules"]);if(!rules)return false;saved.rules=*rules;}
        panel.tabs.append(saved);
    }
    if(object.contains("view")){
        const auto parsed=decodeFolderView(object["view"]);if(!parsed)return false;panel.view=*parsed;
    }
    if(object.contains("treeSplitter")){
        const auto field=object["treeSplitter"];if(!field.isString())return false;
        const auto decoded=QByteArray::fromBase64Encoding(field.toString().toLatin1(),QByteArray::AbortOnBase64DecodingErrors);if(!decoded||decoded.decoded.size()>4096)return false;panel.treeSplitter=decoded.decoded;
    }
    panel.currentTab = object["currentTab"].toInt(); panel.sortColumn = object["sortColumn"].toInt(); panel.descending = object["descending"].toBool(); return true;
}
}
std::optional<PanelSession> decodePanelSession(const QJsonValue&v){PanelSession panel;if(!parsePanel(v,panel))return {};return panel;}
QJsonObject encodePanelSession(const PanelSession &panel) {
    QJsonArray tabs; for (const auto &tab : panel.tabs) tabs.append(QJsonObject{{"path", tab.path}, {"filter", tab.filter},{"locked",tab.locked},{"rules",encodeFolderRules(tab.rules)}});
    QJsonObject o{{"tabs", tabs}, {"currentTab", panel.currentTab}, {"sortColumn", panel.sortColumn}, {"descending", panel.descending},{"treeSplitter",QString::fromLatin1(panel.treeSplitter.toBase64())}};
    if(panel.view)o["view"]=encodeFolderView(*panel.view);return o;
}
