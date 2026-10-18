#include "fileoperation.h"
#include "teracopy.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QRegularExpression>
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <sherrors.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace {
QString normalized(const QString &path) { return QDir::cleanPath(QDir::fromNativeSeparators(path)); }
bool same(const QString &a, const QString &b) { return normalized(a).compare(normalized(b), Qt::CaseInsensitive) == 0; }
bool inside(const QString &path, const QString &root) {
    const auto base = normalized(root);
    return same(path, base) || normalized(path).startsWith(base.endsWith('/') ? base : base + '/', Qt::CaseInsensitive);
}
bool link(const QString &path) { const QFileInfo info(path); return info.isSymLink() || info.isJunction(); }
QString shellPath(IShellItem *item) {
    PWSTR name = nullptr;
    if (!item || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) return {};
    const auto result = normalized(QString::fromWCharArray(name)); CoTaskMemFree(name); return result;
}
QString errorText(HRESULT hr) {
    PWSTR message = nullptr;
    const DWORD size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, DWORD(hr), 0, reinterpret_cast<LPWSTR>(&message), 0, nullptr);
    const auto text = size ? QString::fromWCharArray(message).trimmed() : QString("Erro do Windows 0x%1").arg(quint32(hr), 8, 16, QChar('0'));
    if (message) LocalFree(message); return text;
}
bool cancellation(HRESULT hr) { return hr == COPYENGINE_E_USER_CANCELLED || hr == HRESULT_FROM_WIN32(ERROR_CANCELLED) || hr == E_ABORT; }

