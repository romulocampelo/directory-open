#include "mediatoolsdialog.h"
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QUuid>

MediaToolsDialog::MediaToolsDialog(MediaTask task, ToolSettings settings, QStringList sources, const QString &parentPath, QWidget *parent)
    : QDialog(parent),m_task(task),m_settings(std::move(settings)),m_sources(std::move(sources)) {
    setObjectName("mediaToolsDialog"); resize(1040,780);
    setWindowTitle(task==MediaTask::Metadata?tr("Metadados — ExifTool"):task==MediaTask::Images?tr("Converter imagens — ImageMagick"):tr("Áudio e vídeo — FFmpeg"));
    auto *layout=new QVBoxLayout(this); m_controls=new QWidget(this); auto *form=new QFormLayout(m_controls); layout->addWidget(m_controls);
    auto fileField=[&](const QString &label,const QString &name,const QString &value){
        auto *row=new QWidget(m_controls);auto *box=new QHBoxLayout(row);box->setContentsMargins(0,0,0,0);
        auto *edit=new QLineEdit(value,row);edit->setObjectName(name);auto *browse=new QPushButton(tr("Escolher…"),row);box->addWidget(edit);box->addWidget(browse);form->addRow(label,row);
        connect(browse,&QPushButton::clicked,this,[this,edit]{const auto p=QFileDialog::getOpenFileName(this,tr("Escolher programa"),edit->text(),tr("Programas (*.exe)"));if(!p.isEmpty())edit->setText(p);});
        connect(edit,&QLineEdit::textChanged,this,&MediaToolsDialog::invalidate);return edit;
    };
    const auto name=task==MediaTask::Metadata?QString("exiftool"):task==MediaTask::Images?QString("magick"):QString("ffmpeg");
    const auto configured=task==MediaTask::Metadata?m_settings.exiftool:task==MediaTask::Images?m_settings.magick:m_settings.ffmpeg;
    m_exe=fileField(tr("Programa:"),"mediaExecutable",configured.isEmpty()?mediaToolExecutable(name):configured);
    if(task==MediaTask::AudioVideo)m_probe=fileField(tr("Consulta de mídia:"),"probeExecutable",m_settings.ffprobe.isEmpty()?mediaToolExecutable("ffprobe"):m_settings.ffprobe);
    m_destination=new QLineEdit(QDir(parentPath).filePath("Convertidos-"+QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)),m_controls);m_destination->setObjectName("mediaDestination");form->addRow(tr("Pasta nova de saída:"),m_destination);connect(m_destination,&QLineEdit::textChanged,this,&MediaToolsDialog::invalidate);
    if(task==MediaTask::Metadata){
        m_tag=new QComboBox(m_controls);m_tag->setObjectName("metadataTag");const auto tags=editableMetadataTags();for(auto it=tags.begin();it!=tags.end();++it)m_tag->addItem(it.value(),it.key());form->addRow(tr("Campo:"),m_tag);
        auto *row=new QWidget(m_controls);auto *box=new QHBoxLayout(row);box->setContentsMargins(0,0,0,0);m_value=new QLineEdit(row);m_value->setObjectName("metadataValue");m_value->setPlaceholderText(tr("Datas: AAAA:MM:DD HH:MM:SS"));auto *add=new QPushButton(tr("Acrescentar alteração"),row);add->setObjectName("addMetadataEdit");box->addWidget(m_value);box->addWidget(add);form->addRow(tr("Novo valor:"),row);
        m_edits=new QTreeWidget(m_controls);m_edits->setObjectName("metadataEdits");m_edits->setHeaderLabels({tr("Campo a aplicar em toda a seleção"),tr("Novo valor")});m_edits->setMaximumHeight(110);m_edits->setColumnWidth(0,320);m_edits->setRootIsDecorated(false);form->addRow(m_edits);
        connect(add,&QPushButton::clicked,this,[this]{const auto key=m_tag->currentData().toString();for(int i=0;i<m_edits->topLevelItemCount();++i)if(m_edits->topLevelItem(i)->data(0,Qt::UserRole).toString()==key){delete m_edits->takeTopLevelItem(i);break;}auto *item=new QTreeWidgetItem(m_edits,{m_tag->currentText(),m_value->text()});item->setData(0,Qt::UserRole,key);invalidate();});
        auto *remove=new QPushButton(tr("Remover alteração selecionada"),m_controls);form->addRow(remove);connect(remove,&QPushButton::clicked,this,[this]{delete m_edits->takeTopLevelItem(m_edits->indexOfTopLevelItem(m_edits->currentItem()));invalidate();});
    }else{
        m_format=new QComboBox(m_controls);m_format->setObjectName("mediaFormat");m_format->addItems(task==MediaTask::Images?QStringList{"png","jpg","webp","tiff"}:QStringList{"mp4","mp3","wav","flac"});form->addRow(tr("Formato de saída:"),m_format);connect(m_format,&QComboBox::currentIndexChanged,this,&MediaToolsDialog::invalidate);
        if(task==MediaTask::Images){
            auto *row=new QWidget(m_controls);auto *box=new QHBoxLayout(row);box->setContentsMargins(0,0,0,0);
            m_width=new QSpinBox(row);m_height=new QSpinBox(row);m_quality=new QSpinBox(row);m_rotation=new QComboBox(row);
            m_width->setObjectName("imageWidth");m_height->setObjectName("imageHeight");m_rotation->setObjectName("imageRotation");m_quality->setObjectName("imageQuality");
            for(auto *s:{m_width,m_height}){s->setRange(0,16384);s->setSpecialValueText(tr("Livre"));connect(s,&QSpinBox::valueChanged,this,&MediaToolsDialog::invalidate);}m_quality->setRange(1,100);m_quality->setValue(90);m_rotation->addItems({"0°","90°","180°","270°"});
            box->addWidget(new QLabel(tr("Largura:"),row));box->addWidget(m_width);box->addWidget(new QLabel(tr("Altura:"),row));box->addWidget(m_height);box->addWidget(m_rotation);box->addWidget(new QLabel(tr("Qualidade:"),row));box->addWidget(m_quality);form->addRow(row);
            connect(m_rotation,&QComboBox::currentIndexChanged,this,&MediaToolsDialog::invalidate);connect(m_quality,&QSpinBox::valueChanged,this,&MediaToolsDialog::invalidate);
        }
    }
    auto *help=new QLabel(task==MediaTask::Metadata?tr("Consulta propriedades da seleção. Acrescente campos para ver os valores atuais e propostos; salvar cria cópias com as alterações. O suporte depende do formato. Valores vazios/remoção e edição direta dos originais ficam para uma etapa posterior."):task==MediaTask::Images?tr("JPEG, PNG, WebP e TIFF raster. Aplica orientação EXIF, limita dimensões sem ampliar e gira no sentido horário. Apenas a primeira página/quadro. JPEG usa fundo branco para transparência; conversões podem perder dados e metadados. Originais preservados."):tr("Consulta codecs, fluxos, duração e características. MP4 usa H.264/AAC; MP3/WAV/FLAC extraem o primeiro áudio. MP4 usa o primeiro vídeo e áudio opcional; legendas, anexos, múltiplas faixas e preservação HDR ficam fora desta etapa. Saídas verificadas antes de publicar."),this);help->setWordWrap(true);help->setTextFormat(Qt::PlainText);layout->addWidget(help);
    auto *buttons=new QHBoxLayout;layout->addLayout(buttons);m_preview=new QPushButton(task==MediaTask::Metadata?tr("Consultar / gerar prévia"):tr("Gerar prévia"),this);m_preview->setObjectName("previewMedia");m_apply=new QPushButton(task==MediaTask::Metadata?tr("Salvar cópias…"):tr("Converter…"),this);m_apply->setObjectName("applyMedia");m_apply->setEnabled(false);buttons->addWidget(m_preview);buttons->addWidget(m_apply);connect(m_preview,&QPushButton::clicked,this,&MediaToolsDialog::inspect);connect(m_apply,&QPushButton::clicked,this,&MediaToolsDialog::apply);
    m_rows=new QTreeWidget(this);m_rows->setObjectName("mediaRows");m_rows->setHeaderLabels({tr("Origem"),tr("Novo arquivo")});m_rows->setRootIsDecorated(false);m_rows->setColumnWidth(0,520);layout->addWidget(m_rows,1);
    m_details=new QPlainTextEdit(this);m_details->setReadOnly(true);m_details->setObjectName("mediaDetails");m_details->setMaximumHeight(190);layout->addWidget(m_details,1);connect(m_rows,&QTreeWidget::currentItemChanged,this,[this](QTreeWidgetItem *i){m_details->setPlainText(i?i->data(0,Qt::UserRole).toString():QString());});
    if(task==MediaTask::Images){m_imagePreview=new QLabel(tr("Prévia visual do primeiro arquivo após gerar a prévia."),this);m_imagePreview->setAlignment(Qt::AlignCenter);m_imagePreview->setMaximumHeight(170);layout->addWidget(m_imagePreview);}
    m_status=new QLabel(tr("Seleção: %1 arquivos. Gere a prévia antes de executar.").arg(m_sources.size()),this);m_status->setObjectName("mediaStatus");m_status->setWordWrap(true);m_status->setTextFormat(Qt::PlainText);layout->addWidget(m_status);
    auto *bottom=new QHBoxLayout;layout->addLayout(bottom);m_open=new QPushButton(tr("Abrir pasta de saída"),this);m_open->setEnabled(false);bottom->addWidget(m_open);connect(m_open,&QPushButton::clicked,this,[this]{emit openOutput(m_destination->text());});bottom->addStretch();m_cancel=new QPushButton(tr("Cancelar tarefa"),this);m_cancel->setObjectName("cancelMedia");m_cancel->setEnabled(false);bottom->addWidget(m_cancel);connect(m_cancel,&QPushButton::clicked,this,[this]{if(m_job){m_job->cancel();m_cancel->setEnabled(false);m_status->setText(tr("Aguardando encerramento…"));}});auto *close=new QPushButton(tr("Fechar"),this);bottom->addWidget(close);connect(close,&QPushButton::clicked,this,&MediaToolsDialog::reject);
}
MediaToolsDialog::~MediaToolsDialog(){if(m_job){m_job->cancel();m_job->wait();delete m_job;}}
ToolSettings MediaToolsDialog::settings()const{auto s=m_settings;if(m_task==MediaTask::Metadata)s.exiftool=m_exe->text();else if(m_task==MediaTask::Images)s.magick=m_exe->text();else{s.ffmpeg=m_exe->text();s.ffprobe=m_probe->text();}return s;}
MediaOptions MediaToolsDialog::options()const{MediaOptions o;o.task=m_task;o.executable=m_exe->text();o.destination=m_destination->text();if(m_probe)o.probeExecutable=m_probe->text();if(m_format)o.format=m_format->currentText();if(m_width){o.width=m_width->value();o.height=m_height->value();o.rotation=m_rotation->currentIndex()*90;o.quality=m_quality->value();}if(m_edits)for(int i=0;i<m_edits->topLevelItemCount();++i){const auto *item=m_edits->topLevelItem(i);o.tags[item->data(0,Qt::UserRole).toString()]=item->text(1);}return o;}
void MediaToolsDialog::invalidate(){m_plan={};if(m_apply)m_apply->setEnabled(false);if(m_open)m_open->setEnabled(false);if(m_imagePreview)m_imagePreview->clear();}
void MediaToolsDialog::inspect(){if(m_job)return;m_applying=false;m_controls->setEnabled(false);m_preview->setEnabled(false);m_apply->setEnabled(false);m_cancel->setEnabled(true);m_rows->clear();m_details->clear();m_status->setText(tr("Consultando a seleção…"));m_job=new MediaToolsJob(m_sources,options(),this);connect(m_job,&QThread::finished,this,&MediaToolsDialog::finished);m_job->start();}
void MediaToolsDialog::apply(){if(m_job||!m_plan.valid())return;const auto text=tr("Criar %1 arquivos novos em:\n%2\n\nConfira a prévia. Os originais serão preservados. Cancelar mantém saídas já concluídas.").arg(m_plan.items.size()).arg(m_plan.options.destination);if(QMessageBox::question(this,tr("Confirmar conversão"),text,QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;m_applying=true;m_controls->setEnabled(false);m_preview->setEnabled(false);m_apply->setEnabled(false);m_cancel->setEnabled(true);m_job=new MediaToolsJob(m_plan,this);connect(m_job,&MediaToolsJob::progress,m_status,&QLabel::setText);connect(m_job,&QThread::finished,this,&MediaToolsDialog::finished);m_job->start();}
void MediaToolsDialog::finished(){m_job->wait();if(m_applying){const auto r=m_job->result();m_status->setText(r.message);m_details->setPlainText(r.diagnostics);m_open->setEnabled(!r.outputs.isEmpty());m_plan={};}else{m_plan=m_job->plan();if(m_plan.valid()){for(const auto &item:m_plan.items){auto *row=new QTreeWidgetItem(m_rows,{item.source,item.output});row->setData(0,Qt::UserRole,item.details);row->setToolTip(0,item.source);row->setToolTip(1,item.output);}if(m_imagePreview&&!m_plan.items.first().preview.isNull())m_imagePreview->setPixmap(QPixmap::fromImage(m_plan.items.first().preview).scaled(280,150,Qt::KeepAspectRatio,Qt::SmoothTransformation));m_rows->setCurrentItem(m_rows->topLevelItem(0));m_status->setText(tr("Prévia pronta: %1 arquivos. Selecione uma linha para os detalhes.").arg(m_plan.items.size()));}else m_status->setText(m_plan.cancelled?tr("Consulta cancelada."):m_plan.error);}delete m_job;m_job=nullptr;m_controls->setEnabled(true);m_preview->setEnabled(true);m_cancel->setEnabled(false);m_apply->setEnabled(m_plan.valid()&&(m_task!=MediaTask::Metadata||!m_plan.options.tags.isEmpty()));if(m_closing)QDialog::reject();}
void MediaToolsDialog::reject(){if(m_job){m_closing=true;m_job->cancel();m_status->setText(tr("Aguardando encerramento…"));return;}QDialog::reject();}
void MediaToolsDialog::closeEvent(QCloseEvent *e){if(m_job){e->ignore();reject();}else QDialog::closeEvent(e);}
