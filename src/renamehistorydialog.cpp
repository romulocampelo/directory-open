#include "renamehistorydialog.h"
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

RenameHistoryDialog::RenameHistoryDialog(QString root, QWidget *parent) : QDialog(parent),m_root(std::move(root)) {
    setObjectName("renameHistoryDialog"); setWindowTitle(tr("Histórico de renomeações")); resize(1060,740);
    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(tr("Escolha um lote para conferir a reversão. Só podem ser desfeitos itens cuja identidade, tamanho e datas conferem e cujo nome original está livre. Marque os itens e confirme a prévia."),this);
    hint->setWordWrap(true); layout->addWidget(hint);
    m_records = new QTreeWidget(this); m_records->setObjectName("renameHistoryRecords");
    m_records->setHeaderLabels({tr("Data"),tr("Pasta"),tr("Itens"),tr("Registro")}); m_records->setRootIsDecorated(false);
    m_records->setUniformRowHeights(true); m_records->setColumnWidth(0,160); m_records->header()->setSectionResizeMode(1,QHeaderView::Stretch);
    layout->addWidget(m_records,1);
    m_items = new QTreeWidget(this); m_items->setObjectName("renameHistoryItems");
    m_items->setHeaderLabels({tr("Desfazer"),tr("Nome atual"),tr("Nome original"),tr("Situação")}); m_items->setRootIsDecorated(false); m_items->setUniformRowHeights(true);
    m_items->setColumnWidth(0,70); m_items->header()->setSectionResizeMode(1,QHeaderView::Stretch); m_items->header()->setSectionResizeMode(2,QHeaderView::Stretch); m_items->setColumnWidth(3,350);
    layout->addWidget(m_items,2);
    m_status = new QLabel(this); m_status->setObjectName("renameHistoryStatus"); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
    m_confirm = new QCheckBox(tr("Revisei os nomes originais e quero desfazer os itens marcados."),this); m_confirm->setObjectName("confirmUndoRename"); layout->addWidget(m_confirm);
    auto *buttons = new QHBoxLayout;
    m_refresh = new QPushButton(tr("Atualizar histórico"),this); m_refresh->setObjectName("refreshRenameHistory");
    m_apply = new QPushButton(tr("Desfazer itens marcados"),this); m_apply->setObjectName("applyUndoRename");
    m_cancel = new QPushButton(tr("Cancelar trabalho"),this); m_cancel->setObjectName("cancelRenameHistory");
    auto *close = new QPushButton(tr("Fechar"),this); close->setDefault(true);
    for (auto *button : {m_refresh,m_apply,m_cancel}) { button->setAutoDefault(false); buttons->addWidget(button); }
    buttons->addStretch(); buttons->addWidget(close); layout->addLayout(buttons);
    connect(close,&QPushButton::clicked,this,&RenameHistoryDialog::reject);
    connect(m_refresh,&QPushButton::clicked,this,[this] { start({}); });
    connect(m_cancel,&QPushButton::clicked,this,[this] { if (m_job) { m_job->cancel(); m_cancel->setEnabled(false); m_status->setText(tr("Cancelamento solicitado; alterações já concluídas são preservadas.")); } });
    connect(m_confirm,&QCheckBox::toggled,this,&RenameHistoryDialog::updateApply);
    connect(m_items,&QTreeWidget::itemChanged,this,[this] { m_confirm->setChecked(false); updateApply(); });
    connect(m_records,&QTreeWidget::currentItemChanged,this,[this](QTreeWidgetItem *row) {
        if (!row || m_job) return;
        RenameHistoryRequest request; request.mode = RenameHistoryMode::Preview; request.id = row->data(0,Qt::UserRole).toString(); start(request);
    });
    connect(m_apply,&QPushButton::clicked,this,[this] {
        if (m_job || !m_apply->isEnabled()) return;
        RenameHistoryRequest request; request.mode = RenameHistoryMode::Undo; request.id = m_preview.id; request.baseline = m_preview.baseline;
        for (int i = 0; i < m_items->topLevelItemCount(); ++i) {
            auto *row = m_items->topLevelItem(i);
            if (row->checkState(0) == Qt::Checked) request.selected.append(row->data(0,Qt::UserRole).toInt());
        }
        start(request);
    });
    updateApply(); QTimer::singleShot(0,this,[this] { start({}); });
}
RenameHistoryDialog::~RenameHistoryDialog() { if (m_job) { m_job->cancel(); m_job->wait(); } }
void RenameHistoryDialog::reject() {
    if (m_job) { m_closePending = true; m_job->cancel(); m_status->setText(tr("Aguardando o encerramento…")); return; }
    QDialog::reject();
}
void RenameHistoryDialog::updateApply() {
    bool selected = false;
    for (int i = 0; i < m_items->topLevelItemCount(); ++i) selected |= m_items->topLevelItem(i)->checkState(0) == Qt::Checked;
    m_apply->setEnabled(!m_job && m_preview.error.isEmpty() && !m_preview.id.isEmpty() && selected && m_confirm->isChecked());
}
void RenameHistoryDialog::start(RenameHistoryRequest request) {
    if (m_job) return;
    m_confirm->setChecked(false); m_confirm->setEnabled(false); m_refresh->setEnabled(false);
    m_records->setEnabled(false); m_items->setEnabled(false); m_apply->setEnabled(false); m_cancel->setEnabled(true);
    m_status->setText(request.mode == RenameHistoryMode::Undo ? tr("Conferindo e desfazendo…") : tr("Consultando histórico…"));
    m_job = new RenameHistoryJob(m_root,request,this);
    connect(m_job,&QThread::finished,this,[this,request] {
        m_job->wait(); const auto report = m_job->report(); m_job->deleteLater(); m_job = nullptr;
        m_confirm->setEnabled(true); m_refresh->setEnabled(true); m_records->setEnabled(true); m_items->setEnabled(true); m_cancel->setEnabled(false);
        if (request.mode == RenameHistoryMode::List) {
            QSignalBlocker blocker(m_records); m_records->clear(); m_items->clear(); m_preview = {};
            for (const auto &record : report.records) {
                auto *row = new QTreeWidgetItem(m_records,{record.created.toLocalTime().toString("dd/MM/yyyy HH:mm:ss"),QDir::toNativeSeparators(record.folder),QString::number(record.count),
                    !record.error.isEmpty() ? record.error : record.phase == "prepared" || record.phase == "undoing" ? tr("Interrompido: conferir prévia") : tr("Disponível para consulta")});
                row->setData(0,Qt::UserRole,record.id); row->setToolTip(3,record.error);
            }
            m_status->setText(report.error.isEmpty() ? tr("%1 lotes. Escolha um registro para conferir os itens.").arg(report.records.size()) : report.error);
        } else if (request.mode == RenameHistoryMode::Preview) {
            QSignalBlocker blocker(m_items); m_items->clear(); m_preview = report.preview;
            for (const auto &item : m_preview.items) {
                auto *row = new QTreeWidgetItem(m_items,{QString{},QFileInfo(item.current).fileName(),QFileInfo(item.original).fileName(),item.detail});
                row->setData(0,Qt::UserRole,item.index); row->setToolTip(1,QDir::toNativeSeparators(item.current)); row->setToolTip(2,QDir::toNativeSeparators(item.original)); row->setToolTip(3,item.detail);
                if (item.state == UndoRenameState::Ready) row->setCheckState(0,Qt::Unchecked);
            }
            m_status->setText(report.error.isEmpty() ? tr("Prévia pronta. Marque os itens que deseja desfazer e confirme.") : report.error);
        } else {
            m_preview = {}; m_items->clear();
            const auto &result = report.operation;
            m_status->setText(!report.error.isEmpty() ? report.error :
                tr("Itens desfeitos: %1. %2 Atualize a prévia selecionando o lote novamente.").arg(result.completedCount())
                .arg(result.state == OperationState::Completed ? tr("Reversão concluída.") : tr("Trabalho interrompido; confira os itens restantes.")) + "\n" + result.error);
            // A failed apply must never leave an actionable stale preview.
            m_records->setCurrentItem(nullptr);
        }
        updateApply(); if (m_closePending) QDialog::reject();
    });
    m_job->start();
}
