#pragma once
#include "uicustomization.h"
#include <QWidget>
#include <QDialog>
#include <QMap>
struct NativeHotkey { unsigned modifiers=0,key=0;QString error; };
NativeHotkey parseGlobalHotkey(const QString &text);
// Owns registrations through its own hidden HWND. Definitions never auto-activate.
class GlobalHotkeys final:public QWidget{
    Q_OBJECT
public:
    explicit GlobalHotkeys(QWidget *owner=nullptr);
    ~GlobalHotkeys()override;
    QString activate(const QMap<QString,QString> &bindings);
    QString deactivate();
    QMap<QString,QString> bindings()const;
    int cleanupPending()const;
signals:
    void commandRequested(const QString &id);
protected:
    bool nativeEvent(const QByteArray &type,void *message,qintptr *result)override;
private:
    struct Registration{int id;unsigned modifiers,key;QString command,sequence;bool active=false;};
    QVector<Registration> m_registrations;
    int m_nextId=1;
    QString cleanupInactive();
};
class GlobalKeysDialog final:public QDialog{
    Q_OBJECT
public:
    GlobalKeysDialog(QMap<QString,QString> definitions,const QVector<UiCommand> &commands,GlobalHotkeys &runtime,QWidget *parent);
    QMap<QString,QString> definitions()const{return m_definitions;}
private:
    QMap<QString,QString>m_definitions;
};