class OperationSink final : public IFileOperationProgressSink {
public:
    OperationSink(const OperationRequest &request, OperationResult &result, const std::atomic_bool &cancel, const OperationProgress &progress)
        : m_request(request), m_result(result), m_cancel(cancel), m_progress(progress) {
        m_destination = QFileInfo(request.kind == OperationKind::Rename ? QFileInfo(request.sources.first()).absolutePath() : request.destination).canonicalFilePath();
        for (const auto &item : result.items) if (QFileInfo(item.source).isDir()) m_directories.insert(item.source.toCaseFolded());
        for (int i = 0; i < result.items.size(); ++i) m_roots.insert(normalized(result.items[i].source).toCaseFolded(), i);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_IFileOperationProgressSink) { *out = static_cast<IFileOperationProgressSink *>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG left = --m_refs; if (!left) delete this; return left; }
    HRESULT STDMETHODCALLTYPE StartOperations() override { return checkCancel(); }
    HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT hr) override { finishCode = hr; return S_OK; }
    HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD, IShellItem *item, IShellItem *folder, LPCWSTR name) override {
        if (FAILED(checkCancel())) return COPYENGINE_E_USER_CANCELLED;
        const auto source = shellPath(item); const auto destination = shellPath(folder);
        const auto resolved = QFileInfo(destination).canonicalFilePath();
        if (resolved.isEmpty() || !inside(resolved, m_destination)) return reject(source, "A pasta de destino mudou ou aponta para fora do destino escolhido.");
        if (link(source)) return reject(source, "Links e junções não são suportados nesta versão.");
        const auto target = QDir(destination).filePath(name ? QString::fromWCharArray(name) : QFileInfo(source).fileName());
        if (m_request.conflict == OperationConflict::Ask && link(target)) return reject(source, "Um item no destino é um link ou junção.");
        m_current = source; if (m_progress) m_progress(0, 0, source); return checkCancel();
    }
    HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD, IShellItem *item, IShellItem *, LPCWSTR, HRESULT hr, IShellItem *created) override {
        const auto source = shellPath(item); const int root = rootIndex(source);
        if (root < 0) { m_result.error = "O Windows retornou um item fora da seleção de origem."; return E_FAIL; }
        auto &result = m_result.items[root];
        if (hr == COPYENGINE_S_USER_IGNORED) {
            result.state = same(source, result.source) ? OperationItemState::Skipped : OperationItemState::Incomplete;
            if (result.error.isEmpty()) result.error = "Um item foi ignorado.";
        } else if (cancellation(hr)) {
            result.state = m_directories.contains(result.source.toCaseFolded()) ? OperationItemState::Incomplete : OperationItemState::Cancelled;
            if (result.error.isEmpty()) result.error = "Operação cancelada.";
        } else if (FAILED(hr)) {
            result.state = OperationItemState::Failed;
            if (result.error.isEmpty()) result.error = errorText(hr);
        } else if (same(source, result.source)) {
            result.target = shellPath(created);
            if (result.state != OperationItemState::Failed && result.state != OperationItemState::Incomplete && result.state != OperationItemState::Cancelled) {
                result.state = !result.target.isEmpty() ? OperationItemState::Completed : OperationItemState::Incomplete;
            }
        }
        // Cancellation is checked after each item and during native progress updates.
        return checkCancel();
    }
    HRESULT STDMETHODCALLTYPE UpdateProgress(UINT total, UINT done) override {
        if (m_progress) m_progress(total, done, m_current); return checkCancel();
    }
    HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD, IShellItem *item, LPCWSTR name) override {
        const auto source = shellPath(item);
        if (m_request.kind != OperationKind::Rename || !name || QString::fromWCharArray(name) != m_request.name) return reject(source, "Renomeação inesperada.");
        const auto error = validateOperationRequest(m_request);
        if (!error.isEmpty()) return reject(source, error);
        m_current = source; if (m_progress) m_progress(0, 0, source); return checkCancel();
    }
    HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD flags, IShellItem *item, LPCWSTR, HRESULT hr, IShellItem *created) override {
        return PostCopyItem(flags, item, nullptr, nullptr, hr, created);
    }
    HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD flags, IShellItem *item, IShellItem *folder, LPCWSTR name) override {
        if (m_request.kind != OperationKind::Move) return E_ACCESSDENIED;
        return PreCopyItem(flags, item, folder, name);
    }
    HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD flags, IShellItem *item, IShellItem *folder, LPCWSTR name, HRESULT hr, IShellItem *created) override {
        return PostCopyItem(flags, item, folder, name, hr, created);
    }
    // Recycling uses ITransferSource::RecycleItem exclusively; never DeleteItem.
    HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD, IShellItem *) override { return E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD, IShellItem *, HRESULT, IShellItem *) override { return E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE PreNewItem(DWORD, IShellItem *folder, LPCWSTR name) override {
        const auto destination = shellPath(folder); const auto resolved = QFileInfo(destination).canonicalFilePath();
        if (resolved.isEmpty() || !inside(resolved, m_destination)) return reject(m_current, "O destino de uma nova pasta aponta para fora da pasta escolhida.");
        if (name && link(QDir(destination).filePath(QString::fromWCharArray(name)))) return reject(m_current, "Uma pasta existente no destino é um link ou junção.");
        if (m_request.kind == OperationKind::CreateFolder) {
            if (!name || QString::fromWCharArray(name) != m_request.name) return reject(m_result.items.first().source, "O Windows propôs um nome diferente para a nova pasta.");
            const auto error = validateOperationRequest(m_request);
            if (!error.isEmpty()) return reject(m_result.items.first().source, error);
            m_current = m_result.items.first().source; if (m_progress) m_progress(0, 0, m_current);
        }
        return checkCancel();
    }
    HRESULT STDMETHODCALLTYPE PostNewItem(DWORD, IShellItem *, LPCWSTR, LPCWSTR, DWORD, HRESULT hr, IShellItem *created) override {
        if (m_request.kind != OperationKind::CreateFolder) return S_OK;
        auto &item = m_result.items.first();
        item.target = shellPath(created);
        if (FAILED(hr)) { item.state = cancellation(hr) ? OperationItemState::Cancelled : OperationItemState::Failed; item.error = errorText(hr); }
        else item.state = !item.target.isEmpty() ? OperationItemState::Completed : OperationItemState::Incomplete;
        return checkCancel();
    }
    HRESULT STDMETHODCALLTYPE ResetTimer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PauseTimer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResumeTimer() override { return S_OK; }
    HRESULT finishCode = S_OK;
private:
    std::atomic_ulong m_refs{1};
    const OperationRequest &m_request;
    OperationResult &m_result;
    const std::atomic_bool &m_cancel;
    const OperationProgress &m_progress;
    QHash<QString, int> m_roots;
    QSet<QString> m_directories;
    QString m_destination, m_current;
    HRESULT checkCancel() const { return m_cancel.load() ? COPYENGINE_E_USER_CANCELLED : S_OK; }
    int rootIndex(QString path) const {
        path = normalized(path).toCaseFolded();
        while (!path.isEmpty()) {
            const auto found = m_roots.constFind(path); if (found != m_roots.constEnd()) return found.value();
            const int slash = path.lastIndexOf('/'); if (slash < 0) break; path.truncate(slash);
        }
        return -1;
    }
    HRESULT reject(const QString &source, const QString &message) {
        const int root = rootIndex(source);
        if (root >= 0) { m_result.items[root].state = OperationItemState::Failed; m_result.items[root].error = message; }
        m_result.error = message; return E_ACCESSDENIED;
    }
};
}

