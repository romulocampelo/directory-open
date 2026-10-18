#pragma once
#include "fileoperation.h"
struct TransferApproval {
    OperationRequest request;
    QVector<RenameIdentity> sources;
    QStringList sourceNames;
    RenameIdentity destination;
    QString error;
    qsizetype retainedBytes=0;
};
// Captures only selected roots. Folder contents are enumerated at execution.
TransferApproval approveTransfer(const OperationRequest &request);
QString revalidateTransfer(const TransferApproval &approval);
OperationResult performApprovedTransfer(const TransferApproval &,const std::atomic_bool &,const OperationProgress &progress={});
class ApprovedTransferJob final:public QThread {
    Q_OBJECT
public:
    ApprovedTransferJob(TransferApproval approval,QObject *parent=nullptr):QThread(parent),m_approval(std::move(approval)){}
    ~ApprovedTransferJob() override {cancel();wait();}
    void cancel(){m_cancel=true;}
    const OperationResult &result()const{return m_result;}
signals:
    void progress(quint32 total,quint32 done,const QString &path);
protected:
    void run()override{m_result=performApprovedTransfer(m_approval,m_cancel,[this](quint32 total,quint32 done,const QString &path){emit progress(total,done,path);});}
private:
    TransferApproval m_approval;std::atomic_bool m_cancel{false};OperationResult m_result;
};
