#pragma once
#include "fileannotations.h"
#include "viewersettings.h"
#include <QDialog>
class QListWidget;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QCheckBox;
class QLabel;
class QPushButton;
class PreviewWidget;
class AnnotationsDialog final:public QDialog{
    Q_OBJECT
public:
    AnnotationsDialog(QString catalogPath,QStringList files,AnnotationCatalog memory,const ViewerSettings &settings,QWidget *parent);
    ~AnnotationsDialog()override;
    AnnotationCatalog memoryCatalog()const{return m_catalog;}
signals:
    void viewerSettingsChanged(const ViewerSettings &settings);
protected:
    void reject()override;
private:
    QString m_path;
    AnnotationCatalog m_catalog;
    ViewerSettings m_viewerSettings;
    QVector<AnnotationRecord>m_records;
    QVector<int>m_editing;
    AnnotationJob *m_job=nullptr;
    bool m_loading=false,m_closePending=false;
    QListWidget *m_files;
    PreviewWidget *m_preview;
    QLineEdit *m_label,*m_status,*m_tags,*m_color;
    QPlainTextEdit *m_description;
    QSpinBox *m_rating;
    QVector<QCheckBox*>m_fields;
    QCheckBox *m_advance;
    QWidget *m_editor;
    QLabel *m_message;
    QPushButton *m_save,*m_apply;
    void loadFields();
    bool stageSelected();
    void refreshRows();
    void save();
    void setBusy(bool busy);
};
