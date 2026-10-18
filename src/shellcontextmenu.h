#pragma once
#include <QHash>
#include <QPoint>
#include <QPointer>
#include <QStringList>
#include <functional>
#include <memory>

class QAction;
class QMenu;
class QWidget;

struct ShellMenuResult {
    QPointer<QAction> action;
    bool invoked = false;
    QString error;
};

// Owns the Shell selection, COM apartment, menu and message forwarding together.
// Application actions are returned for dispatch after this owner is destroyed.
class ShellContextMenu final {
public:
    static std::unique_ptr<ShellContextMenu> create(QWidget *owner, const QStringList &selection,
                                                   const QString &folder, bool extended, QString &error);
    ~ShellContextMenu();
    quintptr nativeMenu() const;
    QString commandVerb(quint32 command) const;
    bool appendApplicationMenu(const QMenu &menu, QString &error);
    quint32 track(const QPoint &globalPosition, QString &error);
    ShellMenuResult dispatch(quint32 command, const QHash<QString, QAction *> &routes,
                             const std::function<void()> &beforeExternal = {});
private:
    ShellContextMenu();
    struct Data;
    std::unique_ptr<Data> m_data;
};