int OperationResult::completedCount() const {
    int count = 0; for (const auto &item : items) if (item.state == OperationItemState::Completed) ++count; return count;
}
QString operationLabel(OperationKind kind) {
    switch (kind) {
    case OperationKind::Copy: return "Cópia";
    case OperationKind::Move: return "Movimentação";
    case OperationKind::Rename: return "Renomeação";
    case OperationKind::CreateFolder: return "Criação de pasta";
    case OperationKind::Trash: return "Envio à Lixeira";
    }
    return {};
}
QString operationItemLabel(OperationItemState state){
    switch(state){case OperationItemState::Completed:return "Concluído";case OperationItemState::Cancelled:return "Cancelado";case OperationItemState::Failed:return "Erro";case OperationItemState::Incomplete:return "Parcial / não confirmado";case OperationItemState::Delegated:return "Encaminhado — conferir ferramenta";case OperationItemState::NotStarted:return "Não iniciado";case OperationItemState::Skipped:return "Ignorado";}return {};
}
QString validateItemName(const QString &name) {
    if (name.isEmpty() || name == "." || name == "..") return "Informe um nome para o item.";
    if (name.size() > 255 || name.endsWith(' ') || name.endsWith('.')) return "O nome é longo demais ou termina com espaço ou ponto.";
    for (const auto ch : name) if (ch.unicode() < 32 || QString("<>:\"/\\|?*").contains(ch)) return "O nome contém um caractere que o Windows não permite.";
    static const QRegularExpression reserved("^(CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³])(?:\\.|$)", QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(name).hasMatch()) return "Este nome é reservado pelo Windows.";
    return {};
}
QString validateOperationRequest(const OperationRequest &request) {
    if (request.engine == TransferEngine::TeraCopy) {
        if (request.kind != OperationKind::Copy) return "A integração TeraCopy desta versão aceita apenas cópia. Use Windows para as outras operações.";
        if (request.conflict != OperationConflict::KeepBoth) return "Com TeraCopy, use Manter os dois para preservar arquivos existentes.";
        for (const auto &path : request.sources + QStringList{request.destination})
            for (const auto ch : path) if (ch.unicode() < 32) return "Um caminho contém um caractere incompatível com a lista do TeraCopy.";
    }
    const bool transfer = request.kind == OperationKind::Copy || request.kind == OperationKind::Move;
    const bool named = request.kind == OperationKind::Rename || request.kind == OperationKind::CreateFolder;
    if (named) { const auto error = validateItemName(request.name); if (!error.isEmpty()) return error; }
    if (request.kind == OperationKind::Rename && request.sources.size() != 1) return "Selecione apenas um item para renomear.";
    if (request.kind != OperationKind::CreateFolder && request.sources.isEmpty()) return "Selecione ao menos um arquivo ou pasta.";
    QString target;
    if (transfer || request.kind == OperationKind::CreateFolder) {
        if (!QDir::isAbsolutePath(request.destination)) return "Informe o caminho completo da pasta de destino.";
        const QFileInfo destination(request.destination);
        target = destination.canonicalFilePath();
        if (!destination.exists() || !destination.isDir() || target.isEmpty()) return "A pasta de destino não existe ou não está disponível.";
    }
    if (request.kind == OperationKind::CreateFolder) {
        if (!request.sources.isEmpty()) return "Uma nova pasta não deve ter itens de origem.";
        const auto existing = QDir(target).filePath(request.name);
        if (QFileInfo::exists(existing) || link(existing)) return "Já existe um item com esse nome. Escolha outro nome.";
        return {};
    }
    QSet<QString> seen;
    for (const auto &source : request.sources) {
        if (!QDir::isAbsolutePath(source)) return "O caminho de um item de origem não é completo.";
        const QFileInfo info(source);
        if (!info.exists() || (!info.isFile() && !info.isDir())) return "Um item de origem não existe ou não está disponível: " + source;
        if (link(source)) return "Links e junções não são suportados nesta versão: " + source;
        if (info.fileName().isEmpty() || QDir(source).isRoot()) return "Selecione uma pasta ou arquivo, não uma raiz de unidade.";
        const auto canonical = info.canonicalFilePath();
        if (canonical.isEmpty()) return "Não foi possível confirmar o caminho da origem: " + source;
        if (seen.contains(canonical.toCaseFolded())) return "Há itens duplicados na seleção.";
        seen.insert(canonical.toCaseFolded());
        if (transfer) {
            if (same(info.absolutePath(), target)) return "Escolha uma pasta de destino diferente da pasta de origem.";
            if (info.isDir() && inside(target, canonical)) return "Uma pasta não pode ser transferida para dentro de si mesma.";
            const auto existing = QDir(target).filePath(info.fileName());
            if (request.conflict == OperationConflict::Ask && link(existing)) return "Um item de destino é um link ou junção: " + existing;
            if (!request.nativeUi && request.conflict == OperationConflict::Ask && QFileInfo::exists(existing)) return "O destino contém um item existente; a escolha do conflito requer a interface do Windows.";
        } else if (request.kind == OperationKind::Rename) {
            if (info.fileName() == request.name) return "Informe um nome diferente do atual.";
            const auto existing = QDir(info.absolutePath()).filePath(request.name);
            if ((QFileInfo::exists(existing) || link(existing)) && !same(existing, source)) return "Já existe um item com esse nome. Escolha outro nome.";
        } else if (request.kind == OperationKind::Trash) {
            const auto path = QDir::toNativeSeparators(canonical); wchar_t volume[MAX_PATH];
            if (!GetVolumePathNameW(reinterpret_cast<LPCWSTR>(path.utf16()), volume, MAX_PATH) || GetDriveTypeW(volume) != DRIVE_FIXED)
                return "Nesta versão, a Lixeira aceita apenas itens em unidades locais fixas.";
            if (canonical.contains("/$Recycle.Bin/", Qt::CaseInsensitive) || canonical.endsWith("/$Recycle.Bin", Qt::CaseInsensitive))
                return "Não selecione itens internos da Lixeira.";
        }
    }
    for (auto path : seen) {
        for (int slash = path.lastIndexOf('/'); slash > 0; slash = path.lastIndexOf('/')) {
            path.truncate(slash);
            if (seen.contains(path)) return "A seleção contém uma pasta e um item dentro dela. Selecione apenas a pasta.";
        }
    }
    return {};
}

