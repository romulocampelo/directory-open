#pragma once
#include "mediatools.h"
#include <QDialog>
class QLineEdit; class QComboBox; class QSpinBox; class QTreeWidget; class QLabel; class QPushButton; class QPlainTextEdit;
class MediaToolsDialog final : public QDialog {
    Q_OBJECT
public:
    MediaToolsDialog(MediaTask task, ToolSettings settings, QStringList sources, const QString &destinationParent, QWidget *parent=nullptr);
    ~MediaToolsDialog() override;
    ToolSettings settings() const;
signals:
    void openOutput(QString path);
protected:
    void reject() override;
    void closeEvent(QCloseEvent *) override;
private:
    MediaTask m_task;
    ToolSettings m_settings;
    QStringList m_sources;
    QLineEdit *m_exe,*m_probe=nullptr,*m_destination,*m_value=nullptr;
    QComboBox *m_format=nullptr,*m_rotation=nullptr,*m_tag=nullptr;
    QSpinBox *m_width=nullptr,*m_height=nullptr,*m_quality=nullptr;
    QTreeWidget *m_rows,*m_edits=nullptr;
    QPlainTextEdit *m_details;
    QLabel *m_status;
    QLabel *m_imagePreview=nullptr;
    QPushButton *m_preview=nullptr,*m_apply=nullptr,*m_cancel=nullptr,*m_open=nullptr;
    QWidget *m_controls;
    MediaToolsJob *m_job=nullptr;
    MediaPlan m_plan;
    bool m_applying=false,m_closing=false;
    MediaOptions options() const;
    void invalidate();
    void inspect();
    void apply();
    void finished();
};
