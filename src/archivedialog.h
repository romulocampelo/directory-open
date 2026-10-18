#pragma once
#include "archive.h"
#include <QDialog>
class QLineEdit; class QLabel; class QComboBox; class QTreeWidget; class QPushButton; class QProgressBar;
class ArchiveDialog final : public QDialog {
    Q_OBJECT
public:
    explicit ArchiveDialog(QStringList sources, QString destinationParent, bool create, QWidget *parent = nullptr);
    ~ArchiveDialog() override;
signals:
    void revealOutput(const QString &path);
protected:
    void reject() override;
private:
    QStringList m_sources;
    bool m_create, m_closePending = false;
    ArchiveResult m_catalog;
    ArchiveJob *m_job = nullptr;
    QLineEdit *m_output;
    QComboBox *m_format = nullptr;
    QTreeWidget *m_table;
    QLabel *m_status, *m_details;
    QProgressBar *m_progress;
    QPushButton *m_apply, *m_test = nullptr, *m_reload = nullptr, *m_cancel, *m_reveal, *m_advanced;
    QString m_lastOutput;
    void start(ArchiveAction action);
    void setBusy(bool busy);
};
