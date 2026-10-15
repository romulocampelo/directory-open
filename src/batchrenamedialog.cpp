#include "batchrenamedialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

BatchRenameDialog::BatchRenameDialog(QStringList sources, QWidget *parent) : QDialog(parent), m_sources(std::move(sources)) {
    setObjectName("batchRenameDialog"); setWindowTitle(tr("Renomear em lote")); resize(1040, 730);
    auto *layout = new QVBoxLayout(this); auto *form = new QFormLayout;
    auto edit = [&](const QString &name, const QString &text) { auto *value = new QLineEdit(text, this); value->setObjectName(name); value->setMaxLength(1024); return value; };
    m_pattern = edit("renamePattern", "{name}_{n}"); form->addRow(tr("Modelo do nome:"), m_pattern);
    auto *replaceRow = new QHBoxLayout; m_find = edit("renameFind", {}); m_replace = edit("renameReplace", {});
    m_find->setPlaceholderText(tr("Texto a procurar")); m_replace->setPlaceholderText(tr("Substituir por")); replaceRow->addWidget(m_find); replaceRow->addWidget(m_replace); form->addRow(tr("Substituição literal:"), replaceRow);
    auto *numbers = new QHBoxLayout;
    auto spin = [&](const char *name, int min, int max, int initial) { auto *value = new QSpinBox(this); value->setObjectName(name); value->setRange(min, max); value->setValue(initial); return value; };
    m_start = spin("renameStart", 0, 1000000000, 1); m_step = spin("renameStep", 1, 1000000, 1); m_digits = spin("renameDigits", 1, 10, 3);
    numbers->addWidget(new QLabel(tr("Início"), this)); numbers->addWidget(m_start); numbers->addWidget(new QLabel(tr("Passo"), this)); numbers->addWidget(m_step); numbers->addWidget(new QLabel(tr("Dígitos mínimos"), this)); numbers->addWidget(m_digits); form->addRow(tr("Numeração:"), numbers);
    auto *dates = new QHBoxLayout; m_dateSource = new QComboBox(this); m_dateSource->setObjectName("renameDateSource"); m_dateSource->addItems({tr("Modificação"), tr("Criação no sistema de arquivos"), tr("Data escolhida")});
    m_dateFormat = new QComboBox(this); m_dateFormat->setObjectName("renameDateFormat"); m_dateFormat->addItems({"yyyy-MM-dd", "yyyyMMdd", "yyyy-MM-dd_HHmmss"});
    m_date = new QDateEdit(QDate::currentDate(), this); m_date->setObjectName("renameDate"); m_date->setCalendarPopup(true); m_date->setDisplayFormat("dd/MM/yyyy"); m_date->setEnabled(false);
    dates->addWidget(m_dateSource); dates->addWidget(m_dateFormat); dates->addWidget(m_date); form->addRow(tr("Campo de data:"), dates); layout->addLayout(form);
    auto *options = new QHBoxLayout; m_extension = new QCheckBox(tr("Preservar a última extensão dos arquivos"), this); m_extension->setObjectName("renamePreserveExtension"); m_extension->setChecked(true);
    m_case = new QCheckBox(tr("Diferenciar maiúsculas na substituição"), this); m_case->setChecked(true); options->addWidget(m_extension); options->addWidget(m_case); options->addStretch(); layout->addLayout(options);
    auto *hint = new QLabel(tr("Campos: {name} nome, {n} número, {date} data. A substituição atua no nome antes do modelo. A ordem é a exibida no painel; desmarque itens para excluí-los. Até 1.000 itens da mesma pasta. Datas de arquivo não são EXIF."), this); hint->setWordWrap(true); layout->addWidget(hint);
    m_table = new QTreeWidget(this); m_table->setObjectName("batchRenameRows"); m_table->setColumnCount(4); m_table->setHeaderLabels({tr("Incluir"), tr("Nome atual"), tr("Nome final"), tr("Situação")}); m_table->setRootIsDecorated(false); m_table->setUniformRowHeights(true); m_table->setEditTriggers(QAbstractItemView::NoEditTriggers); m_table->setColumnWidth(0, 65); m_table->header()->setSectionResizeMode(1, QHeaderView::Stretch); m_table->header()->setSectionResizeMode(2, QHeaderView::Stretch); m_table->setColumnWidth(3, 300);
    for (const auto &source : m_sources) { auto *item = new QTreeWidgetItem(m_table, {QString{}, QFileInfo(source).fileName(), {}, {}}); m_rows.insert(source, item); item->setCheckState(0, Qt::Checked); item->setData(0, Qt::UserRole, source); item->setToolTip(1, QDir::toNativeSeparators(source)); } layout->addWidget(m_table, 1);
    m_status = new QLabel(this); m_status->setObjectName("batchRenameStatus"); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
    m_confirm = new QCheckBox(tr("Revisei os nomes finais e quero aplicar esta prévia."), this); m_confirm->setObjectName("confirmBatchRename"); layout->addWidget(m_confirm);
    auto *buttons = new QHBoxLayout; m_preview = new QPushButton(tr("Gerar prévia"), this); m_preview->setObjectName("previewBatchRename"); m_apply = new QPushButton(tr("Aplicar renomeação"), this); m_apply->setObjectName("applyBatchRename");
    m_cancel = new QPushButton(tr("Cancelar trabalho"), this); m_cancel->setObjectName("cancelBatchRename"); m_close = new QPushButton(tr("Fechar"), this); m_close->setDefault(true);
    for (auto *button : {m_preview, m_apply, m_cancel}) { button->setAutoDefault(false); buttons->addWidget(button); } buttons->addStretch(); buttons->addWidget(m_close); layout->addLayout(buttons);
    for (auto *value : {m_pattern, m_find, m_replace}) connect(value, &QLineEdit::textChanged, this, &BatchRenameDialog::invalidate);
    for (auto *value : {m_start, m_step, m_digits}) connect(value, &QSpinBox::valueChanged, this, &BatchRenameDialog::invalidate);
    connect(m_extension, &QCheckBox::toggled, this, &BatchRenameDialog::invalidate); connect(m_case, &QCheckBox::toggled, this, &BatchRenameDialog::invalidate);
    connect(m_dateSource, &QComboBox::currentIndexChanged, this, [this] { m_date->setEnabled(m_dateSource->currentIndex() == 2); invalidate(); }); connect(m_dateFormat, &QComboBox::currentTextChanged, this, &BatchRenameDialog::invalidate); connect(m_date, &QDateEdit::dateChanged, this, &BatchRenameDialog::invalidate);
    connect(m_table, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *, int column) { if (column == 0) invalidate(); });
    connect(m_confirm, &QCheckBox::toggled, this, [this] { m_apply->setEnabled(!m_job && !m_applied && m_plan.valid() && m_plan.changes() > 0 && m_confirm->isChecked()); });
    connect(m_preview, &QPushButton::clicked, this, &BatchRenameDialog::startPreview); connect(m_apply, &QPushButton::clicked, this, &BatchRenameDialog::apply);
    connect(m_close, &QPushButton::clicked, this, &BatchRenameDialog::reject); connect(m_cancel, &QPushButton::clicked, this, [this] { if (m_job) { m_job->cancel(); m_cancel->setEnabled(false); m_status->setText(tr("Cancelamento solicitado; itens já concluídos são preservados.")); } });
    m_timer = new QTimer(this); m_timer->setInterval(150); connect(m_timer, &QTimer::timeout, this, [this] { if (m_job && m_applying && m_cancel->isEnabled()) m_status->setText(tr("Renomeando… %1 de %2 itens processados.").arg(m_job->done()).arg(m_plan.items.size())); });
    invalidate(); QTimer::singleShot(0, this, &BatchRenameDialog::startPreview);
}
BatchRenameDialog::~BatchRenameDialog() { if (m_job) { m_job->cancel(); m_job->wait(); } }
void BatchRenameDialog::reject() { if (m_job) { m_closePending = true; m_job->cancel(); m_cancel->setEnabled(false); m_status->setText(tr("Aguardando o encerramento…")); return; } QDialog::reject(); }
void BatchRenameDialog::invalidate() {
    m_plan = {}; m_confirm->setChecked(false); m_apply->setEnabled(false); m_cancel->setEnabled(false);
    const QSignalBlocker blocker(m_table); for (int i = 0; i < m_table->topLevelItemCount(); ++i) { auto *row = m_table->topLevelItem(i); row->setText(2, {}); row->setText(3, row->checkState(0) == Qt::Checked ? tr("Gerar prévia") : tr("Fora do lote")); }
    m_status->setText(tr("Gere a prévia e confira os nomes. Nenhum item será substituído. Trocas/ciclos de nomes ocupados são conflitos."));
}
void BatchRenameDialog::setBusy(bool busy) {
    for (auto *widget : QList<QWidget *>{m_pattern,m_find,m_replace,m_start,m_step,m_digits,m_extension,m_case,m_dateSource,m_dateFormat,m_table,m_preview}) widget->setEnabled(!busy && !m_applied);
    m_date->setEnabled(!busy && !m_applied && m_dateSource->currentIndex() == 2); m_confirm->setEnabled(!busy && !m_applied); m_apply->setEnabled(false); m_cancel->setEnabled(busy);
}
void BatchRenameDialog::startPreview() {
    if (m_job || m_applied) return;
    QStringList sources; for (int i = 0; i < m_table->topLevelItemCount(); ++i) if (m_table->topLevelItem(i)->checkState(0) == Qt::Checked) sources.append(m_table->topLevelItem(i)->data(0, Qt::UserRole).toString());
    BatchRenameOptions options; options.pattern = m_pattern->text(); options.find = m_find->text(); options.replacement = m_replace->text(); options.preserveExtension = m_extension->isChecked(); options.caseSensitive = m_case->isChecked(); options.start = m_start->value(); options.step = m_step->value(); options.digits = m_digits->value(); options.dateSource = RenameDateSource(m_dateSource->currentIndex()); options.dateFormat = m_dateFormat->currentText(); options.fixedDate = m_date->date();
    m_confirm->setChecked(false); setBusy(true); m_status->setText(tr("Preparando prévia…")); m_job = new BatchRenameJob(sources, options, this);
    connect(m_job, &QThread::finished, this, [this] {
        m_job->wait(); m_plan = m_job->plan(); m_job->deleteLater(); m_job = nullptr; setBusy(false);
        const QSignalBlocker blocker(m_table);
        for (const auto &item : m_plan.items) { auto *view = m_rows.value(item.source); if (!view) continue; view->setText(2, item.name); view->setText(3, !item.error.isEmpty() ? item.error : item.unchanged ? tr("Sem alteração") : tr("Pronto")); view->setToolTip(3, item.error); }
        m_status->setText(m_plan.cancelled ? tr("Prévia cancelada.") : !m_plan.error.isEmpty() ? m_plan.error : m_plan.valid() ? tr("Prévia pronta. Alterações: %1. Confira e marque a confirmação antes de aplicar.").arg(m_plan.changes()) : tr("Há conflitos ou erros. Ajuste as opções ou desmarque os itens e gere outra prévia."));
        if (m_closePending) QDialog::reject();
    }); m_job->start();
}
void BatchRenameDialog::apply() {
    if (m_job || !m_plan.valid() || !m_confirm->isChecked() || !m_plan.changes()) return;
    m_applying = true; setBusy(true); m_job = new BatchRenameJob(m_plan, this); m_timer->start();
    connect(m_job, &QThread::finished, this, [this] {
        m_timer->stop(); m_job->wait(); const auto result = m_job->result(); m_job->deleteLater(); m_job = nullptr; m_applied = true; setBusy(false);
        const QSignalBlocker blocker(m_table);
        for (const auto &item : result.items) { auto *view = m_rows.value(item.source); if (!view) continue;
            QString state; switch(item.state) { case OperationItemState::Completed: state = tr("Concluído"); break; case OperationItemState::Skipped: state = tr("Sem alteração"); break; case OperationItemState::NotStarted: state = tr("Não iniciado"); break; case OperationItemState::Cancelled: state = tr("Cancelado"); break; case OperationItemState::Failed: state = tr("Erro"); break; case OperationItemState::Incomplete: state = tr("Não confirmado"); break; }
            view->setText(3, state + (item.error.isEmpty() ? QString{} : ": " + item.error)); view->setToolTip(3, item.error);
        }
        m_status->setText((result.state == OperationState::Completed ? tr("Lote concluído.") : result.state == OperationState::Cancelled ? tr("Lote cancelado; resultado parcial.") : tr("Lote interrompido; confira o resultado por item.")) + tr(" Itens renomeados: %1. Não há desfazer automático.").arg(result.completedCount()) + (result.error.isEmpty() ? QString{} : "\n" + result.error));
        if (m_closePending) QDialog::reject();
    }); m_job->start();
}
