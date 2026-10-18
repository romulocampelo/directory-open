#include "workspace.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QTabWidget>
#include <QUuid>
#include <QVBoxLayout>
namespace{
bool integer(const QJsonValue&v,int min,int max){return v.isDouble()&&v.toDouble()==v.toInt(-1)&&v.toInt(-1)>=min&&v.toInt(-1)<=max;}
bool bytes(const QJsonValue&v,QByteArray &out,int max){
    if(!v.isString())return false;const auto b=QByteArray::fromBase64Encoding(v.toString().toLatin1(),QByteArray::AbortOnBase64DecodingErrors);if(!b||b.decoded.size()>max)return false;out=b.decoded;return true;
}
QString metadata(const QString&id,const QString&name,QSet<QString>&ids,QSet<QString>&names){
    if(QUuid(id).isNull()||ids.contains(QUuid(id).toString())||name.trimmed().isEmpty()||name.size()>128||name.contains(QChar(0))||names.contains(name.trimmed().toCaseFolded()))return "Nome vazio/repetido ou identificação inválida.";
    ids.insert(QUuid(id).toString());names.insert(name.trimmed().toCaseFolded());return {};
}
}
QString validateWorkspaces(const QVector<SavedWorkspace>&layouts,const QVector<SavedTabGroup>&groups){
    if(layouts.size()>16||groups.size()>32)return "Guarde até 16 layouts e 32 grupos de abas.";QSet<QString>ids,names;
    for(const auto &s:layouts){auto error=metadata(s.id,s.name,ids,names);if(!error.isEmpty())return error;
        if(!decodePanelSession(encodePanelSession(s.left))||!decodePanelSession(encodePanelSession(s.right))||!validateUiCustomization(s.ui).isEmpty()||s.geometry.size()>4096||s.windowState.size()>65536||s.splitter.size()>4096||s.activePanel<0||s.activePanel>1)return "Layout inválido ou grande demais.";
    }
    names.clear();for(const auto &s:groups){auto error=metadata(s.id,s.name,ids,names);if(!error.isEmpty())return error;if(!decodePanelSession(encodePanelSession(s.panel)))return "Grupo de abas inválido.";}
    if(QJsonDocument(QJsonObject{{"layouts",encodeWorkspaces(layouts)},{"groups",encodeTabGroups(groups)}}).toJson(QJsonDocument::Compact).size()>512*1024)return "Os catálogos excedem 512 KiB. Reduza o número de abas ou layouts guardados.";
    return {};
}
QJsonArray encodeWorkspaces(const QVector<SavedWorkspace>&entries){QJsonArray a;for(const auto&s:entries)a.append(QJsonObject{{"id",s.id},{"name",s.name},{"left",encodePanelSession(s.left)},{"right",encodePanelSession(s.right)},{"interface",encodeUiCustomization(s.ui)},
    {"geometry",QString::fromLatin1(s.geometry.toBase64())},{"windowState",QString::fromLatin1(s.windowState.toBase64())},{"splitter",QString::fromLatin1(s.splitter.toBase64())},
    {"dark",s.dark},{"dual",s.dual},{"tree",s.tree},{"independentTrees",s.independentTrees},{"preview",s.preview},{"activePanel",s.activePanel}});return a;}
