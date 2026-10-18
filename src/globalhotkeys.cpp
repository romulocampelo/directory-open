#include "globalhotkeys.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <windows.h>

NativeHotkey parseGlobalHotkey(const QString &text){
    NativeHotkey result;const auto sequence=QKeySequence::fromString(text,QKeySequence::PortableText);
    if(sequence.count()!=1||sequence.isEmpty()||sequence.toString(QKeySequence::PortableText)!=text){result.error=QObject::tr("Use uma combinação simples.");return result;}
    const auto combination=sequence[0];const auto modifiers=combination.keyboardModifiers();
    if(modifiers.testFlag(Qt::MetaModifier)||modifiers.testFlag(Qt::KeypadModifier)){result.error=QObject::tr("Combinações do Windows e do teclado numérico não são admitidas aqui.");return result;}
    int count=0;for(const auto m:{Qt::ControlModifier,Qt::AltModifier,Qt::ShiftModifier})if(modifiers.testFlag(m))++count;
    if(count<2){result.error=QObject::tr("Combine pelo menos duas teclas entre Ctrl, Alt e Shift.");return result;}
    const auto key=combination.key();
    if(key>=Qt::Key_A&&key<=Qt::Key_Z)result.key='A'+key-Qt::Key_A;
    else if(key>=Qt::Key_0&&key<=Qt::Key_9)result.key='0'+key-Qt::Key_0;
    else if(key>=Qt::Key_F1&&key<=Qt::Key_F11)result.key=VK_F1+key-Qt::Key_F1;
    else {result.error=QObject::tr("Use letras A–Z, números ou F1–F11; F12 é reservado pelo Windows.");return result;}
    if(modifiers.testFlag(Qt::ControlModifier))result.modifiers|=MOD_CONTROL;if(modifiers.testFlag(Qt::AltModifier))result.modifiers|=MOD_ALT;if(modifiers.testFlag(Qt::ShiftModifier))result.modifiers|=MOD_SHIFT;return result;
}
GlobalHotkeys::GlobalHotkeys(QWidget *owner):QWidget(owner,Qt::Tool){setObjectName("globalHotkeyReceiver");setAttribute(Qt::WA_DontShowOnScreen);}
GlobalHotkeys::~GlobalHotkeys(){deactivate();/* Destroying this owned HWND releases remaining registrations. */}
QMap<QString,QString> GlobalHotkeys::bindings()const{QMap<QString,QString> result;for(const auto &r:m_registrations)if(r.active)result[r.command]=r.sequence;return result;}
int GlobalHotkeys::cleanupPending()const{int count=0;for(const auto &r:m_registrations)if(!r.active)++count;return count;}
QString GlobalHotkeys::cleanupInactive(){
    QString error;for(auto i=m_registrations.begin();i!=m_registrations.end();){if(i->active){++i;continue;}if(UnregisterHotKey(reinterpret_cast<HWND>(winId()),i->id))i=m_registrations.erase(i);else{error=tr("Há um registro aguardando liberação (erro %1). A tecla não executará comandos; tente desativar novamente.").arg(GetLastError());++i;}}return error;
}
QString GlobalHotkeys::deactivate(){for(auto &r:m_registrations)r.active=false;return cleanupInactive();}
QString GlobalHotkeys::activate(const QMap<QString,QString> &bindings){
    if(QGuiApplication::platformName()!="windows")return tr("Atalhos globais exigem a interface nativa do Windows.");
    if(bindings.size()>32)return tr("Use até 32 atalhos globais.");
    QMap<QString,NativeHotkey> parsed;QSet<quint64> combinations;
    for(auto i=bindings.begin();i!=bindings.end();++i){const auto key=parseGlobalHotkey(i.value());if(!key.error.isEmpty())return key.error;const auto identity=(quint64(key.modifiers)<<32)|key.key;if(combinations.contains(identity))return tr("Há combinações globais repetidas.");combinations.insert(identity);parsed[i.key()]=key;}
    const auto cleanup=cleanupInactive();if(!cleanup.isEmpty())return cleanup;
    QVector<Registration> additions;
    for(auto i=parsed.begin();i!=parsed.end();++i){bool exists=false;for(const auto &r:m_registrations)if(r.modifiers==i->modifiers&&r.key==i->key){exists=true;break;}if(exists)continue;
        if(m_nextId>0xbfff){for(const auto &r:additions)m_registrations.append(r);cleanupInactive();return tr("Limite de registros desta execução atingido. Feche e abra esta janela.");}
        Registration registration{m_nextId++,i->modifiers,i->key,i.key(),bindings[i.key()],false};
        if(!RegisterHotKey(reinterpret_cast<HWND>(winId()),registration.id,registration.modifiers|MOD_NOREPEAT,registration.key)){const auto code=GetLastError();for(const auto &r:additions)m_registrations.append(r);const auto pending=cleanupInactive();return tr("A combinação %1 já está em uso ou indisponível (erro %2). Os atalhos ativos anteriores foram mantidos. %3").arg(bindings[i.key()]).arg(code).arg(pending);}
        additions.append(registration);
    }
    m_registrations+=additions;
    for(auto &r:m_registrations){r.active=false;for(auto i=parsed.begin();i!=parsed.end();++i)if(r.modifiers==i->modifiers&&r.key==i->key){r.active=true;r.command=i.key();r.sequence=bindings[i.key()];break;}}
    return cleanupInactive();
}
bool GlobalHotkeys::nativeEvent(const QByteArray &type,void *message,qintptr *result){
    const auto *event=static_cast<MSG*>(message);if(event->message!=WM_HOTKEY)return QWidget::nativeEvent(type,message,result);
    for(const auto &r:m_registrations)if(r.active&&r.id==int(event->wParam)&&r.modifiers==LOWORD(event->lParam)&&r.key==HIWORD(event->lParam)){
        const auto id=r.id;QTimer::singleShot(0,this,[this,id]{if(qApp->activeModalWidget()||qApp->activePopupWidget())return;for(const auto &current:m_registrations)if(current.id==id&&current.active){emit commandRequested(current.command);return;}});break;
    }*result=0;return true;
}
GlobalKeysDialog::GlobalKeysDialog(QMap<QString,QString>definitions,const QVector<UiCommand>&commands,GlobalHotkeys&runtime,QWidget*parent):QDialog(parent),m_definitions(std::move(definitions)){
    setObjectName("globalKeysDialog");setWindowTitle(tr("Atalhos globais"));resize(860,650);auto *root=new QVBoxLayout(this);auto *hint=new QLabel(tr("Atuam enquanto esta janela estiver aberta, inclusive com outro programa em primeiro plano. Não são ativados ao importar perfil ou reiniciar. Use duas ou três teclas entre Ctrl/Alt/Shift com letra, número ou F1–F11. Combinações em uso serão recusadas."),this);hint->setWordWrap(true);root->addWidget(hint);
    auto *search=new QLineEdit(this);search->setPlaceholderText(tr("Filtrar comandos…"));root->addWidget(search);auto *table=new QTableWidget(this);table->setObjectName("globalKeysTable");table->setColumnCount(2);table->setHorizontalHeaderLabels({tr("Comando"),tr("Combinação global")});table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);root->addWidget(table,1);
    for(const auto &c:commands)if(!c.toolbarOnly){int r=table->rowCount();table->insertRow(r);auto *item=new QTableWidgetItem(c.category+" — "+commandLabel(c));item->setData(Qt::UserRole,c.id);item->setFlags(item->flags()&~Qt::ItemIsEditable);table->setItem(r,0,item);auto *edit=new QKeySequenceEdit(QKeySequence::fromString(m_definitions.value(c.id),QKeySequence::PortableText),table);edit->setMaximumSequenceLength(1);edit->setClearButtonEnabled(true);table->setCellWidget(r,1,edit);}
    connect(search,&QLineEdit::textChanged,this,[table](const QString &text){for(int r=0;r<table->rowCount();++r)table->setRowHidden(r,!table->item(r,0)->text().contains(text,Qt::CaseInsensitive));});
    auto *status=new QLabel(tr("Ativos nesta janela: %1").arg(runtime.bindings().size()),this);status->setObjectName("globalKeysStatus");status->setWordWrap(true);root->addWidget(status);
    auto *buttons=new QDialogButtonBox(this);root->addWidget(buttons);auto *activate=buttons->addButton(tr("Guardar e ativar nesta execução"),QDialogButtonBox::ActionRole),*save=buttons->addButton(tr("Guardar sem ativar"),QDialogButtonBox::ActionRole),*disable=buttons->addButton(tr("Desativar agora"),QDialogButtonBox::ActionRole);activate->setObjectName("activateGlobalKeys");save->setObjectName("saveGlobalKeys");disable->setObjectName("disableGlobalKeys");buttons->addButton(tr("Cancelar"),QDialogButtonBox::RejectRole);connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    auto collect=[table,status]()->std::optional<QMap<QString,QString>>{QMap<QString,QString> result;QSet<QString>used;for(int r=0;r<table->rowCount();++r){const auto key=static_cast<QKeySequenceEdit*>(table->cellWidget(r,1))->keySequence().toString(QKeySequence::PortableText);if(key.isEmpty())continue;const auto parsed=parseGlobalHotkey(key);if(!parsed.error.isEmpty()){status->setText(parsed.error);return {};}if(used.contains(key)||result.size()>=32){status->setText(QObject::tr("Combinação repetida ou limite de 32 atalhos excedido."));return {};}used.insert(key);result[table->item(r,0)->data(Qt::UserRole).toString()]=key;}return result;};
    connect(disable,&QPushButton::clicked,this,[&runtime,status]{const auto error=runtime.deactivate();status->setText(error.isEmpty()?QObject::tr("Atalhos desativados nesta execução."):error);});
    connect(activate,&QPushButton::clicked,this,[this,&runtime,collect,status]{const auto bindings=collect();if(!bindings)return;const auto error=runtime.activate(*bindings);if(!error.isEmpty()){status->setText(error);return;}m_definitions=*bindings;accept();});
    connect(save,&QPushButton::clicked,this,[this,&runtime,collect,status]{const auto bindings=collect();if(!bindings)return;const auto error=runtime.deactivate();if(!error.isEmpty()){status->setText(error);return;}m_definitions=*bindings;accept();});
}
