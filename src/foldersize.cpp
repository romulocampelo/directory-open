#include "foldersize.h"
#include "windowspath.h"
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMutexLocker>
#include <algorithm>
#include <limits>
#include <memory>
#include <vector>
#include <windows.h>

namespace {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
using OwnedHandle = std::unique_ptr<Handle>;
struct FindHandle {
    HANDLE value;
    ~FindHandle() { if (value != INVALID_HANDLE_VALUE) FindClose(value); }
};
OwnedHandle openMetadata(const QString &path, bool directory = false) {
    // Deny deletion AND write handles: an in-place FSCTL_SET_REPARSE_POINT
    // must not turn a pinned ordinary directory into a junction before FindFirst.
    // FILE_READ_ATTRIBUTES alone does not participate in Windows share-access
    // tracking. Directory handles need FILE_LIST_DIRECTORY for these guards.
    // File metadata uses its handle identity without locking content writes.
    return std::make_unique<Handle>(CreateFileW(reinterpret_cast<LPCWSTR>(WindowsPath::forFileApi(path).utf16()),
        FILE_READ_ATTRIBUTES | (directory ? FILE_LIST_DIRECTORY : 0),
        directory ? FILE_SHARE_READ : FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
}
struct ResolvedPath { QString path; DWORD error = ERROR_SUCCESS; };
ResolvedPath finalPath(HANDLE h) {
    const auto size = GetFinalPathNameByHandleW(h, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!size) return {{}, GetLastError()};
    if (size > 32768) return {{}, ERROR_FILENAME_EXCED_RANGE};
    std::wstring buffer(size, L'\0');
    const auto written = GetFinalPathNameByHandleW(h, buffer.data(), size, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!written) return {{}, GetLastError()};
    if (written >= size) return {{}, ERROR_INSUFFICIENT_BUFFER};
    buffer.resize(written);
    return {QDir::cleanPath(WindowsPath::fromFileApi(QString::fromStdWString(buffer))), ERROR_SUCCESS};
}
bool beneath(const QString &path, const QString &root) {
    return path == root || path.startsWith(root.endsWith('/') ? root : root + '/');
}
bool changed(const BY_HANDLE_FILE_INFORMATION &a, const BY_HANDLE_FILE_INFORMATION &b) {
    return a.nFileSizeHigh != b.nFileSizeHigh || a.nFileSizeLow != b.nFileSizeLow
        || CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) != 0;
}
bool sameObject(const BY_HANDLE_FILE_INFORMATION &a, const BY_HANDLE_FILE_INFORMATION &b) {
    return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow;
}
}

bool addLogicalBytes(quint64 &subtotal, quint64 bytes) {
    if (bytes > std::numeric_limits<quint64>::max() - subtotal) return false;
    subtotal += bytes; return true;
}

FolderSizeResult calculateFolderSize(const FolderSizeRequest &request, const std::atomic_bool &cancel, const FolderSizeUpdate &update) {
    FolderSizeResult result; result.started = QDateTime::currentDateTimeUtc();
    result.request = request;
    result.listingState = request.collectListing ? FolderListingState::Running : FolderListingState::NotRequested;
    QElapsedTimer elapsed, publication; elapsed.start(); publication.start();
    auto publish = [&](bool force = false) {
        result.elapsedMs = elapsed.elapsed();
        if (update && (force || publication.elapsed() >= 100)) { update(result); publication.restart(); }
    };
    auto error = [&](const QString &path, const QString &message) {
        ++result.errors;
        if (result.details.size() < 20) result.details.append(QDir::toNativeSeparators(path) + ": " + message);
    };
    auto nativeError = [&](const QString &path, DWORD code) { error(path, QString("Erro Windows %1").arg(code)); };
    auto retain = [&](const QString &path, bool directory, quint64 bytes = 0) {
        if (!request.collectListing) return;
        ++result.listingObserved;
        if (result.listingState == FolderListingState::Limited) return;
        // Account for UTF-16 storage and record overhead before retaining. No disk reread.
        const quint64 cost = quint64(path.size() + 1) * sizeof(QChar) + sizeof(FolderListingEntry);
        if (quint64(result.listing.size()) >= request.maxListingEntries || cost > request.maxListingBytes - result.listingBytes) {
            result.listingState = FolderListingState::Limited;
            result.listingLimitReason = "Retenção da listagem atingiu o limite de linhas ou memória; cálculo continua.";
            return;
        }
        result.listing.append({path, directory, bytes}); result.listingBytes += cost;
    };
    auto limit = [&](const QString &reason) { result.state = FolderSizeState::Limited; result.limitReason = reason; };
    auto stop = [&] {
        if (result.state == FolderSizeState::Limited || result.state == FolderSizeState::Cancelled) return true;
        if (cancel) { result.state = FolderSizeState::Cancelled; return true; }
        if (elapsed.elapsed() >= request.maxElapsedMs) { limit("Prazo de leitura atingido."); return true; }
        return false;
    };
    // Keep every root's ancestry pinned. This prevents a junction substitution
    // between no-follow metadata inspection and path-based Win32 enumeration.
    std::vector<OwnedHandle> ancestry;
    struct Root { QString path; OwnedHandle handle; BY_HANDLE_FILE_INFORMATION info; };
    std::vector<Root> roots;
    quint64 inputChars = 0;
    for (const auto &path : request.roots) inputChars += quint64(path.size());
    if (request.roots.isEmpty() || request.roots.size() > 256 || inputChars > 1048576
        || !request.maxEntries || request.maxEntries > 10000000 || request.maxDepth < 0 || request.maxDepth > 256
        || request.maxElapsedMs <= 0 || request.maxElapsedMs > 600000
        || (request.collectListing && (!request.maxListingEntries || request.maxListingEntries > 10000
            || !request.maxListingBytes || request.maxListingBytes > 8 * 1024 * 1024))) {
        error({}, "Selecione de 1 a 256 pastas; pedido ou limites inválidos.");
    } else for (const auto &source : request.roots) {
        if (stop()) break;
        if (result.examined >= request.maxEntries) { limit("Limite de entradas examinadas atingido."); break; }
        ++result.examined;
        QString path = QDir::fromNativeSeparators(source);
        if (!QFileInfo(path).isAbsolute() || path.startsWith("//?/") || path.startsWith("//./")
            || path.split('/').contains("..") || path.mid(2).contains(':')) {
            error(path, "Caminho absoluto de pasta física esperado (sem .. ou namespace de dispositivo)."); continue;
        }
        path = QDir::cleanPath(path);
        // Inspect and pin each component, including an explicitly selected link.
        QStringList chain; QString component = path;
        while (true) {
            chain.prepend(component);
            const QString parent = QFileInfo(component).absolutePath();
            if (parent == component || QDir(component).isRoot()
                || (component.startsWith("//") && component.split('/', Qt::SkipEmptyParts).size() == 2) || chain.size() > 512) break;
            component = parent;
        }
        bool valid = chain.size() <= 512;
        if (!valid) error(path, "Ancestrais excedem o limite de 512 componentes.");
        OwnedHandle selected;
        BY_HANDLE_FILE_INFORMATION info{};
        for (const auto &part : chain) {
            if (!valid || stop()) { valid = false; break; }
            if (ancestry.size() >= 4096) { limit("Limite de 4096 handles de ancestrais atingido."); valid = false; break; }
            auto handle = openMetadata(part, true);
            if (handle->value == INVALID_HANDLE_VALUE) { nativeError(part, GetLastError()); valid = false; break; }
            if (!GetFileInformationByHandle(handle->value, &info)) { nativeError(part, GetLastError()); valid = false; break; }
            if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { ++result.skippedLinks; valid = false; break; }
            if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { error(part, "A seleção deve conter somente pastas."); valid = false; break; }
            if (part == path) selected = std::move(handle); else ancestry.push_back(std::move(handle));
        }
        if (!valid || !selected) continue;
        const auto resolved = finalPath(selected->value);
        if (resolved.error != ERROR_SUCCESS) { nativeError(path, resolved.error); continue; }
        roots.push_back({resolved.path, std::move(selected), info});
    }
    // Canonical spelling from the open handle, case preserved for case-sensitive
    // directories. Short names / drive aliases resolve before union selection.
    std::sort(roots.begin(), roots.end(), [](const Root &a, const Root &b) { return a.path.size() < b.path.size(); });
    std::vector<const Root *> effectiveRoots;
    std::function<void(const QString &, HANDLE, const BY_HANDLE_FILE_INFORMATION &, int)> walk;
    walk = [&](const QString &path, HANDLE handle, const BY_HANDLE_FILE_INFORMATION &before, int depth) {
        if (stop()) return;
        ++result.folders; retain(path, true); result.currentPath = path; publish(depth == 0);
        WIN32_FIND_DATAW entry{};
        const auto pattern = WindowsPath::forFileApi(path + "/*");
        FindHandle find{FindFirstFileW(reinterpret_cast<LPCWSTR>(pattern.utf16()), &entry)};
        if (find.value == INVALID_HANDLE_VALUE) {
            const auto code = GetLastError(); if (code != ERROR_FILE_NOT_FOUND) nativeError(path, code);
        } else do {
            if (stop()) break;
            const QString name = QString::fromWCharArray(entry.cFileName);
            if (name == "." || name == "..") continue;
            if (result.examined >= request.maxEntries) { limit("Limite de entradas examinadas atingido."); break; }
            ++result.examined;
            const QString child = path + (path.endsWith('/') ? "" : "/") + name;
            result.currentPath = child;
            auto owned = openMetadata(child);
            if (owned->value == INVALID_HANDLE_VALUE) { nativeError(child, GetLastError()); publish(); continue; }
            BY_HANDLE_FILE_INFORMATION facts{};
            if (!GetFileInformationByHandle(owned->value, &facts)) { nativeError(child, GetLastError()); continue; }
            if (facts.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ++result.skippedLinks;
            else if (facts.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                auto pinned = openMetadata(child, true);
                if (pinned->value == INVALID_HANDLE_VALUE) { nativeError(child, GetLastError()); continue; }
                BY_HANDLE_FILE_INFORMATION directoryFacts{};
                if (!GetFileInformationByHandle(pinned->value, &directoryFacts)) { nativeError(child, GetLastError()); continue; }
                if (!sameObject(facts, directoryFacts) || !(directoryFacts.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    ++result.changes; error(child, "A pasta foi substituída antes da leitura."); continue;
                }
                if (directoryFacts.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { ++result.skippedLinks; continue; }
                if (depth >= request.maxDepth) { limit("Limite de profundidade atingido; subpastas não lidas."); break; }
                walk(child, pinned->value, directoryFacts, depth + 1);
            } else {
                ++result.files;
                const quint64 size = (quint64(facts.nFileSizeHigh) << 32) | facts.nFileSizeLow;
                retain(child, false, size);
                if (!addLogicalBytes(result.bytes, size)) { result.overflow = true; limit("Overflow de 64 bits; subtotal anterior preservado."); break; }
                BY_HANDLE_FILE_INFORMATION after{};
                if (!GetFileInformationByHandle(owned->value, &after)) nativeError(child, GetLastError());
                else if (changed(facts, after)) ++result.changes;
            }
            publish();
        } while ([&] { if (FindNextFileW(find.value, &entry)) return true; const auto code = GetLastError(); if (code != ERROR_NO_MORE_FILES) nativeError(path, code); return false; }());
        BY_HANDLE_FILE_INFORMATION after{};
        if (!GetFileInformationByHandle(handle, &after)) nativeError(path, GetLastError());
        else if (changed(before, after)) ++result.changes;
    };
    for (const auto &root : roots) {
        if (std::any_of(effectiveRoots.begin(), effectiveRoots.end(), [&](const Root *parent) { return beneath(root.path, parent->path); })) { ++result.overlaps; continue; }
        effectiveRoots.push_back(&root); result.roots.append(root.path);
    }
    for (const auto *root : effectiveRoots) {
        if (stop()) break;
        walk(root->path, root->handle->value, root->info, 0);
    }
    stop();
    if (result.state == FolderSizeState::Running)
        result.state = result.errors || result.skippedLinks || result.changes ? FolderSizeState::Partial : FolderSizeState::Complete;
    if (result.listingState == FolderListingState::Running) {
        switch (result.state) {
        case FolderSizeState::Complete: result.listingState = FolderListingState::Complete; break;
        case FolderSizeState::Partial: result.listingState = FolderListingState::Partial; break;
        case FolderSizeState::Cancelled: result.listingState = FolderListingState::Cancelled; break;
        case FolderSizeState::Limited: result.listingState = FolderListingState::Limited; break;
        case FolderSizeState::Running: break;
        }
    }
    result.currentPath.clear(); result.finished = QDateTime::currentDateTimeUtc(); publish(true); return result;
}

QString folderSizeStateText(FolderSizeState state) {
    switch (state) {
    case FolderSizeState::Running: return QStringLiteral("Calculando — subtotal provisório");
    case FolderSizeState::Complete: return QStringLiteral("Completo no escopo lido — tamanho lógico observado");
    case FolderSizeState::Partial: return QStringLiteral("Parcial — subtotal observado, não é o total das pastas");
    case FolderSizeState::Cancelled: return QStringLiteral("Cancelado — subtotal observado, não é o total das pastas");
    case FolderSizeState::Limited: return QStringLiteral("Limitado — subtotal observado, não é o total das pastas");
    }
    return {};
}
QString folderListingStateText(FolderListingState state) {
    switch (state) {
    case FolderListingState::NotRequested: return QStringLiteral("Não solicitada — detalhes não coletados");
    case FolderListingState::Running: return QStringLiteral("Coletando — cobertura provisória");
    case FolderListingState::Complete: return QStringLiteral("Completa no escopo lido");
    case FolderListingState::Partial: return QStringLiteral("Parcial — cobertura incompleta");
    case FolderListingState::Cancelled: return QStringLiteral("Cancelada — cobertura incompleta");
    case FolderListingState::Limited: return QStringLiteral("Limitada — cobertura incompleta");
    }
    return {};
}

FolderSizeJob::FolderSizeJob(FolderSizeRequest request, QObject *parent) : QThread(parent), m_request(std::move(request)) {
    m_result.request = m_request;
    m_result.listingState = m_request.collectListing ? FolderListingState::Running : FolderListingState::NotRequested;
}
FolderSizeResult FolderSizeJob::result() const { QMutexLocker lock(&m_mutex); return m_result; }
void FolderSizeJob::run() {
    const auto result = calculateFolderSize(m_request, m_cancel, [this](const FolderSizeResult &snapshot) { QMutexLocker lock(&m_mutex); m_result = snapshot; });
    QMutexLocker lock(&m_mutex); m_result = result;
}
