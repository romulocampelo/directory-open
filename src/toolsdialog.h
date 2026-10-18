#pragma once
#include <QDialog>
#include "specializedtools.h"
class QLineEdit;
class QCheckBox;
class QTreeWidget;
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QComboBox;
enum class ToolPage { Content, Backup, Remote };
class ToolsDialog final : public QDialog {
    Q_OBJECT
public:
    ToolsDialog(ToolPage page, ToolSettings settings, const QString &root, const QStringList &selection, QWidget *parent = nullptr);
    ~ToolsDialog() override;
    ToolSettings settings() const;
signals:
    void openFileRequested(QString path);
    void openFolderRequested(QString path);
protected:
    void reject() override;
    void closeEvent(QCloseEvent *event) override;
private:
    ToolPage m_page;
    ToolSettings m_settings;
    QStringList m_sources;
    QLineEdit *m_exe, *m_root = nullptr, *m_pattern = nullptr, *m_repo = nullptr, *m_password = nullptr;
    QLineEdit *m_config = nullptr, *m_remote = nullptr, *m_local = nullptr, *m_restore = nullptr;
    QCheckBox *m_regex = nullptr, *m_case = nullptr, *m_hidden = nullptr, *m_ignored = nullptr;
    QComboBox *m_direction = nullptr;
    QTreeWidget *m_results;
    QPlainTextEdit *m_diagnostics;
    QLabel *m_status;
    QWidget *m_form, *m_commands;
    QPushButton *m_cancel, *m_copy = nullptr;
    ToolJob *m_job = nullptr;
    bool m_closing = false;
    ToolAction m_lastAction = ToolAction::ContentSearch;
    QString m_previewKey, m_catalogKey;
    ToolRequest request(ToolAction action) const;
    QString transferKey() const;
    void start(ToolAction action);
    void finish();
};
