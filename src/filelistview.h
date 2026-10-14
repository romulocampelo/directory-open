#pragma once
#include "filetransfer.h"
#include <QTreeView>
#include <QListView>
#include <functional>

// One transfer policy for table and thumbnail presentations. Models stay read-only.
template<class Base> class FileTransferView : public Base {
public:
    explicit FileTransferView(QWidget *parent = nullptr);
    std::function<QStringList()> selectedFiles;
    std::function<QString(const QPoint &)> destinationAt;
    std::function<void(FileTransfer, const QString &)> receiveFiles;
    bool transferEnabled = true;
    void configureTransferInteraction();
protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
private:
    const QMimeData *m_dragMime = nullptr;
    FileTransfer m_dragTransfer;
    Qt::DropAction actionFor(const QMimeData *mime, Qt::DropActions possible, Qt::KeyboardModifiers modifiers) const;
};
class FileListView final : public FileTransferView<QTreeView> {
public: using FileTransferView<QTreeView>::FileTransferView;
};
class FileIconView final : public FileTransferView<QListView> {
public: using FileTransferView<QListView>::FileTransferView;
};
