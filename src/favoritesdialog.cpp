#include "favoritesdialog.h"
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>

FavoritesDialog::FavoritesDialog(const QVector<FavoriteEntry> &favorites, const QString &currentPath, QWidget *parent) : QDialog(parent) {
    setObjectName("favoritesDialog"); setWindowTitle(tr("Organizar favoritos")); resize(800, 480);
    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(tr("Dê dois cliques para editar o nome ou o caminho. Remover um favorito apaga somente o atalho desta lista."), this); hint->setWordWrap(true); layout->addWidget(hint);
    m_list = new QTreeWidget(this); m_list->setObjectName("favoriteEntries"); m_list->setColumnCount(2); m_list->setHeaderLabels({tr("Nome"), tr("Pasta")});
    m_list->setRootIsDecorated(false); m_list->setSelectionMode(QAbstractItemView::SingleSelection); m_list->setColumnWidth(0, 230); m_list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (const auto &entry : favorites) append(entry); layout->addWidget(m_list, 1);
    auto *row = new QHBoxLayout;
    auto button = [&](const QString &name, const QString &label, auto fn) {
        auto *b = new QPushButton(label, this); b->setObjectName(name); row->addWidget(b); connect(b, &QPushButton::clicked, this, fn); return b;
    };
    button("addCurrentFavorite", tr("Adicionar pasta atual"), [this, currentPath] { addFolder(currentPath); });
    button("addFavoriteFolder", tr("Escolher pasta…"), [this, currentPath] { const auto path = QFileDialog::getExistingDirectory(this, tr("Adicionar favorito"), currentPath); if (!path.isEmpty()) addFolder(path); });
    auto *up = button("favoriteUp", tr("Subir"), [this] { moveCurrent(-1); });
    auto *down = button("favoriteDown", tr("Descer"), [this] { moveCurrent(1); });
    auto *remove = button("removeFavorite", tr("Remover"), [this] { delete m_list->takeTopLevelItem(m_list->indexOfTopLevelItem(m_list->currentItem())); });
    auto update = [this, up, down, remove] { const int i = m_list->indexOfTopLevelItem(m_list->currentItem()); up->setEnabled(i > 0); down->setEnabled(i >= 0 && i + 1 < m_list->topLevelItemCount()); remove->setEnabled(i >= 0); };
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, update); connect(m_list->model(), &QAbstractItemModel::rowsRemoved, this, update); update(); layout->addLayout(row);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this); buttons->button(QDialogButtonBox::Save)->setText(tr("Guardar")); buttons->button(QDialogButtonBox::Save)->setObjectName("saveFavorites"); buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    connect(buttons, &QDialogButtonBox::accepted, this, &FavoritesDialog::accept); connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject); layout->addWidget(buttons);
}
void FavoritesDialog::append(const FavoriteEntry &entry) {
    auto *item = new QTreeWidgetItem(m_list, {entry.label, QDir::toNativeSeparators(entry.path)}); item->setData(0, Qt::UserRole, entry.id);
    item->setFlags(item->flags() | Qt::ItemIsEditable); item->setToolTip(1, QDir::toNativeSeparators(entry.path));
}
void FavoritesDialog::addFolder(const QString &path) {
    if (m_list->topLevelItemCount() >= 64) { QMessageBox::information(this, tr("Favoritos"), tr("Você pode guardar até 64 favoritos.")); return; }
    const QFileInfo info(path); const auto label = info.fileName().isEmpty() ? QDir::toNativeSeparators(path) : info.fileName();
    append({QUuid::createUuid().toString(QUuid::WithoutBraces), label, QDir::cleanPath(QDir::fromNativeSeparators(path))}); m_list->setCurrentItem(m_list->topLevelItem(m_list->topLevelItemCount() - 1));
}
void FavoritesDialog::moveCurrent(int offset) {
    const int index = m_list->indexOfTopLevelItem(m_list->currentItem()); const int target = index + offset;
    if (index < 0 || target < 0 || target >= m_list->topLevelItemCount()) return;
    auto *item = m_list->takeTopLevelItem(index); m_list->insertTopLevelItem(target, item); m_list->setCurrentItem(item);
}
QVector<FavoriteEntry> FavoritesDialog::favorites() const {
    QVector<FavoriteEntry> entries;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) { const auto *item = m_list->topLevelItem(i); entries.append({item->data(0, Qt::UserRole).toString(), item->text(0), QDir::cleanPath(QDir::fromNativeSeparators(item->text(1)))}); }
    return entries;
}
void FavoritesDialog::accept() {
    const auto error = validateFavorites(favorites());
    if (!error.isEmpty()) { QMessageBox::information(this, tr("Favoritos"), error); return; }
    QDialog::accept();
}
