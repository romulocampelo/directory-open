#pragma once
#include <QDialog>
#include "folderview.h"
class QListWidget;
class FolderViewsDialog final : public QDialog {
    Q_OBJECT
public:
    explicit FolderViewsDialog(const QVector<FolderViewEntry> &entries, QWidget *parent = nullptr);
    QVector<FolderViewEntry> entries() const;
private:
    QVector<FolderViewEntry> m_original;
    QListWidget *m_list;
};
