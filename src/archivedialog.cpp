#include "archivedialog.h"
#include "specializedlaunch.h"
#include <QMessageBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <limits>

ArchiveDialog::ArchiveDialog(QStringList sources, QString destinationParent, bool create, QWidget *parent)
    : QDialog(parent), m_sources(std::move(sources)), m_create(create) {
    setObjectName("archiveDialog"); setWindowTitle(create ? tr("Criar compactado — 7-Zip") : tr("Compactado — 7-Zip")); resize(820, 610);
    auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel(create ? tr("Compactar %1 itens selecionados").arg(m_sources.size()) : QFileInfo(m_sources.value(0)).fileName(), this);
    auto font = heading->font(); font.setPointSize(13); font.setBold(true); heading->setFont(font); layout->addWidget(heading);
    auto *hint = new QLabel(create
        ? tr("Cria um arquivo novo e testa sua integridade com o 7-Zip. Os originais permanecem na origem; um compactado existente não será atualizado ou substituído.")
        : tr("ZIP, 7z e RAR em arquivo único. A extração cria uma pasta nova e preserva os arquivos já concluídos em caso de cancelamento. Para senhas, volumes divididos e outras opções, abra o gerenciador externo do 7-Zip."), this);
    hint->setWordWrap(true); layout->addWidget(hint);
    m_table = new QTreeWidget(this); m_table->setObjectName("archiveEntries"); m_table->setColumnCount(create ? 1 : 3);
    m_table->setHeaderLabels(create ? QStringList{tr("Origem")} : QStringList{tr("Caminho"), tr("Tamanho"), tr("Tipo")});
    m_table->setRootIsDecorated(false); m_table->setAlternatingRowColors(true); m_table->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    if (create) for (const auto &source : m_sources) { auto *row = new QTreeWidgetItem(m_table, {QFileInfo(source).fileName()}); row->setToolTip(0, source); }
    layout->addWidget(m_table, 1);
    auto *form = new QFormLayout;
    const auto first = QFileInfo(m_sources.value(0));
    auto base = m_sources.size() == 1 ? (first.isDir() ? first.fileName() : first.completeBaseName()) : tr("Arquivos");
    if (base.isEmpty()) base = tr("Arquivos");
    if (create) {
        m_format = new QComboBox(this); m_format->setObjectName("archiveFormat"); m_format->addItems({tr("ZIP"), tr("7z")}); form->addRow(tr("Formato:"), m_format);
    }
    auto *outputRow = new QHBoxLayout;
    m_output = new QLineEdit(QDir::toNativeSeparators(QDir(destinationParent).filePath(base + (create ? ".zip" : tr(" extraído")))), this);
    m_output->setObjectName("archiveOutput"); auto *browse = new QPushButton(tr("Escolher…"), this);
    outputRow->addWidget(m_output, 1); outputRow->addWidget(browse); form->addRow(create ? tr("Novo arquivo:") : tr("Nova pasta:"), outputRow);
    connect(browse, &QPushButton::clicked, this, [this] {
        if (m_job) return;
        if (m_create) {
            const auto path = QFileDialog::getSaveFileName(this, tr("Criar compactado novo"), m_output->text(), m_format->currentIndex() == 0 ? "ZIP (*.zip)" : "7z (*.7z)", nullptr, QFileDialog::DontConfirmOverwrite);
            if (!path.isEmpty()) m_output->setText(QDir::toNativeSeparators(path));
        } else {
            const auto parent = QFileDialog::getExistingDirectory(this, tr("Pasta que receberá a nova pasta de extração"), QFileInfo(m_output->text()).absolutePath());
            if (!parent.isEmpty()) m_output->setText(QDir::toNativeSeparators(QDir(parent).filePath(QFileInfo(m_output->text()).fileName())));
        }
    });
    if (m_format) connect(m_format, &QComboBox::currentIndexChanged, this, [this](int index) {
        const QFileInfo old(QDir::fromNativeSeparators(m_output->text()));
        m_output->setText(QDir::toNativeSeparators(QDir(old.absolutePath()).filePath(old.completeBaseName() + (index == 0 ? ".zip" : ".7z"))));
    });
    layout->addLayout(form);
    m_status = new QLabel(this); m_status->setObjectName("archiveStatus"); m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true); m_status->setMaximumHeight(100); layout->addWidget(m_status);
    m_details = new QLabel(this); m_details->setObjectName("archiveDetails"); m_details->setTextFormat(Qt::PlainText); m_details->setWordWrap(true); m_details->setTextInteractionFlags(Qt::TextSelectableByMouse); m_details->setMaximumHeight(100); layout->addWidget(m_details);
    m_progress = new QProgressBar(this); m_progress->setObjectName("archiveProgress"); m_progress->hide(); layout->addWidget(m_progress);
    auto *buttons = new QHBoxLayout;
    if (!create) {
        m_reload = new QPushButton(tr("Listar novamente"), this); m_reload->setObjectName("archiveList"); buttons->addWidget(m_reload);
        m_test = new QPushButton(tr("Testar integridade"), this); m_test->setObjectName("archiveTest"); buttons->addWidget(m_test);
        connect(m_reload, &QPushButton::clicked, this, [this] { start(ArchiveAction::List); });
        connect(m_test, &QPushButton::clicked, this, [this] { start(ArchiveAction::Test); });
    }
    m_reveal = new QPushButton(tr("Mostrar destino"), this); m_reveal->setObjectName("archiveReveal"); m_reveal->setEnabled(false); buttons->addWidget(m_reveal);
    m_advanced=new QPushButton(tr("Opções no 7-Zip…"),this);m_advanced->setObjectName("archiveAdvanced");buttons->addWidget(m_advanced);
    connect(m_advanced,&QPushButton::clicked,this,[this]{if(m_job)return;const auto location=m_create?QFileInfo(m_sources.value(0)).absolutePath():m_sources.value(0);const auto command=archiveManagerCommand(location);if(!command.error.isEmpty()){m_status->setText(command.error);return;}if(QMessageBox::question(this,tr("Abrir gerenciador 7-Zip"),tr("Abrir:\n%1\n\nAs opções avançadas, alterações, confirmações e cancelamento serão controlados pelo 7-Zip. Ao criar, selecione os itens na janela dele. Confira o resultado lá e liste novamente ao voltar.").arg(location),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;m_catalog={};m_status->setText(delegateCommand(archiveManagerCommand(location)).message);setBusy(false);});
    connect(m_reveal, &QPushButton::clicked, this, [this] { if (!m_lastOutput.isEmpty()) emit revealOutput(m_lastOutput); });
    buttons->addStretch();
    m_apply = new QPushButton(create ? tr("Criar compactado") : tr("Extrair"), this); m_apply->setObjectName("archiveApply"); buttons->addWidget(m_apply);
    m_cancel = new QPushButton(tr("Cancelar tarefa"), this); m_cancel->setObjectName("archiveCancel"); buttons->addWidget(m_cancel);
    auto *close = new QPushButton(tr("Fechar"), this); close->setObjectName("archiveClose"); close->setDefault(true); buttons->addWidget(close);
    connect(m_apply, &QPushButton::clicked, this, [this] { start(m_create ? ArchiveAction::Create : ArchiveAction::Extract); });
    connect(m_cancel, &QPushButton::clicked, this, [this] { if (m_job) { m_job->cancel(); m_cancel->setEnabled(false); m_status->setText(tr("Cancelando e aguardando o 7-Zip encerrar…")); } });
    connect(close, &QPushButton::clicked, this, &ArchiveDialog::reject); layout->addLayout(buttons);
    setBusy(false);
    if (!create) QTimer::singleShot(0, this, [this] { start(ArchiveAction::List); });
    else m_status->setText(tr("Confira o formato, o destino e confirme Criar compactado."));
}
ArchiveDialog::~ArchiveDialog() { if (m_job) { m_job->cancel(); m_job->wait(); } }
void ArchiveDialog::reject() {
    if (m_job) { m_closePending = true; m_job->cancel(); m_status->setText(tr("Aguardando o 7-Zip encerrar antes de fechar…")); return; }
    QDialog::reject();
}
void ArchiveDialog::setBusy(bool busy) {
    m_advanced->setEnabled(!busy);
    m_output->setEnabled(!busy); if (m_format) m_format->setEnabled(!busy);
    if (m_reload) m_reload->setEnabled(!busy);
    if (m_test) m_test->setEnabled(!busy && m_catalog.state == ArchiveState::Completed);
    m_apply->setEnabled(!busy && (m_create || m_catalog.state == ArchiveState::Completed));
    m_cancel->setEnabled(busy); m_progress->setVisible(busy); m_reveal->setEnabled(!busy && !m_lastOutput.isEmpty());
}
void ArchiveDialog::start(ArchiveAction action) {
    if (m_job) return;
    ArchiveRequest request; request.action = action; request.sources = m_sources; request.archive = m_sources.value(0);
    request.output = QDir::fromNativeSeparators(m_output->text().trimmed());
    if (m_format) request.format = m_format->currentIndex() == 0 ? ArchiveFormat::Zip : ArchiveFormat::SevenZip;
    if (action == ArchiveAction::Test || action == ArchiveAction::Extract) request.expectedIdentity = m_catalog.identity;
    const auto error = validateArchiveRequest(request); if (!error.isEmpty()) { m_status->setText(error); return; }
    m_details->clear(); m_status->setText(tr("Executando com o 7-Zip…")); m_progress->setRange(0, 0); setBusy(true);
    m_job = new ArchiveJob(request, this); auto *job = m_job;
    connect(job, &ArchiveJob::progress, this, [this](int done, int total, const QString &path) {
        if (total) { m_progress->setRange(0, total); m_progress->setValue(done); }
        m_status->setText(path.isEmpty() ? tr("Concluindo…") : path);
    });
    connect(job, &QThread::finished, this, [this, job, action] {
        job->wait(); const auto result = job->result(); m_job = nullptr; job->deleteLater();
        if (action == ArchiveAction::List) {
            m_catalog = result; m_table->clear();
            for (const auto &entry : result.entries) new QTreeWidgetItem(m_table, {entry.path, entry.directory ? QString{} : QLocale().formattedDataSize(qint64(qMin(entry.size, quint64(std::numeric_limits<qint64>::max())))), entry.directory ? tr("Pasta") : entry.encrypted ? tr("Arquivo com senha") : tr("Arquivo")});
        }
        m_status->setText(result.message.isEmpty() ? (result.state == ArchiveState::Cancelled ? tr("Tarefa cancelada.") : tr("Não foi possível concluir a tarefa.")) : result.message);
        if (!result.output.isEmpty()) m_lastOutput = result.output;
        const auto details = result.diagnostics.left(2000);
        m_details->setText((result.output.isEmpty() ? QString{} : tr("Destino: %1\nArquivos concluídos: %2\n").arg(QDir::toNativeSeparators(result.output)).arg(result.completedFiles)) + details);
        m_details->setToolTip(result.diagnostics.left(16000)); setBusy(false);
        if (m_closePending) QDialog::reject();
    });
    job->start();
}
