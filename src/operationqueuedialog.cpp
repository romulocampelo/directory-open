#include "operationqueuedialog.h"
#include "operationdialog.h"
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScopeGuard>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
namespace {
QString state(OperationState value){switch(value){case OperationState::Completed:return "Concluída";case OperationState::Cancelled:return "Cancelada";case OperationState::Failed:return "Erro";case OperationState::Incomplete:return "Parcial";case OperationState::Delegated:return "Encaminhada — conferir ferramenta";}return {};}
qsizetype bytes(const OperationRequest &request,const OperationResult &result){qsizetype sum=request.destination.size()*2;for(const auto &s:request.sources)sum+=s.size()*2;sum+=result.error.size()*2+result.externalRecord.size()*2;for(const auto &i:result.items)sum+=(i.source.size()+i.target.size()+i.error.size()+i.recycledParsingName.size())*2;return sum;}
}
OperationQueueDialog::OperationQueueDialog(bool nativeUi,std::function<bool()> canRun,QWidget *parent):QDialog(parent),m_nativeUi(nativeUi),m_canRun(std::move(canRun)){
    setObjectName("operationQueueDialog");setWindowTitle(tr("Fila de transferências"));setAttribute(Qt::WA_DeleteOnClose);setModal(false);resize(1040,800);auto *layout=new QVBoxLayout(this);auto *hint=new QLabel(tr("Prepare cada cópia ou movimentação e confirme sua inclusão. A fila começa somente em Iniciar/Continuar e executa uma tarefa por vez. Arquivos e destino são revalidados antes de iniciar; pastas são percorridas com o conteúdo existente nesse momento. Erro, cancelamento ou encaminhamento ao TeraCopy pausa as próximas tarefas. A fila fica somente nesta execução, sem retomada automática ao reabrir."),this);hint->setWordWrap(true);layout->addWidget(hint);
    auto *form=new QFormLayout;layout->addLayout(form);m_sourcesText=new QPlainTextEdit(this);m_sourcesText->setObjectName("queueSources");m_sourcesText->setReadOnly(true);m_sourcesText->setMaximumHeight(75);form->addRow(tr("Origens preparadas:"),m_sourcesText);auto *choose=new QHBoxLayout;auto *files=new QPushButton(tr("Escolher arquivos…"),this),*folder=new QPushButton(tr("Acrescentar pasta…"),this);choose->addWidget(files);choose->addWidget(folder);choose->addStretch();form->addRow(choose);
    connect(files,&QPushButton::clicked,this,[this]{const auto paths=QFileDialog::getOpenFileNames(this,tr("Origens da próxima tarefa"));if(!paths.isEmpty())setDraft(paths,m_destination->text(),m_preferredEngine);});connect(folder,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getExistingDirectory(this,tr("Acrescentar origem"));if(!path.isEmpty()){auto paths=m_sources;paths.append(path);setDraft(paths,m_destination->text(),m_preferredEngine);}});
    auto *destination=new QHBoxLayout;m_destination=new QLineEdit(this);m_destination->setObjectName("queueDestination");m_destination->setMaxLength(32767);auto *browse=new QPushButton(tr("Escolher…"),this);destination->addWidget(m_destination);destination->addWidget(browse);form->addRow(tr("Destino:"),destination);connect(browse,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getExistingDirectory(this,tr("Destino existente"),m_destination->text());if(!path.isEmpty())m_destination->setText(path);});
    auto *addRow=new QHBoxLayout;m_kind=new QComboBox(this);m_kind->setObjectName("queueKind");m_kind->addItems({tr("Copiar"),tr("Mover")});m_add=new QPushButton(tr("Revisar e adicionar à fila…"),this);m_add->setObjectName("queueAdd");addRow->addWidget(m_kind);addRow->addWidget(m_add);addRow->addStretch();layout->addLayout(addRow);connect(m_add,&QPushButton::clicked,this,&OperationQueueDialog::addDraft);
    m_waiting=new QListWidget(this);m_waiting->setObjectName("queuePending");layout->addWidget(m_waiting,1);auto *order=new QHBoxLayout;layout->addLayout(order);auto button=[&](const char *id,const QString &text,const auto &callback){auto *b=new QPushButton(text,this);b->setObjectName(id);b->setAutoDefault(false);order->addWidget(b);connect(b,&QPushButton::clicked,this,callback);return b;};
    button("queueUp",tr("Subir"),[this]{const int row=m_waiting->currentRow();if(row>0){m_pending.move(row,row-1);refresh();m_waiting->setCurrentRow(row-1);}});button("queueDown",tr("Descer"),[this]{const int row=m_waiting->currentRow();if(row>=0&&row+1<m_pending.size()){m_pending.move(row,row+1);refresh();m_waiting->setCurrentRow(row+1);}});button("queueRemove",tr("Remover pendente"),[this]{const int row=m_waiting->currentRow();if(row<0)return;m_pendingBytes-=m_pending[row].approval.retainedBytes;m_pending.removeAt(row);refresh();});order->addStretch();
    m_activeText=new QLabel(tr("Nenhuma transferência em execução."),this);m_activeText->setObjectName("queueActive");m_activeText->setTextFormat(Qt::PlainText);m_activeText->setWordWrap(true);layout->addWidget(m_activeText);m_progress=new QProgressBar(this);layout->addWidget(m_progress);m_results=new QTreeWidget(this);m_results->setObjectName("queueResults");m_results->setColumnCount(3);m_results->setHeaderLabels({tr("Tarefa"),tr("Resultado"),tr("Destino")});m_results->setRootIsDecorated(false);m_results->header()->setSectionResizeMode(2,QHeaderView::Stretch);layout->addWidget(m_results,1);m_details=new QPlainTextEdit(this);m_details->setObjectName("queueDetails");m_details->setReadOnly(true);m_details->setMaximumHeight(100);layout->addWidget(m_details);connect(m_results,&QTreeWidget::currentItemChanged,this,&OperationQueueDialog::showDetails);
    m_status=new QLabel(tr("Fila pausada. Adicione tarefas e inicie quando estiver pronto."),this);m_status->setObjectName("queueStatus");m_status->setWordWrap(true);m_status->setTextFormat(Qt::PlainText);layout->addWidget(m_status);auto *bottom=new QHBoxLayout;layout->addLayout(bottom);m_start=new QPushButton(tr("Iniciar / continuar fila"),this);m_start->setObjectName("queueStart");m_pause=new QPushButton(tr("Pausar próximas"),this);m_pause->setObjectName("queuePause");m_cancel=new QPushButton(tr("Cancelar atual"),this);m_cancel->setObjectName("queueCancel");for(auto *b:{m_start,m_pause,m_cancel}){b->setAutoDefault(false);bottom->addWidget(b);}bottom->addStretch();auto *close=new QPushButton(tr("Fechar"),this);close->setObjectName("queueClose");bottom->addWidget(close);connect(close,&QPushButton::clicked,this,[this]{requestShutdown();});connect(m_start,&QPushButton::clicked,this,[this]{m_paused=false;startNext();});connect(m_pause,&QPushButton::clicked,this,[this]{m_paused=true;m_status->setText(tr("Próximas tarefas pausadas; a transferência atual continua."));refresh();});connect(m_cancel,&QPushButton::clicked,this,[this]{if(m_job){m_paused=true;m_job->cancel();m_cancel->setEnabled(false);m_status->setText(tr("Aguardando o encerramento da tarefa atual; itens já copiados/movidos são preservados."));}});refresh();
}
OperationQueueDialog::~OperationQueueDialog(){if(m_job){m_job->cancel();m_job->wait();}}
void OperationQueueDialog::setDraft(const QStringList &sources,const QString &destination,TransferEngine engine){
    if(m_confirming||m_closing)return;qsizetype units=0;for(const auto &p:sources){units+=p.size();if(units>1024*1024)break;}if(sources.size()>256||units>1024*1024){m_status->setText(tr("Prepare até 256 origens / 2 MiB de caminhos por tarefa."));return;}m_sources=sources;m_destination->setText(destination);m_preferredEngine=engine;m_sourcesText->setPlainText(m_sources.join('\n'));refresh();
}
void OperationQueueDialog::addDraft(){
    if(m_confirming||m_closing||m_pending.size()>=32||m_sources.isEmpty())return;
    const auto resume=qScopeGuard([this]{QTimer::singleShot(0,this,&OperationQueueDialog::startNext);});
    OperationRequest request;request.kind=m_kind->currentIndex()==0?OperationKind::Copy:OperationKind::Move;request.sources=m_sources;request.destination=QDir::fromNativeSeparators(m_destination->text().trimmed());request.nativeUi=m_nativeUi;request.ownerWindow=m_nativeUi?quintptr(winId()):0;if(request.kind==OperationKind::Copy)request.engine=m_preferredEngine;
    {QScopedValueRollback<bool> guard(m_confirming,true);OperationDialog dialog(request,this);dialog.setWindowTitle(tr("Revisar inclusão na fila"));dialog.findChild<QPushButton*>("confirmOperation")->setText(tr("Adicionar à fila"));if(dialog.exec()!=QDialog::Accepted)return;request=dialog.request();}
    auto approval=approveTransfer(request);if(!approval.error.isEmpty()){m_status->setText(approval.error);return;}if(approval.retainedBytes>8*1024*1024-m_pendingBytes){m_status->setText(tr("A fila atingiu o orçamento de 8 MiB de caminhos/identidades. Remova ou conclua tarefas."));return;}
    m_pendingBytes+=approval.retainedBytes;m_pending.append({m_nextId++,std::move(approval)});m_status->setText(tr("Tarefa preparada. %1 aguardando; %2.").arg(m_pending.size()).arg(m_paused?tr("fila pausada"):tr("execução sequencial")));refresh();if(!m_paused)startNext();
}
void OperationQueueDialog::startNext(){
    if(m_job||m_paused||m_confirming||m_closing||m_pending.isEmpty()){refresh();return;}
    if((m_canRun&&!m_canRun())||QApplication::activeModalWidget()){m_paused=true;m_status->setText(tr("Fila pausada: finalize a operação ou diálogo aberto e pressione Continuar."));refresh();return;}
    m_active=m_pending.takeFirst();m_pendingBytes-=m_active->approval.retainedBytes;m_job=new ApprovedTransferJob(m_active->approval,this);auto *job=m_job;const auto id=m_active->id;m_activeText->setText(tr("Tarefa %1 — %2: %3 origens → %4").arg(id).arg(operationLabel(m_active->approval.request.kind)).arg(m_active->approval.request.sources.size()).arg(QDir::toNativeSeparators(m_active->approval.request.destination)));m_progress->setRange(0,0);m_status->setText(tr("Transferência em andamento. Você pode preparar a próxima seleção."));
    connect(job,&ApprovedTransferJob::progress,this,[this](quint32 total,quint32 done,const QString&){if(total){m_progress->setRange(0,100);m_progress->setValue(int(qMin<quint64>(100,quint64(done)*100/total)));}});
    connect(job,&QThread::finished,this,[this,job,id]{job->wait();auto result=job->result();const auto request=m_active->approval.request;m_job=nullptr;job->deleteLater();m_active.reset();const auto retained=bytes(request,result);m_history.append({id,request,std::move(result),retained});m_historyBytes+=retained;while(m_history.size()>32||(m_historyBytes>8*1024*1024&&m_history.size()>1)){m_historyBytes-=m_history.first().bytes;m_history.removeFirst();}const auto outcome=m_history.last().result.state;if(outcome!=OperationState::Completed)m_paused=true;m_activeText->setText(tr("Nenhuma transferência em execução."));m_status->setText(tr("Tarefa %1: %2. %3").arg(id).arg(state(outcome),m_paused?tr("Próximas tarefas pausadas. Confira o resultado antes de continuar."):tr("Próximas tarefas seguem em sequência.")));refresh();emit busyChanged();if(m_closing){QDialog::reject();return;}QTimer::singleShot(0,this,&OperationQueueDialog::startNext);});
    refresh();emit busyChanged();job->start();
}
void OperationQueueDialog::refresh(){
    const int selected=m_waiting->currentRow();m_waiting->clear();for(const auto &p:m_pending)m_waiting->addItem(tr("%1 — %2: %3 origens → %4").arg(p.id).arg(operationLabel(p.approval.request.kind)).arg(p.approval.request.sources.size()).arg(QDir::toNativeSeparators(p.approval.request.destination)));m_waiting->setCurrentRow(qMin(selected,int(m_pending.size())-1));
    const auto selectedId=m_results->currentItem()?m_results->currentItem()->data(0,Qt::UserRole).toULongLong():0;m_results->clear();QTreeWidgetItem *current=nullptr;for(const auto &r:m_history){auto *row=new QTreeWidgetItem(m_results,{QString::number(r.id)+" — "+operationLabel(r.request.kind),state(r.result.state),QDir::toNativeSeparators(r.request.destination)});row->setData(0,Qt::UserRole,r.id);if(r.id==selectedId)current=row;}if(!current&&m_results->topLevelItemCount())current=m_results->topLevelItem(m_results->topLevelItemCount()-1);m_results->setCurrentItem(current);showDetails();
    m_add->setEnabled(!m_confirming&&!m_closing&&!m_sources.isEmpty()&&m_pending.size()<32);m_start->setEnabled(!m_closing&&!m_pending.isEmpty()&&(!m_job||m_paused));m_pause->setEnabled(!m_paused&&!m_closing&&(m_job||!m_pending.isEmpty()));m_cancel->setEnabled(m_job&&!m_closing);m_progress->setVisible(m_job!=nullptr);
}
void OperationQueueDialog::showDetails(){
    const auto *row=m_results->currentItem();if(!row){m_details->clear();return;}const auto id=row->data(0,Qt::UserRole).toULongLong();for(const auto &record:m_history)if(record.id==id){QString text=record.result.error;if(!record.result.externalRecord.isEmpty())text+="\nRegistro de encaminhamento (não comprova conclusão): "+record.result.externalRecord;for(const auto &i:record.result.items){const auto line="\n"+operationItemLabel(i.state)+": "+i.source+(i.target.isEmpty()?QString{}:" → "+i.target)+(i.error.isEmpty()?QString{}:"\n"+i.error);if(text.size()+line.size()>512*1024){text+="\nDetalhes da exibição limitados a 512 mil caracteres.";break;}text+=line;}m_details->setPlainText(text);return;}
}
bool OperationQueueDialog::requestShutdown(){
    const auto resume=qScopeGuard([this]{if(!m_closing)QTimer::singleShot(0,this,&OperationQueueDialog::startNext);});
    if(m_confirming)return false;if(!m_shutdownApproved&&(m_job||!m_pending.isEmpty())){QScopedValueRollback<bool> guard(m_confirming,true);if(QMessageBox::question(this,tr("Fechar fila"),tr("Descartar tarefas pendentes e cancelar a atual, se houver? Itens já transferidos são preservados. O fechamento aguardará o encerramento."),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return false;m_shutdownApproved=true;}
    m_pending.clear();m_pendingBytes=0;m_paused=true;if(m_job){m_closing=true;m_job->cancel();m_status->setText(tr("Aguardando a tarefa encerrar antes de fechar…"));refresh();return true;}QDialog::reject();return true;
}
void OperationQueueDialog::reject(){requestShutdown();}
void OperationQueueDialog::closeEvent(QCloseEvent *event){if(requestShutdown()&&!m_job)event->accept();else event->ignore();}
