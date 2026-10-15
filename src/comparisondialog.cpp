#include "comparisondialog.h"
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

ComparisonDialog::ComparisonDialog(const QString &left, const QString &right, QWidget *parent) : QDialog(parent) {
    setObjectName("comparisonDialog"); setWindowTitle(tr("Comparar pastas")); resize(1120, 700);
    setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(this); auto *form = new QFormLayout;
    auto addPath = [&](const QString &label, const QString &path, const QString &name, QLineEdit *&edit, QPushButton *&browse) {
        auto *row = new QHBoxLayout; edit = new QLineEdit(QDir::toNativeSeparators(path), this); edit->setObjectName(name);
        browse = new QPushButton(tr("Escolher…"), this); row->addWidget(edit); row->addWidget(browse); form->addRow(label, row);
        connect(browse, &QPushButton::clicked, this, [this, edit] {
            const auto selected = QFileDialog::getExistingDirectory(this, tr("Pasta para comparar"), edit->text());
            if (!selected.isEmpty()) edit->setText(QDir::toNativeSeparators(selected));
        });
    };
    addPath(tr("Esquerda:"), left, "comparisonLeft", m_left, m_leftBrowse);
    addPath(tr("Direita:"), right, "comparisonRight", m_right, m_rightBrowse); layout->addLayout(form);
    auto *options = new QHBoxLayout;
    m_recursive = new QCheckBox(tr("Incluir subpastas"), this); m_recursive->setObjectName("comparisonRecursive"); m_recursive->setChecked(true);
    m_content = new QCheckBox(tr("Conferir conteúdo dos arquivos"), this); m_content->setObjectName("comparisonContent");
    m_content->setToolTip(tr("Compara bytes diretamente, inclusive quando as datas diferem. Pode demorar em arquivos grandes ou na rede. Cancelamento disponível."));
    m_differences = new QCheckBox(tr("Mostrar somente diferenças e pendências"), this); m_differences->setObjectName("comparisonDifferences");
    options->addWidget(m_recursive); options->addWidget(m_content); options->addWidget(m_differences); options->addStretch(); layout->addLayout(options);
    auto *scope = new QLabel(tr("Compara nomes exatos, incluindo maiúsculas/minúsculas, e inclui itens ocultos. Ignora links e junções. Até 5.000 itens por lado. Resultados retratam a leitura; compare novamente após alterações nas pastas."), this);
    scope->setWordWrap(true); layout->addWidget(scope);
    auto *buttons = new QHBoxLayout; m_start = new QPushButton(tr("Comparar"), this); m_start->setObjectName("startComparison"); m_start->setDefault(true);
    m_cancel = new QPushButton(tr("Cancelar comparação"), this); m_cancel->setObjectName("cancelComparison"); m_cancel->setEnabled(false);
    buttons->addWidget(m_start); buttons->addWidget(m_cancel); buttons->addStretch(); layout->addLayout(buttons);
    m_results = new QTreeWidget(this); m_results->setObjectName("comparisonResults"); m_results->setColumnCount(4);
    m_results->setHeaderLabels({tr("Caminho relativo"), tr("Resultado"), tr("Esquerda"), tr("Direita")});
    m_results->setRootIsDecorated(false); m_results->setUniformRowHeights(true); m_results->setSelectionMode(QAbstractItemView::SingleSelection);
    m_results->setEditTriggers(QAbstractItemView::NoEditTriggers); m_results->header()->setStretchLastSection(false);
    m_results->header()->setSectionResizeMode(0, QHeaderView::Stretch); m_results->setColumnWidth(1, 310); m_results->setColumnWidth(2, 210); m_results->setColumnWidth(3, 210);
    layout->addWidget(m_results, 1);
    m_status = new QLabel(tr("Escolha as pastas e clique em Comparar. O modo inicial compara tamanho/data, sem ler o conteúdo."), this);
    m_status->setObjectName("comparisonStatus"); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
    auto *footer = new QHBoxLayout; m_openLeft = new QPushButton(tr("Abrir à esquerda em nova aba"), this); m_openLeft->setObjectName("openComparisonLeft");
    m_openRight = new QPushButton(tr("Abrir à direita em nova aba"), this); m_openRight->setObjectName("openComparisonRight");
    m_openLeft->setEnabled(false); m_openRight->setEnabled(false);
    auto *close = new QPushButton(tr("Fechar"), this); close->setObjectName("closeComparison");
    footer->addWidget(m_openLeft); footer->addWidget(m_openRight); footer->addStretch(); footer->addWidget(close); layout->addLayout(footer);
    connect(m_start, &QPushButton::clicked, this, &ComparisonDialog::startComparison);
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        if (m_job) { m_cancelRequested = true; m_job->cancel(); m_cancel->setEnabled(false); m_status->setText(tr("Cancelamento solicitado; aguardando a leitura em curso…")); }
    });
    connect(close, &QPushButton::clicked, this, &ComparisonDialog::reject);
    connect(m_differences, &QCheckBox::toggled, this, &ComparisonDialog::filterResults);
    connect(m_results, &QTreeWidget::itemSelectionChanged, this, [this] {
        const auto *item = m_results->currentItem(); const bool selected = item && !item->isHidden();
        m_openLeft->setEnabled(selected && !item->data(0, Qt::UserRole).toString().isEmpty());
        m_openRight->setEnabled(selected && !item->data(0, Qt::UserRole + 1).toString().isEmpty());
    });
    connect(m_openLeft, &QPushButton::clicked, this, [this] { openFolder(true); });
    connect(m_openRight, &QPushButton::clicked, this, [this] { openFolder(false); });
    m_timer = new QTimer(this); m_timer->setInterval(150);
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (m_job && !m_cancelRequested && !m_closeWhenFinished)
            m_status->setText(tr("Comparando… %1 itens examinados · %2 MiB lidos dos dois lados").arg(m_job->examined()).arg(double(m_job->bytesRead()) / (1024 * 1024), 0, 'f', 1));
    });
}
ComparisonDialog::~ComparisonDialog() { if (m_job) { m_job->cancel(); m_job->wait(); } }
void ComparisonDialog::reject() {
    if (m_job) { m_closeWhenFinished = true; m_job->cancel(); m_cancel->setEnabled(false); m_status->setText(tr("Aguardando o encerramento da comparação…")); return; }
    QDialog::reject();
}
void ComparisonDialog::startComparison() {
    if (m_job) return;
    ComparisonRequest request; request.left = QDir::fromNativeSeparators(m_left->text()); request.right = QDir::fromNativeSeparators(m_right->text());
    request.recursive = m_recursive->isChecked(); request.content = m_content->isChecked();
    m_report = {}; m_results->setSortingEnabled(false); m_results->clear(); m_cancelRequested = false;
    m_openLeft->setEnabled(false); m_openRight->setEnabled(false); m_status->setText(tr("Comparando…"));
    for (auto *widget : QList<QWidget *>{m_start, m_left, m_right, m_leftBrowse, m_rightBrowse, m_recursive, m_content}) widget->setEnabled(false);
    m_cancel->setEnabled(true); m_job = new ComparisonJob(request, this);
    connect(m_job, &QThread::finished, this, [this] {
        m_timer->stop(); m_job->wait(); m_report = m_job->takeReport(); m_job->deleteLater(); m_job = nullptr;
        for (auto *widget : QList<QWidget *>{m_start, m_left, m_right, m_leftBrowse, m_rightBrowse, m_recursive, m_content}) widget->setEnabled(true);
        m_cancel->setEnabled(false); showReport();
        if (m_closeWhenFinished) QDialog::reject();
    });
    m_timer->start(); m_job->start();
}
void ComparisonDialog::showReport() {
    int only = 0, different = 0, equal = 0, candidates = 0, unknown = 0;
    m_results->setUpdatesEnabled(false);
    for (const auto &entry : m_report.entries) {
        auto *item = new QTreeWidgetItem(m_results, {entry.relativePath, comparisonLabel(entry.kind), entry.leftDescription, entry.rightDescription});
        item->setData(0, Qt::UserRole, entry.leftPath); item->setData(0, Qt::UserRole + 1, entry.rightPath); item->setData(0, Qt::UserRole + 2, int(entry.kind));
        item->setToolTip(0, QDir::toNativeSeparators(entry.leftPath) + '\n' + QDir::toNativeSeparators(entry.rightPath));
        for (int column = 1; column < 4; ++column) item->setToolTip(column, entry.detail);
        switch (entry.kind) {
        case ComparisonKind::LeftOnly: case ComparisonKind::RightOnly: ++only; break;
        case ComparisonKind::MetadataDifferent: case ComparisonKind::ContentDifferent: case ComparisonKind::TypeConflict: ++different; break;
        case ComparisonKind::ContentEqual: ++equal; break;
        case ComparisonKind::MetadataMatch: ++candidates; break;
        case ComparisonKind::IgnoredLink: case ComparisonKind::Unknown: ++unknown; break;
        case ComparisonKind::Folders: break;
        }
        if (entry.kind == ComparisonKind::LeftOnly || entry.kind == ComparisonKind::RightOnly || entry.kind == ComparisonKind::TypeConflict
            || entry.kind == ComparisonKind::ContentDifferent || entry.kind == ComparisonKind::MetadataDifferent) {
            auto font = item->font(1); font.setBold(true); item->setFont(1, font);
        }
    }
    m_results->setSortingEnabled(true); m_results->sortItems(0, Qt::AscendingOrder); m_results->setUpdatesEnabled(true); filterResults();
    QString state;
    switch (m_report.state) {
    case ComparisonState::Completed: state = tr("Comparação concluída no escopo escolhido."); break;
    case ComparisonState::Cancelled: state = tr("Comparação cancelada; resultados parciais."); break;
    case ComparisonState::Limited: state = tr("Limite atingido; resultados parciais. Escolha pastas menores."); break;
    case ComparisonState::Partial: state = tr("Comparação parcial; houve erros de leitura."); break;
    case ComparisonState::Failed: state = tr("Não foi possível comparar as pastas."); break;
    }
    m_status->setText(state + tr(" Exclusivos: %1 · diferentes: %2 · conteúdo igual: %3 · tamanho/data iguais (sem conferir conteúdo): %4 · pendências: %5 · erros: %6 · links ignorados: %7.")
        .arg(only).arg(different).arg(equal).arg(candidates).arg(unknown).arg(m_report.errors).arg(m_report.skippedLinks)
        + (m_report.details.isEmpty() ? QString{} : "\n" + m_report.details.first()));
    m_status->setToolTip(m_report.details.join('\n'));
}
void ComparisonDialog::filterResults() {
    for (int i = 0; i < m_results->topLevelItemCount(); ++i) {
        auto *item = m_results->topLevelItem(i); const auto kind = ComparisonKind(item->data(0, Qt::UserRole + 2).toInt());
        item->setHidden(m_differences->isChecked() && (kind == ComparisonKind::ContentEqual || kind == ComparisonKind::MetadataMatch || kind == ComparisonKind::Folders));
    }
    if (m_results->currentItem() && m_results->currentItem()->isHidden()) { m_results->clearSelection(); m_results->setCurrentItem(nullptr); }
}
void ComparisonDialog::openFolder(bool left) {
    const auto *item = m_results->currentItem(); if (!item || item->isHidden()) return;
    const auto path = item->data(0, left ? Qt::UserRole : Qt::UserRole + 1).toString(); if (path.isEmpty()) return;
    const QFileInfo info(path); const auto folder = info.isDir() ? path : info.absolutePath();
    if (!QFileInfo(folder).isDir()) { m_status->setText(tr("A pasta deste resultado não está mais disponível. Compare novamente.")); return; }
    emit openFolderRequested(folder, left);
}
