#pragma once
#include "externalprocess.h"
#include <QJsonObject>
#include <QThread>
#include <QVector>
#include <optional>
enum class ScriptFieldType { Text, Integer, Boolean, Choice, File, Folder };
struct ScriptField {QString id,label;ScriptFieldType type=ScriptFieldType::Text;QJsonValue initial;QStringList choices;int minimum=-1000000000,maximum=1000000000,maxLength=1024;bool required=false;};
struct UserScript {QString manifest,sourcePath,title,description;QByteArray source,digest;QVector<ScriptField> fields;};
struct UserScriptLoad {std::optional<UserScript> script;QString error;};
UserScriptLoad readUserScript(const QString &manifest);
struct ScriptExecution {QString interpreter,folder,sourcePath;QByteArray source,digest,input;QString error;};
ScriptExecution prepareScriptExecution(const UserScript&,const QString &interpreter,const QString &folder,const QStringList &selection,const QJsonObject &values);
struct ScriptResult {ExternalState state=ExternalState::Failed;int exitCode=-1;QString message,output,errors,preservedDirectory;};
ScriptResult executeUserScript(const ScriptExecution&,const std::atomic_bool &cancel);
class UserScriptJob final:public QThread {
    Q_OBJECT
public:
    UserScriptJob(ScriptExecution execution,QObject *parent=nullptr):QThread(parent),m_execution(std::move(execution)){}
    ~UserScriptJob()override{cancel();wait();}void cancel(){m_cancel=true;}const ScriptResult &result()const{return m_result;}
protected:
    void run()override{m_result=executeUserScript(m_execution,m_cancel);}
private:
    ScriptExecution m_execution;std::atomic_bool m_cancel{false};ScriptResult m_result;
};
