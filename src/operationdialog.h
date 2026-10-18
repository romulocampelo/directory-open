#pragma once
#include "fileoperation.h"
#include <QDialog>
class QLineEdit;
class QComboBox;
class QLabel;
class OperationDialog final : public QDialog {
    Q_OBJECT
public:
    explicit OperationDialog(OperationRequest request, QWidget *parent = nullptr);
    OperationRequest request() const;
private:
    OperationRequest m_initial;
    QLineEdit *m_destination = nullptr;
    QLineEdit *m_name = nullptr;
    QComboBox *m_conflict = nullptr;
    QComboBox *m_engine = nullptr;
    QLabel *m_error;
};
