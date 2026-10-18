#include "annotationsdialog.h"
#include "previewwidget.h"
#include "ocrdialog.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
AnnotationsDialog::AnnotationsDialog(QString path,QStringList files,AnnotationCatalog memory,const ViewerSettings &settings,QWidget *parent):QDialog(parent),m_path(std::move(path)),m_catalog(std::move(memory)),m_viewerSettings(settings){
    setObjectName("annotationsDialog");setWindowTitle(tr("Classificação e triagem"));resize(1180,820);auto *root=new QVBoxLayout(this);auto *hint=new QLabel(tr("Etiquetas, estrelas, status, tags e descrições pertencem ao catálogo do DirOpen. Não alteram EXIF, nomes nem conteúdo; marcar Descartar não apaga arquivos. A identidade acompanha renomeações no mesmo volume; cópias recebem identidade nova. Selecione itens, prepare as alterações e depois guarde."),this);hint->setWordWrap(true);root->addWidget(hint);
    auto *split=new QSplitter(this);root->addWidget(split,1);auto *left=new QWidget(split);auto *leftLayout=new QVBoxLayout(left);leftLayout->setContentsMargins(0,0,0,0);m_files=new QListWidget(left);m_files->setObjectName("annotationFiles");m_files->setSelectionMode(QAbstractItemView::ExtendedSelection);leftLayout->addWidget(m_files,1);
    m_editor=new QWidget(left);leftLayout->addWidget(m_editor);auto *editLayout=new QVBoxLayout(m_editor);editLayout->setContentsMargins(0,0,0,0);auto *form=new QFormLayout;editLayout->addLayout(form);
    auto field=[&](const QString&name,QWidget*input){auto *check=new QCheckBox(name,m_editor);check->setObjectName("applyAnnotationField"+QString::number(m_fields.size()));m_fields.append(check);form->addRow(check,input);};
    auto line=[&](const QString &name,int limit){auto *input=new QLineEdit(m_editor);input->setObjectName(name);input->setMaxLength(limit);return input;};m_label=line("annotationLabel",64);m_status=line("annotationStatus",64);m_tags=line("annotationTags",2048);m_tags->setPlaceholderText(tr("Tags separadas por ponto e vírgula"));m_color=line("annotationColor",7);m_color->setPlaceholderText(tr("Sem cor ou #RRGGBB"));m_rating=new QSpinBox(m_editor);m_rating->setObjectName("annotationRating");m_rating->setRange(0,5);m_rating->setSpecialValueText(tr("Sem estrelas"));m_description=new QPlainTextEdit(m_editor);m_description->setObjectName("annotationDescription");m_description->setMaximumHeight(95);
    field(tr("Etiqueta"),m_label);field(tr("Status"),m_status);field(tr("Tags (substituir)"),m_tags);field(tr("Cor"),m_color);field(tr("Estrelas"),m_rating);field(tr("Descrição"),m_description);
    for(int i=0;i<4;++i){auto *input=QList<QLineEdit*>{m_label,m_status,m_tags,m_color}[i];connect(input,&QLineEdit::textChanged,this,[this,i]{if(!m_loading)m_fields[i]->setChecked(true);});}
    connect(m_rating,&QSpinBox::valueChanged,this,[this]{if(!m_loading)m_fields[4]->setChecked(true);});connect(m_description,&QPlainTextEdit::textChanged,this,[this]{if(m_loading)return;if(m_description->toPlainText().size()>4096){const QSignalBlocker block(m_description);m_description->setPlainText(m_description->toPlainText().left(4096));}m_fields[5]->setChecked(true);});
    auto *colorButton=new QPushButton(tr("Escolher cor da etiqueta…"),m_editor);editLayout->addWidget(colorButton);connect(colorButton,&QPushButton::clicked,this,[this]{const auto color=QColorDialog::getColor(QColor(m_color->text()),this,tr("Cor da etiqueta"));if(color.isValid())m_color->setText(color.name());});
    m_apply=new QPushButton(tr("Preparar campos marcados para a seleção"),m_editor);m_apply->setObjectName("stageAnnotations");editLayout->addWidget(m_apply);connect(m_apply,&QPushButton::clicked,this,[this]{stageSelected();});auto *marks=new QHBoxLayout;editLayout->addLayout(marks);
    for(const auto &status:{tr("Manter"),tr("Revisar"),tr("Descartar")}){auto *button=new QPushButton(status,m_editor);button->setObjectName("mark"+QString(status==tr("Manter")?"Keep":status==tr("Revisar")?"Review":"Reject"));marks->addWidget(button);connect(button,&QPushButton::clicked,this,[this,status]{if(!stageSelected())return;const auto selected=m_files->selectedItems();for(auto *item:selected)m_records[item->data(Qt::UserRole).toInt()].value.status=status;refreshRows();const int next=m_files->currentRow()+1;if(m_advance->isChecked()&&selected.size()==1&&next<m_files->count())m_files->setCurrentRow(next,QItemSelectionModel::ClearAndSelect);else loadFields();});}
    m_advance=new QCheckBox(tr("Avançar após marcar um item"),m_editor);m_advance->setChecked(true);editLayout->addWidget(m_advance);
    m_preview=new PreviewWidget(split);m_preview->setSettings(settings);split->addWidget(left);split->addWidget(m_preview);split->setSizes({500,650});
    connect(m_preview,&PreviewWidget::ocrRequested,this,[this]{const auto result=m_preview->result();if(result.image.isNull())return;OcrDialog dialog(result.image,result.path,m_preview->window());dialog.exec();});
    connect(m_preview,&PreviewWidget::settingsRequested,this,[this]{const auto value=editViewerSettings(m_viewerSettings,this);if(value){m_viewerSettings=*value;m_preview->setSettings(*value);emit viewerSettingsChanged(*value);}});
    connect(m_preview,&PreviewWidget::navigateFile,this,[this](int direction){if(!stageSelected())return;int row=m_files->currentRow()+direction;while(row>=0&&row<m_files->count()&&!(m_files->item(row)->flags()&Qt::ItemIsEnabled))row+=direction;if(row>=0&&row<m_files->count())m_files->setCurrentRow(row,QItemSelectionModel::ClearAndSelect);});
    connect(m_preview,&PreviewWidget::enlargeRequested,this,[this,split](bool full){const auto sizes=split->sizes();QDialog dialog(this);dialog.setWindowTitle(tr("Visualizador — triagem"));auto *layout=new QVBoxLayout(&dialog);layout->addWidget(m_preview);dialog.resize(1100,820);if(full)dialog.showFullScreen();dialog.exec();split->insertWidget(1,m_preview);split->setSizes(sizes);m_preview->show();});
    m_message=new QLabel(tr("Lendo classificações…"),this);m_message->setObjectName("annotationMessage");m_message->setWordWrap(true);m_message->setTextFormat(Qt::PlainText);root->addWidget(m_message);if(m_path.isEmpty()){auto *temporary=new QLabel(tr("Modo sem sessão: classificações temporárias, mantidas somente enquanto esta janela do DirOpen existir."),this);temporary->setWordWrap(true);root->addWidget(temporary);}
    auto *buttons=new QDialogButtonBox(this);root->addWidget(buttons);m_save=buttons->addButton(tr("Guardar classificações preparadas"),QDialogButtonBox::ActionRole);m_save->setObjectName("saveAnnotations");buttons->addButton(tr("Cancelar / fechar"),QDialogButtonBox::RejectRole);connect(buttons,&QDialogButtonBox::rejected,this,&AnnotationsDialog::reject);connect(m_save,&QPushButton::clicked,this,&AnnotationsDialog::save);
    connect(m_files,&QListWidget::itemSelectionChanged,this,&AnnotationsDialog::loadFields);connect(m_files,&QListWidget::currentItemChanged,this,[this](QListWidgetItem*item){if(item)m_preview->setPath(m_records[item->data(Qt::UserRole).toInt()].path);});
    setBusy(true);m_job=new AnnotationJob(m_path,std::move(files),m_catalog,this);connect(m_job,&QThread::finished,this,[this]{m_job->wait();auto result=m_job->takeResult();m_job->deleteLater();m_job=nullptr;setBusy(false);if(m_closePending){QDialog::reject();return;}if(!result.error.isEmpty()||result.cancelled){m_message->setText(result.cancelled?tr("Leitura cancelada."):result.error);m_save->setEnabled(false);m_editor->setEnabled(false);return;}m_catalog=std::move(result.catalog);m_records=std::move(result.records);for(int i=0;i<m_records.size();++i){auto *item=new QListWidgetItem(m_files);item->setData(Qt::UserRole,i);if(!m_records[i].error.isEmpty())item->setFlags(item->flags()&~Qt::ItemIsEnabled);}refreshRows();if(m_files->count())m_files->setCurrentRow(0);m_message->setText(tr("A marcação fica preparada até Guardar. Campos desmarcados são preservados."));});m_job->start();
}
AnnotationsDialog::~AnnotationsDialog(){if(m_job){m_job->cancel();m_job->wait();}}
void AnnotationsDialog::reject(){if(m_job){m_closePending=true;m_job->cancel();m_message->setText(tr("Aguardando o encerramento…"));return;}QDialog::reject();}
void AnnotationsDialog::setBusy(bool busy){m_files->setEnabled(!busy);m_editor->setEnabled(!busy);m_save->setEnabled(!busy);}
void AnnotationsDialog::refreshRows(){for(int i=0;i<m_records.size();++i){const auto &r=m_records[i];auto *item=m_files->item(i);const bool dirty=encodeAnnotation(r.before)!=encodeAnnotation(r.value);item->setText(QString("%1%2  %3  %4  %5").arg(dirty?"● ":"",QFileInfo(r.path).fileName(),QString(r.value.rating,QChar(0x2605)),r.value.label,r.value.status));item->setToolTip(r.path+"\n"+(r.error.isEmpty()?r.value.tags.join("; ")+"\n"+r.value.description:r.error));if(!r.value.color.isEmpty())item->setForeground(QColor(r.value.color));else item->setForeground(QBrush{});}}
void AnnotationsDialog::loadFields(){
    if(m_loading)return;QScopedValueRollback<bool>guard(m_loading,true);
    if(!m_editing.isEmpty()&&!stageSelected()){const QSignalBlocker block(m_files);m_files->clearSelection();for(int index:m_editing)m_files->item(index)->setSelected(true);if(!m_editing.isEmpty()){m_files->setCurrentRow(m_editing.first(),QItemSelectionModel::NoUpdate);m_preview->setPath(m_records[m_editing.first()].path);}return;}
    const auto selected=m_files->selectedItems();m_editing.clear();for(auto *item:selected)m_editing.append(item->data(Qt::UserRole).toInt());m_editor->setEnabled(!m_job&&!selected.isEmpty());for(auto *field:m_fields)field->setChecked(false);if(selected.isEmpty())return;const auto &note=m_records[selected.first()->data(Qt::UserRole).toInt()].value;
    m_label->setText(note.label);m_status->setText(note.status);m_tags->setText(note.tags.join("; "));m_color->setText(note.color);m_rating->setValue(note.rating);m_description->setPlainText(note.description);
    for(auto *item:selected){const auto &other=m_records[item->data(Qt::UserRole).toInt()].value;if(other.label!=note.label)m_label->clear();if(other.status!=note.status)m_status->clear();if(other.tags!=note.tags)m_tags->clear();if(other.color!=note.color)m_color->clear();if(other.rating!=note.rating)m_rating->setValue(0);if(other.description!=note.description)m_description->clear();}
}
bool AnnotationsDialog::stageSelected(){
    if(m_editing.isEmpty())return true;QVector<QPair<int,FileAnnotation>>staged;
    for(int index:m_editing){if(!m_records[index].error.isEmpty())continue;auto v=m_records[index].value;
        if(m_fields[0]->isChecked())v.label=m_label->text();if(m_fields[1]->isChecked())v.status=m_status->text();if(m_fields[2]->isChecked()){v.tags.clear();for(const auto &tag:m_tags->text().split(';',Qt::SkipEmptyParts))v.tags.append(tag.trimmed());}if(m_fields[3]->isChecked())v.color=m_color->text();if(m_fields[4]->isChecked())v.rating=m_rating->value();if(m_fields[5]->isChecked())v.description=m_description->toPlainText();const auto error=validateAnnotation(v);if(!error.isEmpty()){m_message->setText(error);return false;}staged.append({index,v});
    }
    for(const auto &pair:staged)m_records[pair.first].value=pair.second;refreshRows();for(auto *field:m_fields)field->setChecked(false);m_message->setText(tr("Alterações preparadas. Guarde para confirmar."));return true;
}
void AnnotationsDialog::save(){
    if(m_job||!stageSelected())return;QVector<AnnotationRecord>changes;for(const auto &r:m_records)if(r.error.isEmpty()&&encodeAnnotation(r.before)!=encodeAnnotation(r.value))changes.append(r);if(changes.isEmpty()){m_message->setText(tr("Nenhuma classificação foi alterada."));return;}
    setBusy(true);m_message->setText(tr("Guardando %1 classificações…").arg(changes.size()));m_job=new AnnotationJob(m_path,changes,m_catalog,this);connect(m_job,&QThread::finished,this,[this]{m_job->wait();auto result=m_job->takeResult();m_job->deleteLater();m_job=nullptr;setBusy(false);if(result.saved){if(m_path.isEmpty())m_catalog=std::move(result.catalog);accept();return;}m_message->setText(result.error);if(m_closePending)QDialog::reject();});m_job->start();
}
