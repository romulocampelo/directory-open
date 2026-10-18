#pragma once
#include "advancedinterface.h"
#include <QDialog>
struct EditorCommand { QString id,label; };
struct EditableMenu { QString id,label;QStringList defaults;QVector<EditorCommand> choices;bool context=false; };
class QListWidget;
class QComboBox;
class QLineEdit;
class QCheckBox;
class QSpinBox;
class QPlainTextEdit;
class QLabel;
class CommandListEditor;
class InterfaceStudio final:public QDialog{
    Q_OBJECT
public:
    InterfaceStudio(AdvancedInterface value,QVector<EditorCommand> commands,QVector<EditableMenu> menus,QWidget *parent);
    AdvancedInterface value()const{return m_value;}
private:
    AdvancedInterface m_value;
    QVector<EditorCommand> m_commands;
    QVector<EditableMenu> m_menus;
    bool m_loading=false;
    QLabel *m_error;
    QComboBox *m_bars,*m_menu,*m_component;
    QLineEdit *m_barName,*m_background,*m_foreground;
    QCheckBox *m_barVisible,*m_barMovable,*m_customMenu,*m_customFont,*m_animations;
    QSpinBox *m_fontSize;
    CommandListEditor *m_barCommands,*m_menuCommands;
    QListWidget *m_launchers;
    QLineEdit *m_launcherName,*m_program;
    QPlainTextEdit *m_arguments;
    void loadBar();
    void storeBar();
    void loadMenu();
    void storeMenu();
    void loadComponent();
    void storeComponent();
    void refreshCommands();
};
