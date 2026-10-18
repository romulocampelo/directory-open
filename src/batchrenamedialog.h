#pragma once
#include "batchrename.h"
#include <QDialog>
#include <QHash>
class QTreeWidgetItem;
class QLineEdit; class QCheckBox; class QComboBox; class QSpinBox; class QDateEdit;
class QTreeWidget; class QLabel; class QPushButton; class QTimer;
class BatchRenameDialog final : public QDialog {
    Q_OBJECT
public:
    explicit BatchRenameDialog(QStringList sources, QWidget *parent = nullptr, QString historyRoot = {});
    ~BatchRenameDialog() override;
    bool applied() const { return m_applied; }
protected:
    void reject() override;
private:
    QStringList m_sources;
    QString m_historyRoot;
    RenameRecipe m_recipe;
    QPushButton *m_recipeButton;
    QHash<QString, QTreeWidgetItem *> m_rows;
    QLineEdit *m_pattern, *m_find, *m_replace;
    QCheckBox *m_extension, *m_case, *m_confirm;
    QSpinBox *m_start, *m_step, *m_digits;
    QComboBox *m_dateSource, *m_dateFormat;
    QDateEdit *m_date;
    QTreeWidget *m_table;
    QLabel *m_status;
    QPushButton *m_preview, *m_apply, *m_cancel, *m_close;
    QTimer *m_timer;
    BatchRenamePlan m_plan;
    BatchRenameJob *m_job = nullptr;
    bool m_closePending = false, m_applying = false, m_applied = false;
    void invalidate();
    void startPreview();
    void apply();
    void setBusy(bool busy);
};
