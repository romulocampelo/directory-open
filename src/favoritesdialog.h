#pragma once
#include <QDialog>
#include "sessionstore.h"
class QTreeWidget;
class FavoritesDialog final : public QDialog {
    Q_OBJECT
public:
    FavoritesDialog(const QVector<FavoriteEntry> &favorites, const QString &currentPath, QWidget *parent = nullptr);
    QVector<FavoriteEntry> favorites() const;
protected:
    void accept() override;
private:
    QTreeWidget *m_list;
    void append(const FavoriteEntry &entry);
    void addFolder(const QString &path);
    void moveCurrent(int offset);
};