QString validateOperationTree(const OperationRequest &request, const std::atomic_bool &cancel, const OperationProgress &progress) {
    // Validate nested links before queueing writes. Enumeration never follows links.
    for (const auto &source : request.sources) {
        if (!QFileInfo(source).isDir() || request.kind == OperationKind::Rename) continue;
        if (progress) progress(0, 0, source);
        QDirIterator entries(source, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (entries.hasNext()) {
            if (cancel.load()) return {};
            entries.next(); const auto info = entries.fileInfo();
            if (info.isSymLink() || info.isJunction()) return "A seleção contém um link ou junção não suportado: " + info.absoluteFilePath();
        }
    }
    return {};
}

OperationResult performOperation(const OperationRequest &request, const std::atomic_bool &cancel, const OperationProgress &progress) {
    if (request.engine == TransferEngine::TeraCopy) return dispatchTeraCopy(request, cancel, progress);
    OperationResult result;
    for (const auto &source : request.sources) result.items.append({normalized(QFileInfo(source).absoluteFilePath()), {}, OperationItemState::NotStarted, {}, {}});
    if (request.kind == OperationKind::CreateFolder)
        result.items.append({normalized(QDir(request.destination).filePath(request.name)), {}, OperationItemState::NotStarted, {}, {}});
    if (cancel.load()) { result.state = OperationState::Cancelled; return result; }
    result.error = validateOperationRequest(request); if (!result.error.isEmpty()) return result;
    result.error = validateOperationTree(request, cancel, progress);
    if (!result.error.isEmpty()) return result;
    if (cancel.load()) { result.state = OperationState::Cancelled; return result; }
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initialized)) { result.error = errorText(initialized); return result; }
    // Scope releases all COM references before balancing CoInitializeEx.
    [&] {
        if (request.kind == OperationKind::Trash) {
            // This calls only the explicit recycling API. Unsupported recycling fails;
            // there is no DeleteItem/RemoveItem or permanent-deletion fallback.
            ComPtr<IShellItem> bin;
            HRESULT hr = SHGetKnownFolderItem(FOLDERID_RecycleBinFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&bin));
            if (FAILED(hr)) { result.error = errorText(hr); return; }
            quint32 completed = 0;
            for (auto &out : result.items) {
                if (progress) progress(quint32(result.items.size()), completed, out.source);
                if (cancel.load()) { result.state = OperationState::Cancelled; return; }
                OperationRequest single = request; single.sources = {out.source};
                const auto error = validateOperationRequest(single);
                if (!error.isEmpty()) { result.error = out.error = error; out.state = OperationItemState::Failed; return; }
                const auto path = QDir::toNativeSeparators(out.source);
                ComPtr<IShellItem> item, parent, recycled; ComPtr<ITransferSource> transfer;
                hr = SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, IID_PPV_ARGS(&item));
                if (SUCCEEDED(hr)) hr = item->GetParent(&parent);
                if (SUCCEEDED(hr)) hr = parent->BindToHandler(nullptr, BHID_Transfer, IID_PPV_ARGS(&transfer));
                if (SUCCEEDED(hr)) hr = transfer->RecycleItem(item.Get(), bin.Get(), TSF_NORMAL, &recycled);
                if (FAILED(hr)) { out.state = cancellation(hr) ? OperationItemState::Cancelled : OperationItemState::Failed; result.state = cancellation(hr) ? OperationState::Cancelled : OperationState::Failed; result.error = out.error = errorText(hr); return; }
                PWSTR parsing = nullptr;
                if (recycled && SUCCEEDED(recycled->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &parsing))) {
                    out.recycledParsingName = QString::fromWCharArray(parsing); CoTaskMemFree(parsing);
                    out.target = shellPath(recycled.Get());
                }
                if (out.recycledParsingName.isEmpty() || QFileInfo::exists(out.source)) {
                    out.state = OperationItemState::Incomplete; result.state = OperationState::Incomplete;
                    result.error = out.error = "O Windows não confirmou a transferência para a Lixeira."; return;
                }
                out.state = OperationItemState::Completed;
                ++completed;
            }
            if (progress) progress(quint32(result.items.size()), completed, {});
            result.state = OperationState::Completed; return;
        }
        ComPtr<IFileOperation> operation; HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&operation));
        auto fail = [&](HRESULT code) { result.error = errorText(code); };
        if (FAILED(hr)) { fail(hr); return; }
        DWORD flags = FOF_NOCONFIRMMKDIR | FOF_NO_CONNECTED_ELEMENTS | FOF_NOERRORUI | FOFX_EARLYFAILURE;
        if (request.conflict == OperationConflict::KeepBoth) flags |= FOF_RENAMEONCOLLISION;
        if (!request.nativeUi) flags |= FOF_SILENT;
        hr = operation->SetOperationFlags(flags); if (FAILED(hr)) { fail(hr); return; }
        if (request.nativeUi && request.ownerWindow) {
            hr = operation->SetOwnerWindow(reinterpret_cast<HWND>(request.ownerWindow)); if (FAILED(hr)) { fail(hr); return; }
        }
        const auto destinationPath = QDir::toNativeSeparators(QFileInfo(request.kind == OperationKind::Rename ? QFileInfo(request.sources.first()).absolutePath() : request.destination).canonicalFilePath());
        ComPtr<IShellItem> destination;
        hr = SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(destinationPath.utf16()), nullptr, IID_PPV_ARGS(&destination));
        if (FAILED(hr)) { fail(hr); return; }
        if (request.kind == OperationKind::CreateFolder) {
            hr = operation->NewItem(destination.Get(), FILE_ATTRIBUTE_DIRECTORY, reinterpret_cast<LPCWSTR>(request.name.utf16()), nullptr, nullptr);
            if (FAILED(hr)) { fail(hr); return; }
        } else for (const auto &source : result.items) {
            const auto path = QDir::toNativeSeparators(source.source); ComPtr<IShellItem> item;
            hr = SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, IID_PPV_ARGS(&item));
            if (SUCCEEDED(hr)) {
                switch (request.kind) {
                case OperationKind::Copy: hr = operation->CopyItem(item.Get(), destination.Get(), nullptr, nullptr); break;
                case OperationKind::Move: hr = operation->MoveItem(item.Get(), destination.Get(), nullptr, nullptr); break;
                case OperationKind::Rename: hr = operation->RenameItem(item.Get(), reinterpret_cast<LPCWSTR>(request.name.utf16()), nullptr); break;
                default: hr = E_INVALIDARG; break;
                }
            }
            if (FAILED(hr)) { fail(hr); return; }
        }
        ComPtr<OperationSink> sink; sink.Attach(new OperationSink(request, result, cancel, progress));
        DWORD cookie = 0; hr = operation->Advise(sink.Get(), &cookie); if (FAILED(hr)) { fail(hr); return; }
        hr = operation->PerformOperations();
        BOOL aborted = FALSE; const HRESULT abortQuery = operation->GetAnyOperationsAborted(&aborted);
        operation->Unadvise(cookie);
        if (SUCCEEDED(hr) && FAILED(sink->finishCode)) hr = sink->finishCode;
        if (FAILED(abortQuery)) { result.error = "Não foi possível confirmar se todos os itens foram processados: " + errorText(abortQuery); result.state = OperationState::Incomplete; }
        else if (SUCCEEDED(hr) && !aborted && result.error.isEmpty() && result.completedCount() == result.items.size()) result.state = OperationState::Completed;
        else if (cancellation(hr) || cancel.load()) result.state = OperationState::Cancelled;
        else if (FAILED(hr)) { result.state = OperationState::Failed; if (result.error.isEmpty()) fail(hr); }
        else result.state = OperationState::Incomplete;
    }();
    CoUninitialize(); return result;
}

OperationJob::OperationJob(OperationRequest request, QObject *parent) : QThread(parent), m_request(std::move(request)) {}
void OperationJob::run() { m_result = performOperation(m_request, m_cancel, [this](quint32 total, quint32 done, const QString &item) { emit progress(total, done, item); }); }