QJsonArray encodeTabGroups(const QVector<SavedTabGroup>&entries){QJsonArray a;for(const auto&s:entries)a.append(QJsonObject{{"id",s.id},{"name",s.name},{"panel",encodePanelSession(s.panel)}});return a;}
std::optional<QVector<SavedWorkspace>> decodeWorkspaces(const QJsonValue&v){
    if(!v.isArray()||v.toArray().size()>16)return {};QVector<SavedWorkspace> result;
    for(const auto &value:v.toArray()){
        if(!value.isObject())return {};const auto o=value.toObject();if(!o["id"].isString()||!o["name"].isString()||!integer(o["activePanel"],0,1))return {};
        for(const auto &key:{"dark","dual","tree","independentTrees","preview"})if(!o[key].isBool())return {};
        const auto left=decodePanelSession(o["left"]),right=decodePanelSession(o["right"]);const auto ui=decodeUiCustomization(o["interface"]);if(!left||!right||!ui)return {};
        SavedWorkspace s;s.id=o["id"].toString();s.name=o["name"].toString();s.left=*left;s.right=*right;s.ui=*ui;
        if(!bytes(o["geometry"],s.geometry,4096)||!bytes(o["windowState"],s.windowState,65536)||!bytes(o["splitter"],s.splitter,4096))return {};
        s.dark=o["dark"].toBool();s.dual=o["dual"].toBool();s.tree=o["tree"].toBool();s.independentTrees=o["independentTrees"].toBool();s.preview=o["preview"].toBool();s.activePanel=o["activePanel"].toInt();result.append(s);
    }if(!validateWorkspaces(result,{}).isEmpty())return {};return result;
}
std::optional<QVector<SavedTabGroup>> decodeTabGroups(const QJsonValue&v){
    if(!v.isArray()||v.toArray().size()>32)return {};QVector<SavedTabGroup> result;
    for(const auto &value:v.toArray()){if(!value.isObject())return {};const auto o=value.toObject();if(!o["id"].isString()||!o["name"].isString())return {};const auto panel=decodePanelSession(o["panel"]);if(!panel)return {};result.append({o["id"].toString(),o["name"].toString(),*panel});}
    if(!validateWorkspaces({},result).isEmpty())return {};return result;
}
WorkspaceDialog::WorkspaceDialog(QVector<SavedWorkspace> layouts,QVector<SavedTabGroup> groups,SavedWorkspace current,PanelSession active,QWidget *parent)
    :QDialog(parent),m_layouts(std::move(layouts)),m_groups(std::move(groups)),m_current(std::move(current)),m_active(std::move(active)){
    setObjectName("workspaceDialog");setWindowTitle(tr("Layouts e grupos de abas"));resize(780,600);auto *root=new QVBoxLayout(this);
    auto *hint=new QLabel(tr("Guarde e restaure a organização do trabalho. Restaurar substitui as abas da janela ou do painel escolhido; não altera arquivos. Cancelar descarta edições ainda não confirmadas."),this);hint->setWordWrap(true);root->addWidget(hint);
    auto *tabs=new QTabWidget(this);root->addWidget(tabs,1);
    auto *error=new QLabel(this);error->setObjectName("workspaceError");error->setWordWrap(true);
    m_appearance=new QCheckBox(tr("Ao restaurar um layout, aplicar também aparência, barras e atalhos"),this);m_appearance->setChecked(true);
    for(bool group:{false,true}){
        auto *page=new QWidget(tabs);auto *layout=new QVBoxLayout(page);auto *list=new QListWidget(page);list->setObjectName(group?"tabGroupList":"layoutList");layout->addWidget(list,1);
        auto *name=new QLineEdit(page);name->setObjectName(group?"tabGroupName":"workspaceName");name->setMaxLength(128);name->setPlaceholderText(tr("Nome para guardar a organização atual"));layout->addWidget(name);
        auto refresh=[this,list,group]{list->clear();if(group){for(const auto &s:m_groups)list->addItem(QString("%1 — %2 abas").arg(s.name).arg(s.panel.tabs.size()));}else{for(const auto&s:m_layouts)list->addItem(QString("%1 — %2 / %3 abas").arg(s.name).arg(s.left.tabs.size()).arg(s.right.tabs.size()));}};refresh();
        auto *row=new QHBoxLayout;layout->addLayout(row);
        auto *save=new QPushButton(tr("Guardar atual"),page),*rename=new QPushButton(tr("Renomear"),page),*remove=new QPushButton(tr("Remover do catálogo"),page);
        save->setObjectName(group?"saveTabGroup":"saveWorkspace");rename->setObjectName(group?"renameTabGroup":"renameWorkspace");remove->setObjectName(group?"removeTabGroup":"removeWorkspace");
        for(auto *b:{save,rename,remove})row->addWidget(b);
        connect(save,&QPushButton::clicked,this,[this,group,name,refresh,error]{
            auto layouts=m_layouts;auto groups=m_groups;
            if(group)groups.append({QUuid::createUuid().toString(QUuid::WithoutBraces),name->text().trimmed(),m_active});else{auto value=m_current;value.id=QUuid::createUuid().toString(QUuid::WithoutBraces);value.name=name->text().trimmed();layouts.append(value);}
            const auto message=validateWorkspaces(layouts,groups);error->setText(message);if(!message.isEmpty())return;m_layouts=layouts;m_groups=groups;refresh();name->clear();
        });
        connect(rename,&QPushButton::clicked,this,[this,group,list,name,refresh,error]{const int i=list->currentRow();if(i<0)return;auto layouts=m_layouts;auto groups=m_groups;if(group)groups[i].name=name->text().trimmed();else layouts[i].name=name->text().trimmed();const auto message=validateWorkspaces(layouts,groups);error->setText(message);if(!message.isEmpty())return;m_layouts=layouts;m_groups=groups;refresh();});
        connect(remove,&QPushButton::clicked,this,[this,group,list,refresh]{const int i=list->currentRow();if(i<0)return;if(group)m_groups.removeAt(i);else m_layouts.removeAt(i);refresh();});
        connect(list,&QListWidget::currentRowChanged,this,[this,group,name](int i){if(i<0)return;name->setText(group?m_groups[i].name:m_layouts[i].name);});
        auto *open=new QPushButton(group?tr("Substituir abas do painel ativo"):tr("Restaurar nesta janela"),page);layout->addWidget(open);open->setObjectName(group?"applyTabGroup":"applyWorkspace");
        connect(open,&QPushButton::clicked,this,[this,group,list]{const int i=list->currentRow();if(i<0)return;if(group)m_chosenGroup=m_groups[i];else m_chosenLayout=m_layouts[i];accept();});
        if(!group){auto *style=new QPushButton(tr("Aplicar somente aparência e conjunto de barras"),page);style->setObjectName("applyWorkspaceStyle");layout->addWidget(style);connect(style,&QPushButton::clicked,this,[this,list]{const int i=list->currentRow();if(i<0)return;m_chosenLayout=m_layouts[i];m_appearanceOnly=true;accept();});auto *another=new QPushButton(tr("Abrir layout em nova janela"),page);another->setObjectName("openWorkspaceWindow");layout->addWidget(another);connect(another,&QPushButton::clicked,this,[this,list]{const int i=list->currentRow();if(i<0)return;m_chosenLayout=m_layouts[i];m_newWindow=true;accept();});}
        tabs->addTab(page,group?tr("Grupos de abas"):tr("Layouts"));
    }
    root->addWidget(m_appearance);root->addWidget(error);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));root->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,this,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
}
bool WorkspaceDialog::appearance()const{return m_appearance->isChecked();}
