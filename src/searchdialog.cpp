#include "searchdialog.h"
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

SearchDialog::SearchDialog(const QString &root, QWidget *parent) : QDialog(parent) {
    setObjectName("searchDialog"); setWindowTitle(tr("Buscar arquivos e pastas")); resize(940, 600);
    setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(this); auto *form = new QFormLayout;
    auto *rootRow = new QHBoxLayout; m_root = new QLineEdit(QDir::toNativeSeparators(root), this); m_root->setObjectName("searchRoot");
    m_browse = new QPushButton(tr("Escolher…"), this); rootRow->addWidget(m_root); rootRow->addWidget(m_browse); form->addRow(tr("Buscar em:"), rootRow);
    m_name = new QLineEdit(this); m_name->setObjectName("searchName"); m_name->setMaxLength(1024); m_name->setPlaceholderText(tr("Parte do nome, por exemplo: férias")); form->addRow(tr("Nome:"), m_name); layout->addLayout(form);
    m_wildcard = new QCheckBox(tr("Usar padrões (* e ?), por exemplo: *.pdf"), this); m_wildcard->setObjectName("searchWildcard"); layout->addWidget(m_wildcard);
    auto *scope = new QLabel(tr("Inclui subpastas e itens ocultos. Ignora links e junções. Busca pelo nome, sem ler o conteúdo dos arquivos."), this); scope->setWordWrap(true); layout->addWidget(scope);
    auto *buttons = new QHBoxLayout; m_search = new QPushButton(tr("Buscar"), this); m_search->setObjectName("startSearch"); m_search->setDefault(true);
    m_cancel = new QPushButton(tr("Cancelar busca"), this); m_cancel->setObjectName("cancelSearch"); m_cancel->setEnabled(false);
    buttons->addWidget(m_search); buttons->addWidget(m_cancel); buttons->addStretch(); layout->addLayout(buttons);
    m_results = new QTreeWidget(this); m_results->setObjectName("searchResults"); m_results->setColumnCount(3);
    m_results->setHeaderLabels({tr("Nome"), tr("Pasta"), tr("Tipo")}); m_results->setRootIsDecorated(false); m_results->setUniformRowHeights(true);
    m_results->setSelectionMode(QAbstractItemView::SingleSelection); m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_results->header()->setStretchLastSection(false); m_results->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_results->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents); m_results->setColumnWidth(0, 260); layout->addWidget(m_results, 1);
    m_status = new QLabel(tr("Digite um nome para começar. Até 10.000 resultados por busca."), this); m_status->setObjectName("searchStatus"); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
    auto *footer = new QHBoxLayout; m_open = new QPushButton(tr("Abrir pasta em nova aba"), this); m_open->setObjectName("openSearchFolder"); m_open->setEnabled(false);
    auto *close = new QPushButton(tr("Fechar"), this); footer->addWidget(m_open); footer->addStretch(); footer->addWidget(close); layout->addLayout(footer);
    connect(close, &QPushButton::clicked, this, &SearchDialog::reject);
    connect(m_browse, &QPushButton::clicked, this, [this] { const auto path = QFileDialog::getExistingDirectory(this, tr("Pasta da busca"), m_root->text()); if (!path.isEmpty()) m_root->setText(QDir::toNativeSeparators(path)); });
    connect(m_search, &QPushButton::clicked, this, &SearchDialog::startSearch);
    connect(m_cancel, &QPushButton::clicked, this, [this] { if (m_job) { m_cancelRequested = true; m_job->cancel(); m_cancel->setEnabled(false); m_status->setText(tr("Cancelamento solicitado. Aguardando a leitura da pasta atual…")); } });
    connect(m_results, &QTreeWidget::itemSelectionChanged, this, [this] { m_open->setEnabled(m_results->currentItem() != nullptr); });
    connect(m_results, &QTreeWidget::itemDoubleClicked, this, [this] { openFolder(); }); connect(m_open, &QPushButton::clicked, this, &SearchDialog::openFolder);
}
SearchDialog::~SearchDialog() { if (m_job) { m_job->cancel(); m_job->wait(); } }
void SearchDialog::reject() {
    if (m_job) { m_closeWhenFinished = true; m_job->cancel(); m_cancel->setEnabled(false); m_status->setText(tr("Aguardando o encerramento da busca…")); return; }
    QDialog::reject();
}
void SearchDialog::startSearch() {
    if (m_job) return;
    m_cancelRequested = false;
    SearchRequest request; request.root = QDir::cleanPath(QDir::fromNativeSeparators(m_root->text())); request.name = m_name->text(); request.wildcard = m_wildcard->isChecked();
    m_results->setSortingEnabled(false); m_results->clear(); m_open->setEnabled(false); m_status->setText(tr("Buscando…"));
    m_search->setEnabled(false); m_browse->setEnabled(false); m_root->setEnabled(false); m_name->setEnabled(false); m_wildcard->setEnabled(false); m_cancel->setEnabled(true);
    m_job = new SearchJob(request, this);
    connect(m_job, &SearchJob::updated, this, [this](const QVector<SearchEntry> &entries, const SearchSummary &summary) {
        for (const auto &entry : entries) {
            const QFileInfo info(entry.path); auto *item = new QTreeWidgetItem(m_results, {info.fileName(), QDir::toNativeSeparators(info.absolutePath()), entry.directory ? tr("Pasta") : tr("Arquivo")});
            item->setData(0, Qt::UserRole, entry.path); item->setData(0, Qt::UserRole + 1, entry.directory);
            item->setToolTip(0, QDir::toNativeSeparators(entry.path)); item->setToolTip(1, QDir::toNativeSeparators(info.absolutePath()));
        }
        if (!m_closeWhenFinished && !m_cancelRequested) showSummary(summary, false);
    });
    connect(m_job, &QThread::finished, this, [this] {
        m_job->wait(); const auto summary = m_job->summary(); m_job->deleteLater(); m_job = nullptr;
        m_search->setEnabled(true); m_browse->setEnabled(true); m_root->setEnabled(true); m_name->setEnabled(true); m_wildcard->setEnabled(true); m_cancel->setEnabled(false);
        m_results->setSortingEnabled(true); m_results->sortItems(0, Qt::AscendingOrder); showSummary(summary, true);
        if (m_closeWhenFinished) QDialog::reject();
    });
    m_job->start();
}
void SearchDialog::showSummary(const SearchSummary &s, bool final) {
    QString state = tr("Buscando…");
    if (final) switch (s.state) {
        case SearchState::Completed: state = tr("Busca concluída."); break;
        case SearchState::Cancelled: state = tr("Busca cancelada; resultados parciais."); break;
        case SearchState::Limited: state = tr("Limite atingido; restrinja a pasta ou o nome para continuar."); break;
        case SearchState::Partial: state = tr("Busca parcial; algumas pastas ou itens não puderam ser examinados."); break;
        case SearchState::Failed: state = tr("Não foi possível iniciar a busca."); break;
    }
    const auto matches = s.matches == 1 ? tr("1 resultado") : tr("%1 resultados").arg(s.matches);
    const auto visited = s.visited == 1 ? tr("1 item examinado") : tr("%1 itens examinados").arg(s.visited);
    const auto links = s.skippedLinks == 1 ? tr("1 link ignorado") : tr("%1 links ignorados").arg(s.skippedLinks);
    const auto errors = s.errors == 1 ? tr("1 erro") : tr("%1 erros").arg(s.errors);
    QString text = state + " " + QStringList{matches, visited, links, errors}.join(" · ");
    if (final && !s.details.isEmpty()) text += "\n" + s.details.first();
    m_status->setText(text); m_status->setToolTip(s.details.join('\n'));
}
void SearchDialog::openFolder() {
    const auto *item = m_results->currentItem(); if (!item) return;
    const auto path = item->data(0, Qt::UserRole).toString(); const bool directory = item->data(0, Qt::UserRole + 1).toBool();
    const auto folder = directory ? path : QFileInfo(path).absolutePath();
    if (!QFileInfo(folder).isDir()) { m_status->setText(tr("A pasta deste resultado não está mais disponível.")); return; }
    emit openFolderRequested(folder);
}
