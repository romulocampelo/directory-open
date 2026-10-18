#pragma once
#include "panelsession.h"
#include "uicustomization.h"
#include <QDialog>
struct SavedWorkspace {
    QString id,name;
    PanelSession left,right;
    UiCustomization ui;
    QByteArray geometry,windowState,splitter;
    bool dark=false,dual=true,tree=true,independentTrees=false,preview=false;
    int activePanel=0;
};
struct SavedTabGroup { QString id,name;PanelSession panel; };
QString validateWorkspaces(const QVector<SavedWorkspace> &layouts,const QVector<SavedTabGroup> &groups);
std::optional<QVector<SavedWorkspace>> decodeWorkspaces(const QJsonValue &value);
std::optional<QVector<SavedTabGroup>> decodeTabGroups(const QJsonValue &value);
QJsonArray encodeWorkspaces(const QVector<SavedWorkspace> &entries);
QJsonArray encodeTabGroups(const QVector<SavedTabGroup> &entries);
class QListWidget;
class QCheckBox;
class WorkspaceDialog final:public QDialog{
    Q_OBJECT
public:
    WorkspaceDialog(QVector<SavedWorkspace> layouts,QVector<SavedTabGroup> groups,SavedWorkspace current,PanelSession active,QWidget *parent);
    QVector<SavedWorkspace> layouts()const{return m_layouts;}
    QVector<SavedTabGroup> groups()const{return m_groups;}
    std::optional<SavedWorkspace> chosenLayout()const{return m_chosenLayout;}
    std::optional<SavedTabGroup> chosenGroup()const{return m_chosenGroup;}
    bool newWindow()const{return m_newWindow;}
    bool appearance()const;
    bool appearanceOnly()const{return m_appearanceOnly;}
private:
    QVector<SavedWorkspace> m_layouts;
    QVector<SavedTabGroup> m_groups;
    SavedWorkspace m_current;
    PanelSession m_active;
    std::optional<SavedWorkspace> m_chosenLayout;
    std::optional<SavedTabGroup> m_chosenGroup;
    bool m_newWindow=false;
    bool m_appearanceOnly=false;
    QCheckBox *m_appearance;
};
