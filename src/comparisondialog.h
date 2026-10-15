#pragma once
#include "foldercomparison.h"
#include <QDialog>
class QLineEdit;
class QCheckBox;
class QTreeWidget;
class QLabel;
class QPushButton;
class QTimer;
class ComparisonDialog final : public QDialog {
    Q_OBJECT
public:
    explicit ComparisonDialog(const QString &left, const QString &right, QWidget *parent = nullptr);
    ~ComparisonDialog() override;
signals:
    void openFolderRequested(QString path, bool left);
protected:
    void reject() override;
private:
    QLineEdit *m_left, *m_right;
    QCheckBox *m_recursive, *m_content, *m_differences;
    QTreeWidget *m_results;
    QLabel *m_status;
    QPushButton *m_start, *m_cancel, *m_leftBrowse, *m_rightBrowse, *m_openLeft, *m_openRight;
    QTimer *m_timer;
    ComparisonJob *m_job = nullptr;
    ComparisonReport m_report;
    bool m_closeWhenFinished = false, m_cancelRequested = false;
    void startComparison();
    void showReport();
    void filterResults();
    void openFolder(bool left);
};
