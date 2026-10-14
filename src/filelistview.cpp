#include "filelistview.h"
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QPainter>
#include <QTimer>

template<class Base> FileTransferView<Base>::FileTransferView(QWidget *parent) : Base(parent) {
    configureTransferInteraction();
}
template<class Base> void FileTransferView<Base>::configureTransferInteraction() {
    this->setDragDropMode(QAbstractItemView::DragDrop); this->setDefaultDropAction(Qt::CopyAction);
    this->setDropIndicatorShown(false); this->setDragEnabled(true); this->setAcceptDrops(true);
    // Icon-mode setup can reset the viewport independently of the view flag.
    this->viewport()->setAcceptDrops(true);
}
template<class Base> void FileTransferView<Base>::startDrag(Qt::DropActions) {
    if (!transferEnabled || !selectedFiles) return;
    const auto paths = selectedFiles(); if (paths.isEmpty()) return;
    QDrag drag(this); auto *mime = new FileDragData(paths);
    if (!mime->transfer().valid()) { delete mime; return; }
    drag.setMimeData(mime);
    QPixmap icon(170, 38); icon.fill(this->palette().color(QPalette::Base)); QPainter paint(&icon);
    paint.setPen(this->palette().color(QPalette::Text)); paint.drawText(icon.rect().adjusted(8, 0, -8, 0), Qt::AlignCenter, paths.size() == 1 ? this->tr("1 item") : this->tr("%1 itens").arg(paths.size())); paint.end();
    drag.setPixmap(icon); drag.exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
    // The operation worker owns the transfer. Never removeRows after MoveAction.
}
template<class Base> Qt::DropAction FileTransferView<Base>::actionFor(const QMimeData *mime, Qt::DropActions possible, Qt::KeyboardModifiers modifiers) const {
    if (!transferEnabled || (modifiers & Qt::AltModifier) || ((modifiers & Qt::ShiftModifier) && (modifiers & Qt::ControlModifier))) return Qt::IgnoreAction;
    const bool internal = dynamic_cast<const FileDragData *>(mime) != nullptr;
    if (internal && (modifiers & Qt::ShiftModifier)) return possible.testFlag(Qt::MoveAction) ? Qt::MoveAction : Qt::IgnoreAction;
    // External sources must keep originals while our confirmation/job is pending.
    return possible.testFlag(Qt::CopyAction) ? Qt::CopyAction : Qt::IgnoreAction;
}
template<class Base> void FileTransferView<Base>::dragEnterEvent(QDragEnterEvent *event) {
    m_dragMime = event->mimeData(); m_dragTransfer = readFileTransfer(m_dragMime);
    const auto action = actionFor(m_dragMime, event->possibleActions(), event->modifiers());
    if (!m_dragTransfer.valid() || action == Qt::IgnoreAction) { event->ignore(); return; }
    event->setDropAction(action); event->accept();
}
template<class Base> void FileTransferView<Base>::dragMoveEvent(QDragMoveEvent *event) {
    if (m_dragMime != event->mimeData()) { m_dragMime = event->mimeData(); m_dragTransfer = readFileTransfer(m_dragMime); }
    const auto action = actionFor(m_dragMime, event->possibleActions(), event->modifiers());
    if (!m_dragTransfer.valid() || action == Qt::IgnoreAction) { event->ignore(); return; }
    event->setDropAction(action); event->accept();
}
template<class Base> void FileTransferView<Base>::dragLeaveEvent(QDragLeaveEvent *event) { m_dragMime = nullptr; m_dragTransfer = {}; event->accept(); }
template<class Base> void FileTransferView<Base>::dropEvent(QDropEvent *event) {
    auto transfer = readFileTransfer(event->mimeData()); const auto action = actionFor(event->mimeData(), event->possibleActions(), event->modifiers());
    m_dragMime = nullptr; m_dragTransfer = {};
    if (!transfer.valid() || action == Qt::IgnoreAction || !destinationAt || !receiveFiles) { event->ignore(); return; }
    const auto destination = destinationAt(event->position().toPoint()); if (destination.isEmpty()) { event->ignore(); return; }
    transfer.kind = action == Qt::MoveAction ? OperationKind::Move : OperationKind::Copy;
    event->setDropAction(action); event->accept();
    // Leave the native drag loop before showing a modal confirmation or doing work.
    QTimer::singleShot(0, this, [this, transfer, destination] { if (transferEnabled && receiveFiles) receiveFiles(transfer, destination); });
}

template class FileTransferView<QTreeView>;
template class FileTransferView<QListView>;
