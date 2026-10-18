#pragma once
#include "interfacestudio.h"
#include <QHash>
#include <QIcon>
class QMenu;
class QAction;
// One vocabulary for context editors and context producers. Commands remain scoped
// to the clicked item, never redirected through the active file selection.
EditableMenu contextMenuDefinition(const QString &id);
void arrangeContextMenu(QMenu &menu,const QStringList &order,const QHash<QString,QAction*> &commands);
struct LauncherPlan { QString program,workingDirectory;QStringList arguments;QString error; };
LauncherPlan planLauncher(const UserLauncher &launcher,const QString &folder,const QStringList &selection);
QIcon readCommandIcon(const QString &path,QString *error=nullptr);
