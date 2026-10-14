#include "folderviewsdialog.h"
#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

FolderViewsDialog::FolderViewsDialog(const QVector<FolderViewEntry> &entries, QWidget *parent)
    : QDialog(parent), m_original(entries) {
    setObjectName("folderViewsDialog"); setWindowTitle(tr("Exibições guardadas")); resize(620, 400);
    auto *layout = new QVBoxLayout(this);
    auto *explanation = new QLabel(tr("Selecione pastas para esquecer suas exibições. Isso não remove arquivos.\nAs alterações são aplicadas ao clicar em Guardar e salvas ao fechar o Directory Open."), this);
    explanation->setWordWrap(true); layout->addWidget(explanation);
    m_list = new QListWidget(this); m_list->setObjectName("savedFolderViews"); m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (qsizetype i = 0; i < entries.size(); ++i) {
        auto *item = new QListWidgetItem(QDir::toNativeSeparators(entries[i].path), m_list);
        item->setData(Qt::UserRole, int(i)); item->setToolTip(item->text());
    }
    layout->addWidget(m_list, 1);
    auto *remove = new QPushButton(tr("Esquecer selecionadas"), this); remove->setObjectName("removeFolderViews"); remove->setEnabled(false); layout->addWidget(remove);
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this, remove] { remove->setEnabled(!m_list->selectedItems().isEmpty()); });
    connect(remove, &QPushButton::clicked, this, [this] { for (auto *item : m_list->selectedItems()) delete item; });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Guardar")); buttons->button(QDialogButtonBox::Save)->setObjectName("saveFolderViews");
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar")); buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}
QVector<FolderViewEntry> FolderViewsDialog::entries() const {
    QVector<FolderViewEntry> entries;
    for (int i = 0; i < m_list->count(); ++i) entries.append(m_original[m_list->item(i)->data(Qt::UserRole).toInt()]);
    return entries;
}
