#include "specializedlaunchdialog.h"
#include "specializedlaunch.h"
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
namespace {
QLineEdit *field(QFormLayout *form,const QString &label,const QString &text,const char *name,QWidget *parent,int kind){
    auto *row=new QWidget(parent);auto *layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);auto *edit=new QLineEdit(text,row);edit->setObjectName(name);edit->setMaxLength(32767);auto *choose=new QPushButton("Escolher…",row);layout->addWidget(edit);layout->addWidget(choose);form->addRow(label,row);
    QObject::connect(choose,&QPushButton::clicked,parent,[=]{QString selected;if(kind==0)selected=QFileDialog::getExistingDirectory(parent,"Pasta do programa",edit->text());else if(kind==1)selected=QFileDialog::getOpenFileName(parent,"Executável",edit->text(),"Programas (*.exe)");else selected=QFileDialog::getSaveFileName(parent,"Configuração rclone",edit->text(),"Configuração (*.conf)",nullptr,QFileDialog::DontConfirmOverwrite);if(!selected.isEmpty())edit->setText(selected);});return edit;
}
QLabel *label(QVBoxLayout *layout,const QString &text,QWidget *parent,const char *name=nullptr){auto *value=new QLabel(text,parent);if(name)value->setObjectName(name);value->setWordWrap(true);value->setTextFormat(Qt::PlainText);value->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(value);return value;}
}
DuplicateFinderDialog::DuplicateFinderDialog(QString project,QStringList locations,QWidget *parent):QDialog(parent){
    setObjectName("duplicateFinderDialog");setWindowTitle(tr("Duplicador especializado"));resize(800,520);auto *layout=new QVBoxLayout(this);
    label(layout,tr("Abre o duplicador Python já instalado. Escolha as pastas e revise os resultados na janela dele. O DirOpen não inicia buscas nem remove arquivos ao abrir a ferramenta."),this);
    QStringList bounded;int characters=0;for(const auto &location:locations){if(bounded.size()>=256||location.size()>65536-characters)break;bounded.append(location);characters+=location.size()+1;}if(bounded.size()!=locations.size())label(layout,tr("A referência mostra somente os primeiros locais que cabem em 256 itens / 64 mil caracteres."),this);locations=std::move(bounded);
    if(project.isEmpty()&&QFileInfo("C:/Dev/2026_Find_Dup_Files/.venv/Scripts/pythonw.exe").isFile())project="C:/Dev/2026_Find_Dup_Files";
    auto *form=new QFormLayout;layout->addLayout(form);m_project=field(form,tr("Pasta do duplicador:"),project,"duplicateFinderProject",this,0);
    auto *paths=new QPlainTextEdit(locations.join('\n'),this);paths->setObjectName("duplicateLocations");paths->setReadOnly(true);layout->addWidget(paths,1);auto *copy=new QPushButton(tr("Copiar locais sugeridos"),this);copy->setObjectName("copyDuplicateLocations");copy->setEnabled(!locations.isEmpty());layout->addWidget(copy);connect(copy,&QPushButton::clicked,this,[locations]{qApp->clipboard()->setText(locations.join('\n'));});
    label(layout,tr("Os locais acima servem como referência. Esta versão do duplicador não recebe seleção pela linha de comando; adicione-os na interface dele."),this);auto *status=label(layout,tr("Pronto. Nenhuma ferramenta aberta."),this,"duplicateLaunchStatus");auto *buttons=new QHBoxLayout;layout->addLayout(buttons);auto *open=new QPushButton(tr("Abrir duplicador…"),this);open->setObjectName("openDuplicateFinder");buttons->addWidget(open);buttons->addStretch();auto *close=new QPushButton(tr("Fechar"),this);buttons->addWidget(close);connect(close,&QPushButton::clicked,this,&QDialog::reject);
    connect(open,&QPushButton::clicked,this,[this,status]{const auto command=duplicateFinderCommand(this->project());if(!command.error.isEmpty()){status->setText(command.error);return;}if(QMessageBox::question(this,tr("Abrir ferramenta"),tr("Abrir o duplicador instalado em:\n%1\n\nAs operações serão conduzidas na janela externa.").arg(command.directory),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;status->setText(delegateCommand(duplicateFinderCommand(this->project())).message);});
}
QString DuplicateFinderDialog::project()const{return QDir::fromNativeSeparators(m_project->text().trimmed());}
RemoteSetupDialog::RemoteSetupDialog(QString program,QString config,QWidget *parent):QDialog(parent){
    setObjectName("remoteSetupDialog");setWindowTitle(tr("Configurar contas remotas — rclone"));resize(820,480);auto *layout=new QVBoxLayout(this);
    label(layout,tr("1. Escolha o programa e o arquivo de configuração.\n2. Abra o assistente interativo do rclone e siga as instruções na janela de console. O serviço pode abrir o navegador para autorizar o acesso.\n3. Ao terminar, feche o assistente e volte para listar as conexões."),this);
    auto *form=new QFormLayout;layout->addLayout(form);m_program=field(form,tr("Programa rclone:"),program,"setupRcloneProgram",this,1);m_config=field(form,tr("Configuração:"),config,"setupRcloneConfig",this,2);
    label(layout,tr("O rclone guarda credenciais no arquivo escolhido e pode atualizá-lo. Se já existir, o DirOpen faz uma cópia de segurança ao lado dele antes de abrir o assistente. A cópia também pode conter credenciais: proteja-a como o original. Senhas e tokens são informados diretamente ao rclone/navegador; não entram na sessão do DirOpen."),this);auto *status=label(layout,tr("Nenhuma conta alterada pelo DirOpen."),this,"remoteSetupStatus");layout->addStretch();auto *buttons=new QHBoxLayout;layout->addLayout(buttons);auto *open=new QPushButton(tr("Abrir assistente rclone…"),this);open->setObjectName("launchRemoteSetup");buttons->addWidget(open);buttons->addStretch();auto *close=new QPushButton(tr("Voltar às conexões"),this);buttons->addWidget(close);connect(close,&QPushButton::clicked,this,&QDialog::accept);
    connect(open,&QPushButton::clicked,this,[this,status]{const auto command=remoteSetupCommand(this->program(),this->config());if(!command.error.isEmpty()){status->setText(command.error);return;}if(QMessageBox::question(this,tr("Configurar contas"),tr("Abrir rclone:\n%1\n\nEle poderá criar/alterar contas e credenciais em:\n%2\n\nUm arquivo existente será copiado antes da abertura. Continue na janela do rclone.").arg(command.program,this->config()),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;const auto backup=backupRemoteConfig(this->config());if(backup.state==ConfigBackupState::Failed){status->setText(backup.error);return;}const auto dispatched=delegateCommand(remoteSetupCommand(this->program(),this->config()));status->setText(dispatched.message+(backup.state==ConfigBackupState::BackedUp?"\nBackup: "+backup.path:QString{}));});
}
QString RemoteSetupDialog::program()const{return QDir::fromNativeSeparators(m_program->text().trimmed());}
QString RemoteSetupDialog::config()const{return QDir::fromNativeSeparators(m_config->text().trimmed());}
