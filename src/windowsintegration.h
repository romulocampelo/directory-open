#pragma once
#include <QString>
#include <QByteArray>
#include <QUrl>
enum class AssociationState { Assigned, Missing, Failed };
struct FileAssociation {AssociationState state=AssociationState::Failed;QString application,executable,programId,error;};
QString validateAssociationExtension(const QString &extension);
FileAssociation queryFileAssociation(const QString &extension);
QUrl defaultAppsSettingsUrl();
// Production uses only HKCU. Tests borrow an isolated, caller-owned registry key;
// neither the application nor tests redirect process-global predefined hives.
struct FolderRegistry {quintptr hive=0;QString classes,state;bool isolated=false;};
FolderRegistry currentUserFolderRegistry();
FolderRegistry isolatedFolderRegistry(quintptr fixtureKey);
struct FolderIntegrationSnapshot {
    QByteArray revision;QString error,current,previous,executable;
    bool recorded=false,enabled=false,aligned=false,pending=false;
};
enum class IntegrationChangeState { Applied, Unchanged, Refused, Unconfirmed };
struct IntegrationChange {IntegrationChangeState state=IntegrationChangeState::Refused;QString message;};
QString folderOpenCommand(const QString &executable);
FolderIntegrationSnapshot inspectFolderIntegration(const FolderRegistry&);
IntegrationChange enableFolderIntegration(const FolderRegistry&,const QString &executable,const QByteArray &expectedRevision);
IntegrationChange restoreFolderIntegration(const FolderRegistry&,const QByteArray &expectedRevision);
