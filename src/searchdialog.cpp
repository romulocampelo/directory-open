#include "searchdialog.h"
#include <QCheckBox>
#include <QComboBox>
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
    m_name = new QLineEdit(this); m_name->setObjectName("searchName"); m_name->setMaxLength(1024); m_name->setPlaceholderText(tr("Parte do nome, por exemplo: férias")); form->addRow(tr("Nome:"), m_name);
    m_engine = new QComboBox(this); m_engine->setObjectName("searchEngine");
    m_engine->addItem(tr("Automático — preferir Everything"), int(SearchEngine::Automatic));
    m_engine->addItem(tr("Everything — índice"), int(SearchEngine::Everything));
    m_engine->addItem(tr("Busca direta — percorrer pastas"), int(SearchEngine::Direct));
    form->addRow(tr("Método:"), m_engine);
    m_instance = new QLineEdit(this); m_instance->setObjectName("searchEverythingInstance"); m_instance->setMaxLength(128);
    m_instance->setPlaceholderText(tr("Vazio = instância padrão; informe o nome de uma instância já aberta"));
    form->addRow(tr("Instância Everything:"), m_instance); layout->addLayout(form);
    connect(m_engine, &QComboBox::currentIndexChanged, this, [this] { m_instance->setEnabled(SearchEngine(m_engine->currentData().toInt()) != SearchEngine::Direct); });
    m_wildcard = new QCheckBox(tr("Usar padrões (* e ?), por exemplo: *.pdf"), this); m_wildcard->setObjectName("searchWildcard"); layout->addWidget(m_wildcard);
    m_matchPath = new QCheckBox(tr("Procurar no caminho completo (inclui as pastas; * atravessa separadores)"), this);
    m_matchPath->setObjectName("searchMatchPath"); layout->addWidget(m_matchPath);
    auto *filterRow = new QHBoxLayout; m_filterButton = new QPushButton(tr("Filtros de tamanho, data e tipo…"), this);
    m_filterButton->setObjectName("searchFilters"); m_filterSummary = new QLabel(tr("Sem filtros adicionais."), this);
    m_filterSummary->setObjectName("searchFilterSummary"); m_filterSummary->setWordWrap(true);
    filterRow->addWidget(m_filterButton); filterRow->addWidget(m_filterSummary, 1); layout->addLayout(filterRow);
    connect(m_filterButton, &QPushButton::clicked, this, [this] {
        if (const auto rules = editFolderRules(m_filters, this, tr("Filtros da busca"))) {
            m_filters = *rules;
            m_filterSummary->setText(m_filters == FolderRules{} ? tr("Sem filtros adicionais.")
                : tr("Filtros ativos. Padrões, tamanho e data se aplicam aos arquivos; pastas têm seleção própria."));
        }
    });
    auto *scope = new QLabel(tr("Busca nesta pasta e subpastas, sem ler conteúdo nem seguir links/junções. Filtros conferem metadados atuais. Everything consulta os locais e exclusões do seu índice; instâncias são usadas sem iniciar ou reconfigurar o programa."), this); scope->setWordWrap(true); layout->addWidget(scope);
    auto *buttons = new QHBoxLayout; m_search = new QPushButton(tr("Buscar"), this); m_search->setObjectName("startSearch"); m_search->setDefault(true);
    m_cancel = new QPushButton(tr("Cancelar busca"), this); m_cancel->setObjectName("cancelSearch"); m_cancel->setEnabled(false);
    buttons->addWidget(m_search); buttons->addWidget(m_cancel); buttons->addStretch(); layout->addLayout(buttons);
    m_results = new QTreeWidget(this); m_results->setObjectName("searchResults"); m_results->setColumnCount(3);
    m_results->setHeaderLabels({tr("Nome"), tr("Pasta"), tr("Tipo")}); m_results->setRootIsDecorated(false); m_results->setUniformRowHeights(true);
    m_results->setSelectionMode(QAbstractItemView::SingleSelection); m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_results->header()->setStretchLastSection(false); m_results->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_results->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents); m_results->setColumnWidth(0, 260); layout->addWidget(m_results, 1);
    m_status = new QLabel(tr("Digite um nome para começar. Até 10.000 resultados por busca."), this); m_status->setObjectName("searchStatus"); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
    auto *footer = new QHBoxLayout; m_openFile = new QPushButton(tr("Abrir"), this); m_openFile->setObjectName("openSearchResult"); m_openFile->setEnabled(false);
    m_open = new QPushButton(tr("Abrir pasta em nova aba"), this); m_open->setObjectName("openSearchFolder"); m_open->setEnabled(false);
    auto *close = new QPushButton(tr("Fechar"), this); footer->addWidget(m_openFile); footer->addWidget(m_open); footer->addStretch(); footer->addWidget(close); layout->addLayout(footer);
    connect(close, &QPushButton::clicked, this, &SearchDialog::reject);
    connect(m_browse, &QPushButton::clicked, this, [this] { const auto path = QFileDialog::getExistingDirectory(this, tr("Pasta da busca"), m_root->text()); if (!path.isEmpty()) m_root->setText(QDir::toNativeSeparators(path)); });
    connect(m_search, &QPushButton::clicked, this, &SearchDialog::startSearch);
    connect(m_cancel, &QPushButton::clicked, this, [this] { if (m_job) { m_cancelRequested = true; m_job->cancel(); m_cancel->setEnabled(false); m_status->setText(tr("Cancelamento solicitado. Aguardando o encerramento da busca…")); } });
    connect(m_results, &QTreeWidget::itemSelectionChanged, this, [this] {
        const auto selected = m_results->currentItem() != nullptr; m_open->setEnabled(selected); m_openFile->setEnabled(selected);
    });
    connect(m_results, &QTreeWidget::itemDoubleClicked, this, [this] { openEntry(); });
    connect(m_openFile, &QPushButton::clicked, this, &SearchDialog::openEntry);
    connect(m_open, &QPushButton::clicked, this, &SearchDialog::openFolder);
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
    request.engine = SearchEngine(m_engine->currentData().toInt());
    request.everythingInstance = m_instance->text(); request.matchPath = m_matchPath->isChecked(); request.filters = m_filters;
    m_results->setSortingEnabled(false); m_results->clear(); m_open->setEnabled(false); m_openFile->setEnabled(false); m_status->setText(tr("Buscando…"));
    m_engine->setEnabled(false);
    m_instance->setEnabled(false); m_matchPath->setEnabled(false); m_filterButton->setEnabled(false);
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
        m_engine->setEnabled(true);
        m_instance->setEnabled(SearchEngine(m_engine->currentData().toInt()) != SearchEngine::Direct);
        m_matchPath->setEnabled(true); m_filterButton->setEnabled(true);
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
        case SearchState::Failed: state = s.matches ? tr("Busca interrompida por erro; resultados recebidos preservados.") : tr("Não foi possível concluir a busca."); break;
    }
    const auto matches = s.matches == 1 ? tr("1 resultado") : tr("%1 resultados").arg(s.matches);
    const auto visited = s.visited == 1 ? tr("1 item examinado") : tr("%1 itens examinados").arg(s.visited);
    const auto links = s.skippedLinks == 1 ? tr("1 link ignorado") : tr("%1 links ignorados").arg(s.skippedLinks);
    const auto errors = s.errors == 1 ? tr("1 erro") : tr("%1 erros").arg(s.errors);
    QString text = state + " " + s.source + " · " + QStringList{matches, visited, links, errors}.join(" · ");
    if (s.engine == SearchEngine::Everything) {
        text = state + " " + s.source + " · " + matches;
        if (s.indexedTotal) text += *s.indexedTotal == 1 ? tr(" · 1 candidato no índice, antes dos filtros")
            : tr(" · %1 candidatos no índice, antes dos filtros").arg(*s.indexedTotal);
        text += " · " + visited + " · " + links + " · " + errors;
        text += tr("\nCobertura e atualização dependem do índice do Everything. Para conferir a pasta, use a busca direta.");
    }
    if (!s.fallbackReason.isEmpty()) text += "\n" + s.fallbackReason;
    if (final && s.failure) text += "\n" + s.failure->message;
    else if (final && !s.details.isEmpty()) text += "\n" + s.details.first();
    m_status->setText(text); m_status->setToolTip(QStringList{s.fallbackReason, s.failure ? s.failure->message : QString{}, s.details.join('\n')}.join('\n'));
}
void SearchDialog::openEntry() {
    const auto *item = m_results->currentItem(); if (!item) return;
    const auto path = item->data(0, Qt::UserRole).toString(); const auto directory = item->data(0, Qt::UserRole + 1).toBool();
    const QFileInfo info(path);
    if (!info.exists() || info.isDir() != directory) { m_status->setText(tr("Este resultado não está mais disponível ou foi alterado.")); return; }
    if (directory) openFolder(); else emit openFileRequested(path);
}
void SearchDialog::openFolder() {
    const auto *item = m_results->currentItem(); if (!item) return;
    const auto path = item->data(0, Qt::UserRole).toString(); const bool directory = item->data(0, Qt::UserRole + 1).toBool();
    const auto folder = directory ? path : QFileInfo(path).absolutePath();
    if (!QFileInfo(folder).isDir()) { m_status->setText(tr("A pasta deste resultado não está mais disponível.")); return; }
    emit openFolderRequested(folder);
}
