#pragma once
#include "foldersize.h"
#include <QDialog>
class QLabel; class QPlainTextEdit; class QPushButton; class QProgressBar; class QTimer; class QCheckBox;
class FolderSizeDialog final : public QDialog {
    Q_OBJECT
public:
    explicit FolderSizeDialog(FolderSizeRequest request, QWidget *parent = nullptr);
    ~FolderSizeDialog() override;
protected:
    void reject() override;
private:
    FolderSizeRequest m_request;
    FolderSizeJob *m_job = nullptr;
    QLabel *m_status;
    QPlainTextEdit *m_details;
    QPushButton *m_start, *m_cancel;
    QPushButton *m_csv, *m_text, *m_print;
    QCheckBox *m_listing;
    QProgressBar *m_progress;
    QTimer *m_poll;
    bool m_closePending = false;
    void start();
    void render();
    void exportReport(bool csv);
};
