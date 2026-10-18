#include "toolsdialog.h"
#include "specializedlaunchdialog.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

ToolsDialog::ToolsDialog(ToolPage page, ToolSettings settings, const QString &root, const QStringList &selection, QWidget *parent)
    : QDialog(parent), m_page(page), m_settings(std::move(settings)), m_sources(selection) {
    setObjectName("toolsDialog"); resize(980, 740);
    const auto name = page == ToolPage::Content ? QString("rg") : page == ToolPage::Backup ? QString("restic") : QString("rclone");
    setWindowTitle(page == ToolPage::Content ? tr("Buscar conteúdo — ripgrep") : page == ToolPage::Backup ? tr("Backups com versões — restic") : tr("Armazenamento remoto — rclone"));
    auto *layout = new QVBoxLayout(this);
    m_form = new QWidget(this); auto *form = new QFormLayout(m_form); layout->addWidget(m_form);
    auto pathField = [&](const QString &label, const QString &value, const QString &object, bool file) {
        auto *row = new QWidget(m_form); auto *box = new QHBoxLayout(row); box->setContentsMargins(0, 0, 0, 0);
        auto *edit = new QLineEdit(value, row); edit->setObjectName(object); auto *browse = new QPushButton(tr("Escolher…"), row);
        box->addWidget(edit); box->addWidget(browse); form->addRow(label, row);
        connect(browse, &QPushButton::clicked, this, [this, edit, file] {
            const auto p = file ? QFileDialog::getOpenFileName(this, tr("Escolher arquivo"), edit->text()) : QFileDialog::getExistingDirectory(this, tr("Escolher pasta existente"), edit->text());
            if (!p.isEmpty()) edit->setText(p);
        });
        return edit;
    };
    const auto configured = page == ToolPage::Content ? m_settings.ripgrep : page == ToolPage::Backup ? m_settings.restic : m_settings.rclone;
    m_exe = pathField(tr("Programa:"), configured.isEmpty() ? discoverTool(name) : configured, "toolExecutable", true);
    QString description;
    if (page == ToolPage::Content) {
        m_root = pathField(tr("Pasta:"), root, "contentRoot", false);
        m_pattern = new QLineEdit(m_form); m_pattern->setObjectName("contentPattern"); form->addRow(tr("Procurar:"), m_pattern);
        auto *options = new QWidget(m_form); auto *optionsBox = new QHBoxLayout(options); optionsBox->setContentsMargins(0, 0, 0, 0);
        m_regex = new QCheckBox(tr("Expressão regular"), options); m_case = new QCheckBox(tr("Distinguir maiúsculas"), options);
        m_hidden = new QCheckBox(tr("Incluir ocultos"), options); m_ignored = new QCheckBox(tr("Incluir ignorados (.gitignore etc.)"), options);
        m_regex->setObjectName("contentRegex"); m_hidden->setObjectName("contentHidden"); m_ignored->setObjectName("contentIgnored");
        for (auto *c : {m_regex, m_case, m_hidden, m_ignored}) optionsBox->addWidget(c);
        form->addRow(options);
        description = tr("Texto e código, incluindo subpastas. Por padrão respeita exclusões e oculta arquivos ocultos. Não segue links. Binários, arquivos acima de 64 MiB e conteúdo de PDF/Office não têm cobertura completa. Duplo clique abre o arquivo.");
    } else if (page == ToolPage::Backup) {
        m_repo = pathField(tr("Repositório local:"), m_settings.repository, "backupRepository", false);
        m_repo->setPlaceholderText(tr("Para criar: digite o caminho de uma pasta que ainda não exista"));
        m_password = new QLineEdit(m_form); m_password->setObjectName("backupPassword"); m_password->setEchoMode(QLineEdit::Password); form->addRow(tr("Senha (não é salva):"), m_password);
        auto *sources = new QPlainTextEdit(m_sources.join('\n'), m_form); sources->setReadOnly(true); sources->setMaximumHeight(70); sources->setObjectName("backupSources"); form->addRow(tr("Seleção para backup:"), sources);
        m_restore = new QLineEdit(QDir(root).filePath("Restauracao-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)), m_form);
        m_restore->setObjectName("restoreDestination"); form->addRow(tr("Restaurar em pasta nova:"), m_restore);
        description = tr("Versões criptografadas com deduplicação. Inicialize um repositório novo ou liste as versões de um existente. Selecione uma versão para restaurar a cópia completa em pasta nova, com verificação. Guarde a senha: ela é necessária para recuperar seus arquivos. Cancelar pode deixar dados parciais; versões não são apagadas automaticamente.");
    } else {
        m_config = pathField(tr("Configuração rclone:"), m_settings.rcloneConfig, "rcloneConfig", true);
        auto *setup=new QPushButton(tr("Configurar / autorizar contas…"),m_form);setup->setObjectName("configureRemoteAccounts");form->addRow(setup);
        connect(setup,&QPushButton::clicked,this,[this]{if(m_job)return;RemoteSetupDialog dialog(m_exe->text(),m_config->text(),this);dialog.exec();m_exe->setText(dialog.program());m_config->setText(dialog.config());m_previewKey.clear();m_catalogKey.clear();m_results->clear();m_status->setText(tr("Ao terminar no rclone, liste novamente as conexões. Abrir o assistente não comprova autorização."));});
        m_remote = new QLineEdit(m_form); m_remote->setObjectName("remotePath"); m_remote->setPlaceholderText(tr("nome:pasta — escolha uma conexão listada abaixo")); form->addRow(tr("Local remoto:"), m_remote);
        m_local = pathField(tr("Pasta local:"), root, "transferLocal", false);
        m_direction = new QComboBox(m_form); m_direction->setObjectName("transferDirection"); m_direction->addItems({tr("Enviar conteúdo da pasta local → remoto"), tr("Receber conteúdo remoto → pasta local")}); form->addRow(tr("Direção:"), m_direction);
        description = tr("Use conexões já autorizadas em um rclone.conf. O DirOpen não lê nem salva as credenciais desse arquivo; o rclone pode atualizar seus tokens. Copia o conteúdo da pasta, sem acrescentar o nome dela. Faça a prévia e confirme o destino. Arquivos diferentes existentes causam erro, sem substituição; não há sincronização com exclusões. Configurações criptografadas/interativas ainda não são suportadas.");
        const auto invalidate = [this] { m_previewKey.clear(); if (m_copy) m_copy->setEnabled(false); };
        for (auto *e : {m_exe, m_config, m_local, m_remote}) connect(e, &QLineEdit::textChanged, this, invalidate);
        connect(m_direction, &QComboBox::currentIndexChanged, this, invalidate);
    }
    auto *help = new QLabel(description, this); help->setWordWrap(true); help->setTextFormat(Qt::PlainText); layout->addWidget(help);
    m_commands = new QWidget(this); auto *buttons = new QHBoxLayout(m_commands); buttons->setContentsMargins(0, 0, 0, 0); layout->addWidget(m_commands);
    auto button = [&](const QString &label, const QString &object, ToolAction action) {
        auto *b = new QPushButton(label, m_commands); b->setObjectName(object); buttons->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, action] { start(action); }); return b;
    };
    if (page == ToolPage::Content) {
        button(tr("Buscar"), "runContentSearch", ToolAction::ContentSearch);
        connect(m_pattern, &QLineEdit::returnPressed, this, [this] { start(ToolAction::ContentSearch); });
        auto *folder = new QPushButton(tr("Abrir pasta do resultado"), m_commands); buttons->addWidget(folder);
        connect(folder, &QPushButton::clicked, this, [this] { if (auto *i = m_results->currentItem()) emit openFolderRequested(QFileInfo(i->data(0, Qt::UserRole).toString()).absolutePath()); });
    } else if (page == ToolPage::Backup) {
        button(tr("Inicializar repositório…"), "initializeBackup", ToolAction::InitBackup);
        button(tr("Listar versões"), "listSnapshots", ToolAction::Snapshots);
        button(tr("Backup da seleção…"), "runBackup", ToolAction::Backup);
        button(tr("Restaurar versão…"), "restoreSnapshot", ToolAction::Restore);
    } else {
        button(tr("Listar conexões"), "listRemotes", ToolAction::Remotes);
        button(tr("Abrir local remoto"), "listRemoteFiles", ToolAction::RemoteList);
        button(tr("Prévia da cópia"), "previewTransfer", ToolAction::TransferPreview);
        m_copy = button(tr("Copiar…"), "copyRemote", ToolAction::TransferCopy); m_copy->setEnabled(false);
    }
    m_results = new QTreeWidget(this); m_results->setObjectName("toolResults"); m_results->setRootIsDecorated(false);
    m_results->setHeaderLabels(page == ToolPage::Content ? QStringList{tr("Arquivo"), tr("Linha"), tr("Trecho")} : page == ToolPage::Backup ? QStringList{tr("Versão"), tr("Data"), tr("Origens")} : QStringList{tr("Nome"), tr("Tipo / tamanho"), tr("Data")});
    m_results->header()->setSectionResizeMode(QHeaderView::Interactive); m_results->setColumnWidth(0, 340); m_results->setColumnWidth(1, 180); layout->addWidget(m_results, 1);
    connect(m_results, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        if (m_job) return;
        const auto key = item->data(0, Qt::UserRole).toString();
        if (m_page == ToolPage::Content) emit openFileRequested(key);
        if (m_page == ToolPage::Remote && m_lastAction == ToolAction::Remotes) m_remote->setText(key);
        else if (m_page == ToolPage::Remote && m_lastAction == ToolAction::RemoteList && item->data(0, Qt::UserRole + 1).toBool()) {
            if (key.contains('/') || key.contains('\\') || key == "." || key == "..") return;
            auto base = m_remote->text(); if (!base.endsWith(':') && !base.endsWith('/')) base += '/';
            m_remote->setText(base + key); start(ToolAction::RemoteList);
        }
    });
    m_diagnostics = new QPlainTextEdit(this); m_diagnostics->setReadOnly(true); m_diagnostics->setMaximumHeight(100); m_diagnostics->setObjectName("toolDiagnostics"); m_diagnostics->hide(); layout->addWidget(m_diagnostics);
    m_status = new QLabel(tr("Pronto. Nenhuma tarefa executada."), this); m_status->setObjectName("toolStatus"); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
    auto *bottom = new QHBoxLayout; layout->addLayout(bottom); bottom->addStretch();
    m_cancel = new QPushButton(tr("Cancelar tarefa"), this); m_cancel->setObjectName("cancelTool"); m_cancel->setEnabled(false); bottom->addWidget(m_cancel);
    connect(m_cancel, &QPushButton::clicked, this, [this] { if (m_job) { m_job->cancel(); m_cancel->setEnabled(false); m_status->setText(tr("Aguardando o encerramento da ferramenta…")); } });
    auto *close = new QPushButton(tr("Fechar"), this); bottom->addWidget(close); connect(close, &QPushButton::clicked, this, &ToolsDialog::reject);
}
ToolsDialog::~ToolsDialog() { if (m_job) { m_job->cancel(); m_job->wait(); delete m_job; } }
ToolSettings ToolsDialog::settings() const {
    auto s = m_settings;
    if (m_page == ToolPage::Content) s.ripgrep = m_exe->text();
    if (m_page == ToolPage::Backup) { s.restic = m_exe->text(); s.repository = m_repo->text(); }
    if (m_page == ToolPage::Remote) { s.rclone = m_exe->text(); s.rcloneConfig = m_config->text(); }
    return s;
}
ToolRequest ToolsDialog::request(ToolAction action) const {
    ToolRequest r; r.action = action; r.executable = m_exe->text();
    if (m_page == ToolPage::Content) {
        r.root = m_root->text(); r.pattern = m_pattern->text(); r.regex = m_regex->isChecked(); r.caseSensitive = m_case->isChecked(); r.hidden = m_hidden->isChecked(); r.ignored = m_ignored->isChecked();
    } else if (m_page == ToolPage::Backup) {
        r.repository = m_repo->text(); r.password = m_password->text(); r.sources = m_sources; r.destination = m_restore->text();
        if (auto *i = m_results->currentItem()) r.snapshot = i->data(0, Qt::UserRole).toString();
    } else {
        r.config = m_config->text();
        r.source = action == ToolAction::Remotes || action == ToolAction::RemoteList || m_direction->currentIndex() == 1 ? m_remote->text() : m_local->text();
        r.destination = m_direction->currentIndex() == 1 ? m_local->text() : m_remote->text();
    }
    return r;
}
QString ToolsDialog::transferKey() const {
    return m_exe->text() + QChar(0) + m_config->text() + QChar(0) + m_local->text() + QChar(0) + m_remote->text() + QChar(0) + QString::number(m_direction->currentIndex());
}
void ToolsDialog::start(ToolAction action) {
    if (m_job) return;
    auto r = request(action); const auto invalid = validateToolRequest(r);
    if (!invalid.isEmpty()) { m_status->setText(invalid); return; }
    if (action == ToolAction::Restore && m_catalogKey != m_exe->text() + QChar(0) + m_repo->text()) { m_status->setText(tr("Liste novamente as versões deste repositório antes de restaurar.")); return; }
    if (action == ToolAction::TransferCopy && m_previewKey != transferKey()) { m_status->setText(tr("Faça a prévia desta transferência antes de copiar.")); return; }
    QString confirmation;
    if (action == ToolAction::InitBackup) {
        bool ok = false; const auto password = QInputDialog::getText(this, tr("Confirmar senha"), tr("Repita a senha do novo repositório:"), QLineEdit::Password, {}, &ok);
        if (!ok) return; if (password != r.password) { m_status->setText(tr("As senhas não coincidem.")); return; }
        confirmation = tr("Criar repositório criptografado em:\n%1\n\nGuarde a senha em local seguro.").arg(r.repository);
    }
    if (action == ToolAction::Backup) confirmation = tr("Criar uma versão no repositório:\n%1\n\nOrigens:\n%2").arg(r.repository, r.sources.join('\n'));
    if (action == ToolAction::Restore) confirmation = tr("Restaurar a versão %1\nDo repositório: %2\nEm pasta nova: %3\n\nOs arquivos serão verificados. Uma interrupção pode deixar conteúdo parcial.").arg(r.snapshot, r.repository, r.destination);
    if (action == ToolAction::TransferCopy) confirmation = tr("Copiar o conteúdo da pasta:\n%1\nPara:\n%2\n\nA prévia é indicativa; os locais serão relidos. Arquivos diferentes existentes não serão substituídos. Cancelar preserva itens já copiados.").arg(r.source, r.destination);
    if (!confirmation.isEmpty() && QMessageBox::question(this, tr("Confirmar tarefa"), confirmation, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    m_lastAction = action;
    if (action != ToolAction::Restore) m_results->clear();
    m_diagnostics->clear(); m_status->setText(tr("Executando…")); m_form->setEnabled(false); m_commands->setEnabled(false); m_cancel->setEnabled(true);
    m_job = new ToolJob(std::move(r), this);
    connect(m_job, &ToolJob::progress, m_status, &QLabel::setText);
    connect(m_job, &QThread::finished, this, &ToolsDialog::finish); m_job->start();
}
void ToolsDialog::finish() {
    m_job->wait(); const auto result = m_job->result(); delete m_job; m_job = nullptr;
    m_form->setEnabled(true); m_commands->setEnabled(true); m_cancel->setEnabled(false);
    m_status->setText(result.message + tr(" (%1 s)").arg(double(result.elapsedMs) / 1000, 0, 'f', 2));
    m_diagnostics->setPlainText(result.diagnostics);
    m_diagnostics->setVisible(!result.diagnostics.isEmpty());
    if (m_lastAction != ToolAction::Restore) {
        for (const auto &row : result.rows) {
            QStringList cells;
            if (m_page == ToolPage::Content) cells = {row.key, QString::number(row.number), row.text};
            else if (m_page == ToolPage::Backup) {
                const auto date = QDateTime::fromString(row.detail, Qt::ISODateWithMs);
                cells = {row.key.left(12), date.isValid() ? date.toLocalTime().toString("dd/MM/yyyy HH:mm:ss") : row.detail, row.text};
            }
            else cells = {row.key, m_lastAction == ToolAction::Remotes ? tr("Conexão") : row.directory ? tr("Pasta") : tr("%1 bytes").arg(row.number), row.detail};
            auto *item = new QTreeWidgetItem(m_results, cells); item->setData(0, Qt::UserRole, row.key); item->setData(0, Qt::UserRole + 1, row.directory);
            item->setToolTip(0, row.key); item->setToolTip(1, row.detail); item->setToolTip(2, row.text);
        }
    }
    if (m_lastAction == ToolAction::Snapshots) m_catalogKey = result.state == ToolState::Completed ? m_exe->text() + QChar(0) + m_repo->text() : QString();
    if (m_lastAction == ToolAction::TransferPreview) m_previewKey = result.state == ToolState::Completed ? transferKey() : QString();
    if (m_lastAction == ToolAction::TransferCopy) m_previewKey.clear();
    if (m_copy) m_copy->setEnabled(!m_previewKey.isEmpty());
    if (m_closing) QDialog::reject();
}
void ToolsDialog::reject() {
    if (m_job) { m_closing = true; m_job->cancel(); m_cancel->setEnabled(false); m_status->setText(tr("Aguardando o encerramento da ferramenta…")); return; }
    QDialog::reject();
}
void ToolsDialog::closeEvent(QCloseEvent *event) { if (m_job) { event->ignore(); reject(); } else QDialog::closeEvent(event); }
