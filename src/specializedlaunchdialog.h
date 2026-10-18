#pragma once
#include <QDialog>
class QLineEdit;
class DuplicateFinderDialog final:public QDialog {
    Q_OBJECT
public:
    DuplicateFinderDialog(QString project,QStringList locations,QWidget *parent=nullptr);
    QString project()const;
private:
    QLineEdit *m_project;
};
class RemoteSetupDialog final:public QDialog {
    Q_OBJECT
public:
    RemoteSetupDialog(QString program,QString config,QWidget *parent=nullptr);
    QString program()const;QString config()const;
private:
    QLineEdit *m_program,*m_config;
};
