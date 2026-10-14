#include "filetransfer.h"
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QSet>
#include <QUrl>
#include <QUuid>
#include <QTimer>
#include <QtEndian>
#include <windows.h>
#include <ole2.h>
#include <shlobj.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
namespace {
const QString tokenFormat = "application/x-navega-file-transfer-token";
QString key(const QString &path) { return QDir::cleanPath(QDir::fromNativeSeparators(path)).toCaseFolded(); }
void notifyOptimizedMove(IDataObject *object) {
    if (!object) return;
    auto set = [object](const wchar_t *format, DWORD effect) {
        FORMATETC descriptor{CLIPFORMAT(RegisterClipboardFormatW(format)), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD)); if (!memory) return;
        auto *value = static_cast<DWORD *>(GlobalLock(memory)); if (!value) { GlobalFree(memory); return; }
        *value = effect; GlobalUnlock(memory);
        STGMEDIUM data{}; data.tymed = TYMED_HGLOBAL; data.hGlobal = memory;
        if (FAILED(object->SetData(&descriptor, &data, TRUE))) ReleaseStgMedium(&data);
    };
    // IFileOperation already moved the actual files. Never request delete-on-paste.
    set(CFSTR_PERFORMEDDROPEFFECT, DROPEFFECT_NONE);
    set(CFSTR_LOGICALPERFORMEDDROPEFFECT, DROPEFFECT_MOVE);
}
}
QString preferredDropEffectFormat() { return "application/x-qt-windows-mime;value=\"Preferred DropEffect\""; }
FileTransfer readFileTransfer(const QMimeData *mime) {
    if (const auto *internal = dynamic_cast<const FileDragData *>(mime)) return internal->transfer();
    FileTransfer result;
    if (!mime || !mime->hasUrls()) { result.error = "A seleção não contém arquivos ou pastas do sistema de arquivos."; return result; }
    const auto urls = mime->urls(); QSet<QString> seen; qsizetype bytes = 0;
    // Bounds the list shown in the confirmation, not the contents of a folder.
    if (urls.size() > 16384) { result.error = "Há muitos itens individuais. Selecione a pasta que os contém."; return result; }
    for (const auto &url : urls) {
        const auto path = QDir::cleanPath(QDir::fromNativeSeparators(url.toLocalFile()));
        if (!url.isValid() || !url.isLocalFile() || url.hasQuery() || url.hasFragment() || !QDir::isAbsolutePath(path) || path.contains(QChar(0))) {
            result.paths.clear(); result.error = "Somente caminhos completos de arquivos e pastas são aceitos."; return result;
        }
        bytes += path.size() * qsizetype(sizeof(QChar));
        if (bytes > 4 * 1024 * 1024) { result.paths.clear(); result.error = "A lista de caminhos é grande demais para esta operação."; return result; }
        const auto identity = key(path); if (seen.contains(identity)) continue;
        seen.insert(identity); result.paths.append(path);
    }
    if (mime->hasFormat(preferredDropEffectFormat())) {
        const auto value = mime->data(preferredDropEffectFormat());
        // HGLOBAL allocations can include padding beyond the DWORD field.
        const auto effect = value.size() >= 4 ? qFromLittleEndian<quint32>(value.constData()) : quint32(0);
        if (effect != 1 && effect != 2) { result.paths.clear(); result.error = "A ação pedida pela área de transferência não é suportada."; return result; }
        result.kind = effect == 2 ? OperationKind::Move : OperationKind::Copy;
    }
    auto token = mime->data(tokenFormat); while (token.endsWith('\0')) token.chop(1);
    result.token = QString::fromLatin1(token); return result;
}
std::unique_ptr<QMimeData> makeFileTransfer(const QStringList &paths, OperationKind kind) {
    auto mime = std::make_unique<QMimeData>(); QList<QUrl> urls;
    for (const auto &path : paths) urls.append(QUrl::fromLocalFile(path)); mime->setUrls(urls);
    QByteArray value(4, '\0'); qToLittleEndian<quint32>(kind == OperationKind::Move ? 2 : 1, value.data());
    mime->setData(preferredDropEffectFormat(), value);
    mime->setData(tokenFormat, QUuid::createUuid().toByteArray(QUuid::WithoutBraces)); return mime;
}
FileDragData::FileDragData(const QStringList &paths) {
    const auto source = makeFileTransfer(paths, OperationKind::Copy); m_transfer = readFileTransfer(source.get());
    setData("application/x-navega-internal-drag", "1");
}
FileClipboard::FileClipboard(QObject *parent) : QObject(parent) {
    connect(qApp->clipboard(), &QClipboard::dataChanged, this, [this] {
        ++m_revision; m_cached = false;
        // Native notifications may arrive inside OLE/clipboard transactions.
        // Do not read the provider reentrantly from that window procedure.
        if (m_notificationPending) return; m_notificationPending = true;
        QTimer::singleShot(50, this, [this] { m_notificationPending = false; emit changed(); });
    });
}
ClipboardSnapshot FileClipboard::snapshot() {
    const auto sequence = GetClipboardSequenceNumber();
    const bool native = QApplication::platformName() == "windows";
    if (m_cached && m_snapshot.sequence == sequence && (native ? sequence != 0 : m_snapshot.revision == m_revision)) return m_snapshot;
    if (native) {
        if (GetOpenClipboardWindow()) {
            m_cached = false; ClipboardSnapshot unavailable; unavailable.revision = m_revision; unavailable.sequence = sequence;
            unavailable.transfer.error = "A área de transferência está ocupada."; return unavailable;
        }
    }
    m_snapshot = {}; m_snapshot.transfer = readFileTransfer(qApp->clipboard()->mimeData());
    m_snapshot.revision = m_revision;
    m_snapshot.sequence = GetClipboardSequenceNumber();
    // A temporarily unreadable native file list is not evidence of absence.
    m_cached = m_snapshot.transfer.valid() || QApplication::platformName() != "windows" || !IsClipboardFormatAvailable(CF_HDROP);
    return m_snapshot;
}
QString FileClipboard::publish(const QStringList &paths, OperationKind kind) {
    auto mime = makeFileTransfer(paths, kind); const auto expected = readFileTransfer(mime.get());
    if (!expected.valid()) return expected.error;
    if (QApplication::platformName() == "windows") {
        // Do not open/close the native clipboard as a probe: a null-window lock
        // can be part of an OLE transaction in this process.
        if (GetOpenClipboardWindow()) return "A área de transferência está ocupada. Tente novamente.";
        // Preserve any previous Qt-owned provider before Qt releases it.
        if (qApp->clipboard()->ownsClipboard() && FAILED(OleFlushClipboard())) return "A área de transferência está ocupada. Tente novamente.";
    }
    qApp->clipboard()->setMimeData(mime.release()); m_cached = false;
    if (QApplication::platformName() == "windows") {
        if (!qApp->clipboard()->ownsClipboard() || FAILED(OleFlushClipboard())) return "A área de transferência não pôde receber os arquivos. Tente novamente.";
    }
    const auto actual = snapshot().transfer;
    if (!actual.valid() || actual.token != expected.token || actual.paths != expected.paths || actual.kind != expected.kind)
        return "A área de transferência está ocupada ou não pôde receber os arquivos. Tente novamente.";
    return {};
}
QString FileClipboard::finishMove(const ClipboardSnapshot &before, const OperationResult &result) {
    if (before.transfer.kind != OperationKind::Move || !before.transfer.valid()) return {};
    const auto now = snapshot();
    const bool native = QApplication::platformName() == "windows";
    if (native && (!before.sequence || !now.sequence)) return "Os arquivos foram movidos, mas não foi possível confirmar o estado da área de transferência.";
    if (native && now.sequence == before.sequence && !now.transfer.valid())
        return "Os arquivos foram movidos, mas a área de transferência não pôde ser conferida e foi preservada.";
    // The Windows sequence owns identity. Qt can deliver our own notification
    // after the snapshot, without any new clipboard content being published.
    if ((native ? now.sequence != before.sequence : now.revision != before.revision) || now.transfer.token != before.transfer.token
        || now.transfer.paths != before.transfer.paths || now.transfer.kind != before.transfer.kind) return {};
    QSet<QString> completed;
    for (const auto &item : result.items) if (item.state == OperationItemState::Completed) completed.insert(key(item.source));
    QStringList pending; for (const auto &path : before.transfer.paths) if (!completed.contains(key(path))) pending.append(path);
    if (pending.size() == before.transfer.paths.size()) return {};
    if (!pending.isEmpty()) return publish(pending, OperationKind::Move);
    if (QApplication::platformName() == "windows") {
        if (GetOpenClipboardWindow()) return "Os arquivos foram movidos, mas a área de transferência está ocupada e não pôde ser atualizada.";
    }
    // Acquire the source only for the immediate acknowledgement, never cache a
    // native data object throughout navigation or a potentially long operation.
    ComPtr<IDataObject> origin;
    if (QApplication::platformName() == "windows") OleGetClipboard(&origin);
    qApp->clipboard()->clear(); m_cached = false;
    if (snapshot().transfer.valid()) return "Os arquivos foram movidos, mas a área de transferência não pôde ser limpa. Confira os itens antes de colar novamente.";
    notifyOptimizedMove(origin.Get()); return {};
}
