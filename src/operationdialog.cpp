#include "operationdialog.h"
#include "teracopy.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

OperationDialog::OperationDialog(OperationRequest request, QWidget *parent) : QDialog(parent), m_initial(std::move(request)) {
    const auto kind = m_initial.kind;
    const bool transfer = kind == OperationKind::Copy || kind == OperationKind::Move;
    QString verb, hint;
    switch (kind) {
    case OperationKind::Copy: verb = tr("Copiar"); hint = tr("Os originais permanecem na origem. O Windows mostrará o progresso e permitirá cancelar a cópia."); break;
    case OperationKind::Move: verb = tr("Mover"); hint = tr("Os itens concluídos saem da origem. Cancelar conserva as movimentações já concluídas."); break;
    case OperationKind::Rename: verb = tr("Renomear"); hint = tr("O item permanece na mesma pasta. Um nome existente não será substituído."); break;
    case OperationKind::CreateFolder: verb = tr("Criar pasta"); hint = tr("A nova pasta será criada no painel ativo."); break;
    case OperationKind::Trash: verb = tr("Enviar à Lixeira"); hint = tr("Você poderá restaurar os itens pela Lixeira do Windows. Se a reciclagem não estiver disponível, a operação apresentará um erro. O cancelamento ocorre entre itens selecionados."); break;
    }
    setObjectName("operationDialog"); setWindowTitle(verb); resize(650, kind == OperationKind::CreateFolder ? 240 : 440);
    auto *layout = new QVBoxLayout(this);
    const auto count = m_initial.sources.size();
    auto *heading = new QLabel(kind == OperationKind::CreateFolder ? verb : verb + (count == 1 ? tr(" — 1 item") : tr(" — %1 itens").arg(count)), this);
    auto font = heading->font(); font.setPointSize(13); font.setBold(true); heading->setFont(font); layout->addWidget(heading);
    if (kind != OperationKind::CreateFolder) {
        auto *list = new QListWidget(this); list->setObjectName("operationSources");
        for (const auto &source : m_initial.sources) {
            auto *item = new QListWidgetItem(QFileInfo(source).fileName(), list); item->setToolTip(QDir::toNativeSeparators(source));
        }
        layout->addWidget(list, 1);
    }
    auto *form = new QFormLayout;
    if (transfer) {
        auto *row = new QHBoxLayout; m_destination = new QLineEdit(QDir::toNativeSeparators(m_initial.destination), this);
        m_destination->setObjectName("operationDestination"); m_destination->setAccessibleName(tr("Pasta de destino"));
        auto *browse = new QPushButton(tr("Escolher…"), this);
        connect(browse, &QPushButton::clicked, this, [this] {
            const auto path = QFileDialog::getExistingDirectory(this, tr("Escolher destino"), m_destination->text());
            if (!path.isEmpty()) m_destination->setText(QDir::toNativeSeparators(path));
        });
        row->addWidget(m_destination, 1); row->addWidget(browse); form->addRow(tr("Destino:"), row);
        m_conflict = new QComboBox(this); m_conflict->setObjectName("operationConflict");
        m_conflict->addItem(tr("Perguntar quando um nome já existir"), int(OperationConflict::Ask));
        m_conflict->addItem(tr("Manter os dois, criando outro nome"), int(OperationConflict::KeepBoth));
        m_conflict->setCurrentIndex(m_initial.conflict == OperationConflict::KeepBoth ? 1 : 0);
        form->addRow(tr("Nomes repetidos:"), m_conflict);
        if (kind == OperationKind::Copy) {
            m_engine = new QComboBox(this); m_engine->setObjectName("operationEngine");
            m_engine->addItem(tr("Windows"), int(TransferEngine::Windows));
            const auto executable = teraCopyExecutable();
            m_engine->addItem(executable.isEmpty() ? tr("TeraCopy — não encontrado") : tr("TeraCopy"), int(TransferEngine::TeraCopy));
            m_engine->setItemData(1, QDir::toNativeSeparators(executable), Qt::ToolTipRole);
            if (executable.isEmpty()) static_cast<QStandardItemModel *>(m_engine->model())->item(1)->setEnabled(false);
            // Preserve an explicit preference even if the provider disappeared: the
            // confirmation reports it, and the user can choose Windows explicitly.
            m_engine->setCurrentIndex(m_initial.engine == TransferEngine::TeraCopy ? 1 : 0);
            form->insertRow(0, tr("Copiar com:"), m_engine);
        }
    } else if (kind != OperationKind::Trash) {
        const auto folder = kind == OperationKind::Rename ? QFileInfo(m_initial.sources.first()).absolutePath() : m_initial.destination;
        auto *path = new QLabel(QDir::toNativeSeparators(folder), this); path->setWordWrap(true); path->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(tr("Pasta:"), path);
        m_name = new QLineEdit(m_initial.name, this); m_name->setObjectName("operationName"); m_name->setAccessibleName(tr("Novo nome"));
        form->addRow(tr("Nome:"), m_name); m_name->setFocus();
        const int dot = m_initial.name.lastIndexOf('.');
        if (kind == OperationKind::Rename && QFileInfo(m_initial.sources.first()).isFile() && dot > 0) m_name->setSelection(0, dot);
        else m_name->selectAll();
    }
    layout->addLayout(form);
    auto *description = new QLabel(hint, this); description->setObjectName("operationHint"); description->setWordWrap(true);
    description->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum); layout->addWidget(description);
    if (m_engine) {
        const auto updateEngine = [this, description, hint] {
            const bool external = m_engine->currentData().toInt() == int(TransferEngine::TeraCopy);
            m_conflict->setEnabled(!external);
            if (external) m_conflict->setCurrentIndex(1);
            description->setText(external ? tr("O TeraCopy controla progresso, cancelamento e verificação na janela dele. Confira o resultado nessa janela; o DirOpen informa somente o encaminhamento. Arquivos existentes serão preservados e sua escolha será lembrada.") : hint);
            if (external) resize(width(), qMax(height(), 520));
        };
        connect(m_engine, &QComboBox::currentIndexChanged, this, updateEngine); updateEngine();
    }
    m_error = new QLabel(this); m_error->setObjectName("operationError"); m_error->setWordWrap(true); m_error->setStyleSheet("color: #c95248;"); layout->addWidget(m_error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    auto *confirm = buttons->addButton(verb, QDialogButtonBox::AcceptRole); confirm->setObjectName("confirmOperation");
    if (kind == OperationKind::Trash) { confirm->setAutoDefault(false); buttons->button(QDialogButtonBox::Cancel)->setDefault(true); buttons->button(QDialogButtonBox::Cancel)->setFocus(); }
    else confirm->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(confirm, &QPushButton::clicked, this, [this] {
        const auto selected = this->request();
        auto error = validateOperationRequest(selected);
        if (error.isEmpty() && selected.engine == TransferEngine::TeraCopy && teraCopyExecutable().isEmpty()) error = tr("TeraCopy não encontrado. Escolha Windows para copiar.");
        m_error->setText(error);
        if (error.isEmpty()) accept();
    });
    layout->addWidget(buttons);
}
OperationRequest OperationDialog::request() const {
    auto request = m_initial;
    if (m_destination) request.destination = QDir::fromNativeSeparators(m_destination->text().trimmed());
    if (m_conflict) request.conflict = OperationConflict(m_conflict->currentData().toInt());
    if (m_name) request.name = m_name->text();
    if (m_engine) request.engine = TransferEngine(m_engine->currentData().toInt());
    return request;
}
