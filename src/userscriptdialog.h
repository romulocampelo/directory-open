#pragma once
#include "userscript.h"
#include <QDialog>
#include <QMap>
class QLineEdit;class QScrollArea;class QPlainTextEdit;class QCheckBox;class QPushButton;class QLabel;
class UserScriptDialog final:public QDialog {
    Q_OBJECT
public:
    UserScriptDialog(QString interpreter,QString manifest,QString folder,QStringList selection,QWidget *parent=nullptr);
    ~UserScriptDialog()override;
    QString interpreter()const;QString manifest()const;
signals:
    void executionStarting();
protected:
    void reject()override;
private:
    QString m_folder;QStringList m_selection;std::optional<UserScript> m_script;UserScriptJob *m_job=nullptr;bool m_closePending=false;
    QLineEdit *m_interpreter,*m_manifest;QScrollArea *m_form;QPlainTextEdit *m_code,*m_output;QCheckBox *m_permission;QPushButton *m_load,*m_run,*m_cancel;QLabel *m_status,*m_identity;QMap<QString,QWidget*> m_fields;
    QVector<QPushButton*> m_pathChoices;
    void clearScript();void load();void run();void updatePermission();void setBusy(bool);QJsonObject values()const;
};
