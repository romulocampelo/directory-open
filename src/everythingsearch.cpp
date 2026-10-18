#include "everythingsearch.h"
#include "everythingipc.h"
#include "searchquery.h"
#include "windowspath.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSet>
#include <QHash>
#include <cstring>
#include <windows.h>

namespace {
struct DirectoryIdentity {
    DWORD volume, high, low;
    bool operator==(const DirectoryIdentity &) const = default;
};
using DirectoryProbe = std::variant<DirectoryIdentity, SearchFailure>;
DirectoryProbe directoryIdentity(const QString &path) {
    const auto native = WindowsPath::forFileApi(path).toStdWString();
    const auto handle = CreateFileW(native.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return SearchFailure{SearchErrorCode::Access, "Não foi possível confirmar a identidade da pasta da busca."};
    BY_HANDLE_FILE_INFORMATION info{}; const bool read = GetFileInformationByHandle(handle, &info); CloseHandle(handle);
    if (!read || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return SearchFailure{SearchErrorCode::Access, "A pasta da busca foi alterada ou sua identidade não pôde ser confirmada."};
    return DirectoryIdentity{info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow};
}
// A worker owns its reply window and retained endpoint handle, never the Everything process.
class Channel {
public:
    HWND target = nullptr, reply = nullptr;
    HANDLE process = nullptr;
    DWORD processId = 0;
    quint32 replyId = 0;
    quint32 offset = 0, capacity = 0;
    std::optional<EverythingIpc::PageResult> result;
    ~Channel() { if (reply) DestroyWindow(reply); if (process) CloseHandle(process); }
    bool alive() const {
        DWORD current = 0;
        return process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT
            && GetWindowThreadProcessId(target, &current) && current == processId;
    }
    static LRESULT CALLBACK receive(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            const auto *create = reinterpret_cast<CREATESTRUCTW *>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto *self = reinterpret_cast<Channel *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_COPYDATA && self && !self->result) {
            const auto *data = reinterpret_cast<COPYDATASTRUCT *>(lparam);
            if (!data || data->dwData != self->replyId || (wparam && reinterpret_cast<HWND>(wparam) != self->target)) return FALSE;
            if (!data->lpData || data->cbData > EverythingIpc::MaxPageBytes) {
                self->result = SearchFailure{SearchErrorCode::Protocol, "Resposta do Everything excede o limite da página."};
            } else self->result = EverythingIpc::decodePage(QByteArrayView(static_cast<const char *>(data->lpData), data->cbData), self->offset, self->capacity);
            return TRUE;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }
    bool createReply() {
        static const ATOM windowClass = [] {
            WNDCLASSW value{}; value.lpfnWndProc = receive; value.hInstance = GetModuleHandleW(nullptr);
            value.lpszClassName = L"DirectoryOpen.EverythingReply"; return RegisterClassW(&value);
        }();
        if (!windowClass) return false;
        reply = CreateWindowExW(0, L"DirectoryOpen.EverythingReply", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), this);
        return reply != nullptr;
    }
};
}

SearchSummary searchEverything(const SearchRequest &request, const PreparedSearch &prepared, const std::atomic_bool &cancel, const SearchUpdate &update) {
    const auto instanceSuffix = request.everythingInstance.isEmpty() ? QString{} : " · " + request.everythingInstance;
    SearchSummary summary; summary.engine = SearchEngine::Everything; summary.source = "Everything" + instanceSuffix;
    auto fail = [&](SearchErrorCode code, const QString &message) {
        summary.state = SearchState::Failed; summary.failure = SearchFailure{code, message}; ++summary.errors;
        if (summary.details.size() < 20) summary.details.append(message);
        return summary;
    };
    if (cancel) { summary.state = SearchState::Cancelled; return summary; }
    const auto rootProbe = directoryIdentity(prepared.root);
    if (const auto *error = std::get_if<SearchFailure>(&rootProbe)) return fail(error->code, error->message);
    const auto rootIdentity = std::get<DirectoryIdentity>(rootProbe);
    QElapsedTimer deadline; deadline.start();
    Channel channel;
    const auto className = QString("EVERYTHING_TASKBAR_NOTIFICATION")
        + (request.everythingInstance.isEmpty() ? QString{} : "_(" + request.everythingInstance + ')');
    channel.target = FindWindowW(reinterpret_cast<LPCWSTR>(className.utf16()), nullptr);
    if (!channel.target) return fail(SearchErrorCode::Unavailable, "Everything não está em execução nesta instância. Use a busca direta ou abra o Everything.");
    GetWindowThreadProcessId(channel.target, &channel.processId);
    channel.process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, channel.processId);
    if (!channel.alive()) return fail(SearchErrorCode::Unavailable, "A instância do Everything não está mais disponível.");
    auto send = [&](UINT message, WPARAM wparam, LPARAM lparam, DWORD_PTR &value) {
        const auto remaining = qMin<qint64>(1000, request.timeoutMs - deadline.elapsed());
        return !cancel && remaining > 0 && channel.alive()
            && SendMessageTimeoutW(channel.target, message, wparam, lparam, SMTO_ABORTIFHUNG | SMTO_BLOCK, UINT(remaining), &value);
    };
    DWORD_PTR version[4]{}, loaded = 0;
    for (int part = 0; part < 4; ++part) if (!send(WM_USER, WPARAM(part), 0, version[part])) {
        if (cancel) { summary.state = SearchState::Cancelled; return summary; }
        return fail(SearchErrorCode::Timeout, "Everything não respondeu à consulta de versão dentro do prazo.");
    }
    if (version[0] != 1 || version[1] < 4 || version[1] > 99 || (version[1] == 4 && version[2] < 1))
        return fail(SearchErrorCode::Unsupported, "Esta versão do Everything não oferece o protocolo de busca necessário (1.4.1 ou 1.5).");
    summary.source = QString("Everything %1.%2.%3.%4").arg(qulonglong(version[0])).arg(qulonglong(version[1])).arg(qulonglong(version[2])).arg(qulonglong(version[3]));
    summary.source += instanceSuffix;
    if (!send(WM_USER, 401, 0, loaded)) {
        if (cancel) { summary.state = SearchState::Cancelled; return summary; }
        return fail(SearchErrorCode::Timeout, "Everything não respondeu sobre a disponibilidade do índice.");
    }
    if (loaded > 1) return fail(SearchErrorCode::Protocol, "Everything informou um estado de índice incompatível.");
    if (!loaded) return fail(SearchErrorCode::NotReady, "O índice do Everything ainda não está pronto. A busca direta continua disponível.");
    if (!channel.createReply()) return fail(SearchErrorCode::Internal, "Não foi possível preparar a comunicação local com o Everything.");

