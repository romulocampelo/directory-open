#pragma once
#include "ocr.h"
#include <QDialog>
class QComboBox;class QPlainTextEdit;class QLabel;class QPushButton;
class OcrDialog final:public QDialog{
    Q_OBJECT
public:
    OcrDialog(QImage preview,QString source,QWidget *parent);
    ~OcrDialog()override;
protected:
    void reject()override;
private:
    QImage m_image;OcrJob *m_job=nullptr;bool m_closing=false;
    QComboBox*m_language;QPlainTextEdit*m_text;QLabel*m_status;QPushButton*m_start,*m_cancel,*m_copy;
    void start(bool languages);
};
