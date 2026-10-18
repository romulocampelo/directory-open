#pragma once
#include "guardedtransfer.h"
#include <QDialog>
#include <functional>
class QPlainTextEdit;class QLineEdit;class QComboBox;class QListWidget;class QTreeWidget;class QLabel;class QPushButton;class QProgressBar;
class OperationQueueDialog final:public QDialog {
    Q_OBJECT
public:
    OperationQueueDialog(bool nativeUi,std::function<bool()> canRun,QWidget *parent=nullptr);
    ~OperationQueueDialog()override;
    void setDraft(const QStringList &sources,const QString &destination,TransferEngine engine);
    bool busy()const{return m_job!=nullptr;}
    bool activeMovesFiles()const{return m_active&&m_active->approval.request.kind==OperationKind::Move;}
    bool hasWork()const{return busy()||!m_pending.isEmpty()||m_confirming;}
    int pendingCount()const{return m_pending.size();}
    bool requestShutdown();
signals:
    void busyChanged();
protected:
    void reject()override;void closeEvent(QCloseEvent*)override;
private:
    struct Pending {quint64 id;TransferApproval approval;};
    struct Record {quint64 id;OperationRequest request;OperationResult result;qsizetype bytes=0;};
    QVector<Pending> m_pending;QVector<Record> m_history;std::optional<Pending> m_active;
    quint64 m_nextId=1;qsizetype m_pendingBytes=0,m_historyBytes=0;
    bool m_nativeUi,m_paused=true,m_confirming=false,m_closing=false,m_shutdownApproved=false;
    std::function<bool()> m_canRun;
    QStringList m_sources;TransferEngine m_preferredEngine=TransferEngine::Windows;
    ApprovedTransferJob *m_job=nullptr;
    QPlainTextEdit *m_sourcesText,*m_details;QLineEdit *m_destination;QComboBox *m_kind;QListWidget *m_waiting;QTreeWidget *m_results;
    QLabel *m_status,*m_activeText;QPushButton *m_add,*m_start,*m_pause,*m_cancel;QProgressBar *m_progress;
    void addDraft();void startNext();void refresh();void showDetails();
};
