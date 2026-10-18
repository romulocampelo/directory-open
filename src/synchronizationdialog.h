#pragma once
#include "synchronization.h"
#include <QDialog>
class QComboBox;class QCheckBox;class QTreeWidget;class QPlainTextEdit;class QLabel;class QPushButton;
class SynchronizationDialog final:public QDialog {
    Q_OBJECT
public:
    SynchronizationDialog(SynchronizationRequest request,QWidget *parent=nullptr);
    ~SynchronizationDialog()override;
protected:
    void reject()override;
private:
    SynchronizationRequest m_request;SynchronizationPlan m_plan;SynchronizationJob *m_job=nullptr;
    QComboBox *m_direction,*m_conflicts;QCheckBox *m_include,*m_confirm;QTreeWidget *m_rows;QPlainTextEdit *m_details;QLabel *m_status;QPushButton *m_preview,*m_apply,*m_cancel;
    bool m_closePending=false,m_applying=false,m_applied=false;
    void invalidate();void preview();void apply();void setBusy(bool);void finish();
};