    // Quote each operand. The regex comes from the shared matcher, never from raw query syntax.
    auto root = QDir::toNativeSeparators(prepared.root); if (!root.endsWith('\\')) root += '\\';
    auto pattern = prepared.namePattern.pattern(); pattern.replace('"', "\\x{22}");
    const QString scope = "path:\"" + root + '"';
    const auto &literal = prepared.literalName;
    // Keep ordinary filename searches on the engine's native text matcher.
    // Explicit modifiers prevent user preferences from changing literal semantics.
    const bool nativeLiteral = version[1] >= 5 && literal && !literal->contains('"') && !literal->contains('&') && !literal->contains('\\');
    const QString nameQuery = nativeLiteral ? "no-substitute:no-wildcards:diacritics:\"" + *literal + '"'
        : "diacritics:regex:\"(?i)" + pattern + '"';
    const QString queryText = scope + ' ' + (prepared.matchPath ? "path:" : "nopath:") + nameQuery;
    const auto queryChars = queryText.toStdWString();
    QByteArray packet(qsizetype(sizeof(EverythingIpc::Query) + (queryChars.size() + 1) * sizeof(wchar_t)), '\0');
    qsizetype retainedChars = 0;
    constexpr qsizetype MaxRetainedChars = 8 * 1024 * 1024;
    summary.state = SearchState::Completed;
    quint32 nextOffset = 0;
    for (;;) {
        if (cancel) { summary.state = SearchState::Cancelled; break; }
        if (deadline.elapsed() >= request.timeoutMs) return fail(SearchErrorCode::Timeout, "O prazo da busca no Everything foi atingido; os resultados recebidos foram preservados.");
        channel.offset = nextOffset;
        // Result and examined-candidate limits are independent: rejected live
        // metadata must not hide matching candidates on subsequent pages.
        channel.capacity = qMin<quint32>(EverythingIpc::PageSize,
            qMin<quint32>(prepared.filters == FolderRules{} ? quint32(request.maxResults - summary.matches) : EverythingIpc::PageSize,
                quint32(request.maxVisited - nextOffset)));
        channel.replyId = QRandomGenerator::global()->generate(); if (!channel.replyId) channel.replyId = 1;
        channel.result.reset();
        const EverythingIpc::Query query{quint32(reinterpret_cast<quintptr>(channel.reply)), channel.replyId, 0,
            channel.offset, channel.capacity, EverythingIpc::FullPath, EverythingIpc::NameAscending};
        std::memcpy(packet.data(), &query, sizeof(query));
        std::memcpy(packet.data() + sizeof(query), queryChars.c_str(), (queryChars.size() + 1) * sizeof(wchar_t));
        COPYDATASTRUCT data{EverythingIpc::Query2W, DWORD(packet.size()), packet.data()}; DWORD_PTR accepted = 0;
        if (!send(WM_COPYDATA, reinterpret_cast<WPARAM>(channel.reply), reinterpret_cast<LPARAM>(&data), accepted) || !accepted) {
            if (cancel) { summary.state = SearchState::Cancelled; break; }
            return fail(SearchErrorCode::Timeout, "Everything não aceitou a consulta ou deixou de responder.");
        }
        while (!channel.result && !cancel && deadline.elapsed() < request.timeoutMs && channel.alive()) {
            MSG message;
            while (PeekMessageW(&message, channel.reply, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
            if (!channel.result) MsgWaitForMultipleObjects(0, nullptr, FALSE, 25, QS_ALLINPUT);
        }
        if (cancel) { summary.state = SearchState::Cancelled; break; }
        if (!channel.result) return fail(SearchErrorCode::Timeout, "Everything não concluiu a busca dentro do prazo. Resultados recebidos foram preservados.");
        if (auto *error = std::get_if<SearchFailure>(&*channel.result)) return fail(error->code, error->message);
        const auto page = std::get<EverythingIpc::Page>(std::move(*channel.result));
        if (summary.indexedTotal && *summary.indexedTotal != page.total)
            return fail(SearchErrorCode::Protocol, "O índice mudou durante a paginação. Repita a busca para obter um novo resultado.");
        summary.indexedTotal = page.total;
        // Validate the whole page before counting/delivering it. A case-insensitive
        // path prefix alone is insufficient for Windows case-sensitive directories.
        QSet<QString> verifiedRoots;
        for (const auto &entry : page.entries) {
            if (!prepared.contains(entry.path) || !prepared.matchesPath(entry.path))
                return fail(SearchErrorCode::Protocol, "Everything retornou um resultado fora da pasta ou do nome solicitado.");
            const auto resultRoot = entry.path.left(prepared.root.size());
            if (!verifiedRoots.contains(resultRoot)) {
                const auto probe = directoryIdentity(resultRoot);
                if (const auto *error = std::get_if<SearchFailure>(&probe)) return fail(error->code, error->message);
                if (std::get<DirectoryIdentity>(probe) != rootIdentity)
                    return fail(SearchErrorCode::Protocol, "A pasta dos resultados não corresponde à identidade da pasta solicitada.");
                verifiedRoots.insert(resultRoot);
            }
        }
        QVector<SearchEntry> batch; batch.reserve(page.entries.size());
        QHash<QString, DWORD> parentAttributes;
        qsizetype parentCacheChars = 0;
        quint32 examined = 0;
        for (const auto &entry : page.entries) {
            if (cancel) { summary.state = SearchState::Cancelled; break; }
            if (deadline.elapsed() >= request.timeoutMs) {
                fail(SearchErrorCode::Timeout, "O prazo da busca foi atingido durante a conferência dos resultados."); break;
            }
            ++summary.visited;
            ++examined;
            bool blockedParent = false;
            int depth = 0;
            // Check ancestors from the requested root outward, stopping at the
            // first junction/link. Cache only within this already bounded page.
            for (qsizetype slash = entry.path.indexOf('/', prepared.root.size() + (prepared.root.endsWith('/') ? 0 : 1));
                 slash >= 0; slash = entry.path.indexOf('/', slash + 1)) {
                const auto parent = entry.path.left(slash);
                if (++depth > 256) {
                    ++summary.errors; blockedParent = true;
                    if (summary.details.size() < 20) summary.details.append("Limite de profundidade atingido na conferência do índice.");
                    break;
                }
                DWORD attributes;
                const auto cached = parentAttributes.constFind(parent);
                if (cached == parentAttributes.cend()) {
                    const auto nativeParent = WindowsPath::forFileApi(parent).toStdWString();
                    attributes = GetFileAttributesW(nativeParent.c_str());
                    if (parentAttributes.size() < 1024 && parentCacheChars + parent.size() <= 1024 * 1024) {
                        parentAttributes.insert(parent, attributes); parentCacheChars += parent.size();
                    }
                } else attributes = cached.value();
                if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    ++summary.errors; blockedParent = true;
                    if (summary.details.size() < 20) summary.details.append(QDir::toNativeSeparators(parent) + ": pasta do índice indisponível ou alterada.");
                    break;
                }
                if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) { ++summary.skippedLinks; blockedParent = true; break; }
            }
            if (blockedParent) continue;
            const auto native = WindowsPath::forFileApi(entry.path).toStdWString();
            const auto attributes = GetFileAttributesW(native.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || bool(attributes & FILE_ATTRIBUTE_DIRECTORY) != entry.directory) {
                ++summary.errors;
                if (summary.details.size() < 20) summary.details.append(QDir::toNativeSeparators(entry.path) + ": item do índice indisponível ou alterado.");
                continue;
            }
            if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) { ++summary.skippedLinks; continue; }
            const auto filtered = prepared.accepts(entry);
            if (const auto *error = std::get_if<SearchFailure>(&filtered)) {
                ++summary.errors;
                if (summary.details.size() < 20) summary.details.append(error->message);
                continue;
            }
            if (!std::get<bool>(filtered)) continue;
            if (retainedChars + entry.path.size() > MaxRetainedChars) { summary.state = SearchState::Limited; break; }
            retainedChars += entry.path.size(); batch.append(entry); ++summary.matches;
            if (summary.matches >= quint64(request.maxResults)) {
                if (nextOffset + examined < page.total) summary.state = SearchState::Limited;
                break;
            }
        }
        if (update) update(batch, summary);
        if (summary.state == SearchState::Limited || summary.state == SearchState::Cancelled || summary.state == SearchState::Failed) break;
        nextOffset += examined;
        if (nextOffset >= page.total) break;
        if (summary.matches >= quint64(request.maxResults) || nextOffset >= request.maxVisited) { summary.state = SearchState::Limited; break; }
    }
    if (summary.state == SearchState::Completed && summary.errors) summary.state = SearchState::Partial;
    return summary;
}
