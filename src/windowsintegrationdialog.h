#pragma once
#include "windowsintegration.h"
#include <QDialog>
class QLineEdit;class QLabel;class QPushButton;
class WindowsIntegrationDialog final:public QDialog {
    Q_OBJECT
public:
    WindowsIntegrationDialog(FolderRegistry registry,QString executable,QString extension,QWidget *parent=nullptr);
private:
    FolderRegistry m_registry;QString m_executable;FolderIntegrationSnapshot m_snapshot;
    QLineEdit *m_extension;QLabel *m_association,*m_current,*m_previous,*m_status;QPushButton *m_enable,*m_restore;
    void refreshAssociation();void refreshFolders();void change(bool enable);
};
