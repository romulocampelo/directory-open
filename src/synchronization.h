#pragma once
#include "foldercomparison.h"
#include "guardedtransfer.h"
struct SynchronizationRequest {
    QString left,right;QVector<ComparisonEntry> entries;
    bool toRight=true,includeConflicts=false,nativeUi=true;
    OperationConflict conflict=OperationConflict::KeepBoth;quintptr ownerWindow=0;
};
struct SynchronizationItem {
    QString relativePath,target,message;std::optional<TransferApproval> transfer;
    std::optional<RenameIdentity> targetIdentity;QString targetName;
};
struct SynchronizationPlan {
    SynchronizationRequest request;QVector<SynchronizationItem> items;
    RenameIdentity leftIdentity,rightIdentity;QString error;bool cancelled=false;
    bool valid()const;int changes()const;
};
SynchronizationPlan planSynchronization(const SynchronizationRequest&,const std::atomic_bool&);
OperationResult applySynchronization(const SynchronizationPlan&,const std::atomic_bool&,const OperationProgress&progress={});
class SynchronizationJob final:public QThread {
    Q_OBJECT
public:
    SynchronizationJob(SynchronizationRequest request,QObject *parent=nullptr):QThread(parent),m_request(std::move(request)){}
    SynchronizationJob(SynchronizationPlan plan,QObject *parent=nullptr):QThread(parent),m_apply(true),m_plan(std::move(plan)){}
    ~SynchronizationJob()override{cancel();wait();}
    void cancel(){m_cancel=true;}const SynchronizationPlan&plan()const{return m_plan;}const OperationResult&result()const{return m_result;}
signals:
    void progress(quint32 total,quint32 done,const QString &path);
protected:
    void run()override{if(m_apply)m_result=applySynchronization(m_plan,m_cancel,[this](quint32 total,quint32 done,const QString&path){emit progress(total,done,path);});else m_plan=planSynchronization(m_request,m_cancel);}
private:
    bool m_apply=false;SynchronizationRequest m_request;SynchronizationPlan m_plan;OperationResult m_result;std::atomic_bool m_cancel{false};
};
