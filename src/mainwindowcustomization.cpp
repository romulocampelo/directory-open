#include "mainwindow.h"
#include "filepanel.h"
#include "interfacecommands.h"
#include "globalhotkeys.h"
#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QScopedValueRollback>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeView>
#include <QStyle>
#include <QSet>
#include <functional>
#include <algorithm>

void MainWindow::captureInterfaceDefaults(){
    for(const auto &c:m_commands)m_defaultIcons[c.id]=c.action->icon();
    for(const auto &id:interfaceMenuIds())if(auto *menu=findChild<QMenu*>(id)){
        QVector<MenuNode> nodes;
        for(auto *a:menu->actions()){
            if(a->isSeparator())nodes.append({{},nullptr,true});
            else if(a->menu())nodes.append({{},a->menu(),false});
            else nodes.append({a->objectName(),nullptr,false});
        }m_defaultMenus[id]=nodes;
    }
}
void MainWindow::synchronizeLaunchers(){
    QSet<QString> keep;for(const auto &launcher:m_ui.advanced.launchers)keep.insert("user."+launcher.id);
    for(auto i=m_commands.begin();i!=m_commands.end();){
        if(i->id.startsWith("user.")&&!keep.contains(i->id)){auto *action=i->action;m_defaultIcons.remove(i->id);i=m_commands.erase(i);delete action;}else ++i;
    }
    for(const auto &launcher:m_ui.advanced.launchers){
        const auto id="user."+launcher.id;auto found=std::find_if(m_commands.begin(),m_commands.end(),[&](const UiCommand&c){return c.id==id;});
        if(found!=m_commands.end()){found->action->setText(launcher.name);continue;}
        auto *a=new QAction(style()->standardIcon(QStyle::SP_ComputerIcon),launcher.name,this);a->setObjectName(id);addAction(a);connect(a,&QAction::triggered,this,[this,id]{launchUserCommand(id);});m_commands.append({id,tr("Comandos próprios"),a,{},false});m_defaultIcons[id]=a->icon();
    }
}
void MainWindow::applyAdvancedInterface(){
    const auto &value=m_ui.advanced;QHash<QString,QAction*> commands;for(const auto &c:m_commands)if(!c.toolbarOnly)commands[c.id]=c.action;
    QSet<QString>keep;for(const auto &bar:value.toolbars)keep.insert(bar.id);
    for(auto i=m_namedToolbars.begin();i!=m_namedToolbars.end();)if(!keep.contains(i.key())){delete i.value();i=m_namedToolbars.erase(i);}else ++i;
    const Qt::ToolButtonStyle styles[]{Qt::ToolButtonTextBesideIcon,Qt::ToolButtonIconOnly,Qt::ToolButtonTextUnderIcon};
    for(const auto &bar:value.toolbars){
        auto *widget=m_namedToolbars.value(bar.id);if(!widget){widget=new QToolBar(bar.name,this);widget->setObjectName("customToolbar_"+bar.id);addToolBar(widget);m_namedToolbars[bar.id]=widget;}
        widget->setWindowTitle(bar.name);for(auto *a:widget->actions()){widget->removeAction(a);if(a->isSeparator())delete a;}
        for(const auto &id:bar.commands){if(id=="-")widget->addSeparator();else if(commands.contains(id))widget->addAction(commands[id]);}
        widget->setIconSize({m_ui.iconSize,m_ui.iconSize});widget->setToolButtonStyle(styles[m_ui.buttonStyle]);widget->setMovable(bar.movable);widget->setFloatable(bar.movable);widget->setVisible(bar.visible);
    }
    for(const auto &id:interfaceMenuIds())if(auto *menu=findChild<QMenu*>(id)){
        for(auto *a:menu->actions()){menu->removeAction(a);if(a->isSeparator())delete a;}
        if(value.menus.contains(id)){for(const auto &command:value.menus[id]){if(command=="-")menu->addSeparator();else if(commands.contains(command))menu->addAction(commands[command]);}}
        else for(const auto &node:m_defaultMenus[id]){if(node.separator)menu->addSeparator();else if(node.submenu)menu->addMenu(node.submenu);else if(commands.contains(node.command))menu->addAction(commands[node.command]);}
        if(id=="settingsMenu")for(const auto &recovery:{"preferences","commandCenter","interfaceStudio"})if(commands.contains(recovery)&&!menu->actions().contains(commands[recovery]))menu->addAction(commands[recovery]);
    }
    QStringList failures;for(const auto &c:m_commands){QIcon icon=m_defaultIcons.value(c.id);if(value.icons.contains(c.id)){QString error;const auto loaded=readCommandIcon(value.icons[c.id],&error);if(!loaded.isNull())icon=loaded;else failures.append(commandLabel(c)+": "+error);}c.action->setIcon(icon);}
    if(!failures.isEmpty())statusBar()->showMessage(tr("Alguns ícones mantiveram o padrão: %1").arg(failures.mid(0,3).join("; ")));
    for(auto *panel:{m_left,m_right})panel->setTabMenu(value.contexts.contains("tabs")?std::optional<QStringList>(value.contexts["tabs"]):std::nullopt);
    setAnimated(value.animations);m_tree->setAnimated(value.animations);for(auto *panel:{m_left,m_right})if(panel->folderTree())panel->folderTree()->setAnimated(value.animations);
}
void MainWindow::showInterfaceStudio(){
    if(operationBusy()||m_confirming||m_viewerDialog)return;
    {
        QScopedValueRollback<bool>guard(m_confirming,true);updateOperationActions();QVector<EditorCommand>commands;for(const auto &c:m_commands)if(!c.toolbarOnly)commands.append({c.id,c.category+" — "+commandLabel(c)});
        QVector<EditableMenu>menus;
        for(const auto &id:interfaceMenuIds())if(auto *menu=findChild<QMenu*>(id)){
            QStringList defaults;std::function<void(const QVector<MenuNode>&)>append=[&](const QVector<MenuNode>&nodes){for(const auto &n:nodes){if(n.separator)defaults.append("-");else if(n.submenu){for(auto *a:n.submenu->actions())if(a->isSeparator())defaults.append("-");else if(!a->objectName().isEmpty())defaults.append(a->objectName());}else if(!n.command.isEmpty())defaults.append(n.command);}};append(m_defaultMenus[id]);defaults.removeDuplicates();auto label=menu->title();label.remove('&');menus.append({id,label,defaults,{},false});
        }
        for(const auto &id:{"files","tree","tabs"})menus.append(contextMenuDefinition(id));
        InterfaceStudio dialog(session().ui.advanced,commands,menus,this);if(dialog.exec()==QDialog::Accepted){auto candidate=session().ui;candidate.advanced=dialog.value();auto error=validateUiCustomization(candidate);if(error.isEmpty())error=validateUiShortcuts(candidate,m_commands);if(error.isEmpty()){m_ui=candidate;applyCustomization();}else QMessageBox::information(this,tr("Personalização"),error);}
    }updateOperationActions();
}
void MainWindow::launchUserCommand(const QString &id){
    if(operationBusy()||m_confirming||m_viewerDialog)return;
    const auto found=std::find_if(m_ui.advanced.launchers.begin(),m_ui.advanced.launchers.end(),[&](const UserLauncher&c){return "user."+c.id==id;});if(found==m_ui.advanced.launchers.end())return;
    const auto definition=*found;const auto plan=planLauncher(definition,m_active->path(),m_active->selectedPaths());if(!plan.error.isEmpty()){QMessageBox::information(this,tr("Executar comando"),plan.error);return;}
    bool approved=false;
    {QScopedValueRollback<bool>guard(m_confirming,true);updateOperationActions();QMessageBox confirm(QMessageBox::Question,tr("Executar: %1").arg(definition.name),tr("Programa: %1\nPasta: %2\n\nO programa externo terá os seus acessos normais e controlará alterações, progresso e resultado. Importar um perfil não executa comandos.").arg(plan.program,plan.workingDirectory),QMessageBox::Ok|QMessageBox::Cancel,this);confirm.setTextFormat(Qt::PlainText);confirm.setObjectName("launcherConfirmation");confirm.setDetailedText(tr("Argumentos (um por linha):\n%1").arg(plan.arguments.join('\n')));confirm.setDefaultButton(QMessageBox::Cancel);approved=confirm.exec()==QMessageBox::Ok;}
    updateOperationActions();if(!approved)return;
    // Revalidate paths and substitution after the preview. No shell interpretation.
    const auto current=planLauncher(definition,m_active->path(),m_active->selectedPaths());if(!current.error.isEmpty()||current.program!=plan.program||current.workingDirectory!=plan.workingDirectory||current.arguments!=plan.arguments){QMessageBox::information(this,tr("Executar comando"),tr("Os dados mudaram. Abra a prévia novamente."));return;}
    QProcess process;process.setProgram(plan.program);process.setArguments(plan.arguments);process.setWorkingDirectory(plan.workingDirectory);
    if(process.startDetached())statusBar()->showMessage(tr("Comando encaminhado a %1. A conclusão é controlada pelo programa externo.").arg(definition.name));else QMessageBox::information(this,tr("Executar comando"),process.errorString());
}

void MainWindow::showGlobalKeys(){
    if(operationBusy()||m_confirming||m_viewerDialog)return;
    {QScopedValueRollback<bool>guard(m_confirming,true);updateOperationActions();GlobalKeysDialog dialog(m_ui.advanced.globalKeys,m_commands,*m_globalHotkeys,this);if(dialog.exec()==QDialog::Accepted)m_ui.advanced.globalKeys=dialog.definitions();}updateOperationActions();
}
