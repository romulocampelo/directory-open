#pragma once
#include <QStringList>

// Explicit handoff only: a successful start is never an operation receipt.
struct DelegatedCommand { QString program, directory; QStringList arguments; bool console=false; QString error; };
struct DelegatedResult { bool delegated=false; QString message; };
DelegatedCommand duplicateFinderCommand(const QString &project);
DelegatedCommand remoteSetupCommand(const QString &program,const QString &config);
DelegatedCommand archiveManagerCommand(const QString &location);
QString delegatedCommandLine(const DelegatedCommand &command);
DelegatedResult delegateCommand(const DelegatedCommand &command);
enum class ConfigBackupState { Absent, BackedUp, Failed };
struct ConfigBackup { ConfigBackupState state=ConfigBackupState::Failed; QString path,error; };
// Windows copies the file and its security properties without decoding credentials.
ConfigBackup backupRemoteConfig(const QString &config);
