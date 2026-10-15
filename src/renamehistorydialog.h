#pragma once
#include "renamehistory.h"
#include <QDialog>
class QTreeWidget; class QLabel; class QPushButton; class QCheckBox;
class RenameHistoryDialog final : public QDialog {
    Q_OBJECT
public:
    explicit RenameHistoryDialog(QString root, QWidget *parent = nullptr);
    ~RenameHistoryDialog() override;
protected:
    void reject() override;
private:
    QString m_root;
    QTreeWidget *m_records, *m_items;
    QLabel *m_status;
    QPushButton *m_refresh, *m_apply, *m_cancel;
    QCheckBox *m_confirm;
    RenameHistoryJob *m_job = nullptr;
    UndoRenamePreview m_preview;
    bool m_closePending = false;
    void start(RenameHistoryRequest request);
    void updateApply();
};
