#include "batchrename.h"
#include "renamehistory.h"
#include "windowspath.h"
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QRegularExpression>
#include <windows.h>
#include <cstddef>
#include <vector>

namespace {
class Handle {
public:
    explicit Handle(const QString &path, DWORD access = FILE_READ_ATTRIBUTES, DWORD sharing = FILE_SHARE_READ | FILE_SHARE_WRITE) {
        value = CreateFileW(WindowsPath::forFileApi(path).toStdWString().c_str(), access, sharing,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    }
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle &) = delete;
    HANDLE value = INVALID_HANDLE_VALUE;
};
quint64 number(const FILETIME &time) { return (quint64(time.dwHighDateTime) << 32) | time.dwLowDateTime; }
RenameIdentity identity(const BY_HANDLE_FILE_INFORMATION &info) {
    return {info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow, info.dwFileAttributes,
        (quint64(info.nFileSizeHigh) << 32) | info.nFileSizeLow, number(info.ftLastWriteTime), number(info.ftCreationTime)};
}
QDateTime date(quint64 ticks) { return QDateTime::fromMSecsSinceEpoch(qint64(ticks / 10000) - 11644473600000LL).toLocalTime(); }
QString winError() { return QStringLiteral("Erro do Windows %1. O item pode estar em uso ou sem permissão.").arg(GetLastError()); }
QString key(const QString &path) { return QDir::cleanPath(path).toCaseFolded(); }
QString available(const BatchRenameItem &item) {
    if (key(item.target) == key(item.source)) return {};
    const auto attrs = GetFileAttributesW(WindowsPath::forFileApi(item.target).toStdWString().c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) return QStringLiteral("Já existe um item com o nome final. Trocas/ciclos de nomes não são aplicados nesta etapa.");
    const auto code = GetLastError();
    if (code != ERROR_FILE_NOT_FOUND) return QStringLiteral("Não foi possível confirmar que o nome final está livre (Windows %1).").arg(code);
    return {};
}
bool appendName(QString &target, QStringView part) {
    if (part.size() > 255 - target.size()) return false;
    target.append(part); return true;
}
QString replaceLiteral(QStringView source, const BatchRenameOptions &options, bool &ok) {
    QString output; qsizetype position = 0;
    while (position < source.size()) {
        const auto found = options.find.isEmpty() ? -1 : source.indexOf(options.find, position, options.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
        if (found < 0) { ok = appendName(output, source.mid(position)); break; }
        if (!appendName(output, source.mid(position, found - position)) || !appendName(output, options.replacement)) { ok = false; break; }
        position = found + options.find.size();
    }
    return output;
}
RenameProbe inspectRenameHandle(HANDLE nativeHandle) {
    RenameProbe result;
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(nativeHandle, &info)) { result.error = winError(); return result; }
    if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { result.error = "Links e junções não são renomeados em lote."; return result; }
    if (!info.nFileIndexHigh && !info.nFileIndexLow) { result.error = "O sistema de arquivos não confirmou uma identidade estável para o item."; return result; }
    const auto required = GetFinalPathNameByHandleW(nativeHandle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!required || required > 32768) { result.error = "Não foi possível confirmar o nome atual do item."; return result; }
    std::vector<wchar_t> name(required + 1);
    const auto length = GetFinalPathNameByHandleW(nativeHandle, name.data(), DWORD(name.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!length || length >= name.size()) { result.error = "Não foi possível confirmar o nome atual do item."; return result; }
    result.actualName = QFileInfo(WindowsPath::fromFileApi(QString::fromWCharArray(name.data(), int(length)))).fileName();
    result.identity = identity(info); result.state = RenameProbeState::Present; return result;
}
QString checkSource(const BatchRenameItem &item) {
    const auto current = inspectRenameItem(item.source);
    if (current.state != RenameProbeState::Present) return current.error;
    return current.identity == item.identity && current.actualName == QFileInfo(item.source).fileName() ? QString{} : QStringLiteral("A origem ou seu nome mudou desde a prévia. Gere uma nova prévia.");
}
}
RenameProbe inspectRenameItem(const QString &path) {
    Handle handle(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
    if (handle.value == INVALID_HANDLE_VALUE) {
        const auto code = GetLastError();
        RenameProbe result; result.state = code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND ? RenameProbeState::Missing : RenameProbeState::Failed;
        result.error = QStringLiteral("Não foi possível confirmar o item (Windows %1).").arg(code); return result;
    }
    return inspectRenameHandle(handle.value);
}
bool BatchRenamePlan::valid() const {
    if (cancelled || !error.isEmpty() || items.isEmpty() || items.size() > 1000) return false;
    for (const auto &item : items) if (!item.error.isEmpty()) return false;
    return true;
}
int BatchRenamePlan::changes() const { int count = 0; for (const auto &item : items) if (!item.unchanged) ++count; return count; }
BatchRenamePlan planBatchRename(const QStringList &sources, const BatchRenameOptions &options, const std::atomic_bool &cancel) {
    BatchRenamePlan plan;
    if (sources.isEmpty() || sources.size() > 1000) { plan.error = "Selecione de 1 a 1.000 itens da mesma pasta."; return plan; }
    if (options.pattern.size() > 1024 || options.find.size() > 1024 || options.replacement.size() > 1024 || options.start < 0 || options.start > 1000000000
        || options.step < 1 || options.step > 1000000 || options.digits < 1 || options.digits > 10
        || !QStringList{"yyyy-MM-dd", "yyyyMMdd", "yyyy-MM-dd_HHmmss"}.contains(options.dateFormat)
        || (options.dateSource != RenameDateSource::Modified && options.dateSource != RenameDateSource::Created && options.dateSource != RenameDateSource::Fixed)
        || (options.dateSource == RenameDateSource::Fixed && !options.fixedDate.isValid())) {
        plan.error = "As opções excedem os limites ou a data é inválida."; return plan;
    }
    // Parse the template once. Inserted names/replacement text are never parsed as tokens.
    static const QRegularExpression tokens("\\{(name|n|date)\\}");
    QString residue = options.pattern; residue.remove(tokens);
    if (residue.contains('{') || residue.contains('}')) { plan.error = "Use somente os campos {name}, {n} e {date}."; return plan; }
    QString parent; QSet<QString> seen; QHash<QString, int> targets; qsizetype characters = 0;
    for (int i = 0; i < sources.size(); ++i) {
        if (cancel) { plan.cancelled = true; break; }
        BatchRenameItem item; item.source = QDir::cleanPath(QDir::fromNativeSeparators(sources[i]));
        characters += item.source.size();
        if (characters > 1024 * 1024) { plan.error = "A seleção excede o orçamento de caminhos (2 MiB UTF-16)."; break; }
        const QFileInfo info(item.source); const auto folder = info.absolutePath();
        if (!QDir::isAbsolutePath(sources[i]) || sources[i].contains(QChar(0)) || info.fileName().isEmpty() || QDir(item.source).isRoot()) item.error = "Selecione arquivos ou pastas com caminho completo, sem raízes de unidade.";
        else if (!parent.isEmpty() && folder != parent) item.error = "Todos os itens devem estar na mesma pasta.";
        else if (seen.contains(key(item.source))) item.error = "Item duplicado na seleção.";
        if (parent.isEmpty()) parent = folder;
        seen.insert(key(item.source));
        if (item.error.isEmpty()) { const auto probe = inspectRenameItem(item.source); if (probe.state != RenameProbeState::Present) item.error = probe.error; else item.identity = probe.identity; }
        QString base = info.fileName(), suffix;
        if (options.preserveExtension && !(item.identity.attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            const int dot = base.lastIndexOf('.'); if (dot > 0) { suffix = base.mid(dot); base.truncate(dot); }
        }
        bool bounded = true;
        if (options.pattern.contains("{name}")) base = replaceLiteral(base, options, bounded);
        const QDateTime stamp = options.dateSource == RenameDateSource::Created ? date(item.identity.created)
            : options.dateSource == RenameDateSource::Fixed ? QDateTime(options.fixedDate, QTime(0, 0))
            : date(item.identity.modified);
        QString name; qsizetype end = 0; auto matches = tokens.globalMatch(options.pattern);
        while (bounded && matches.hasNext()) { const auto match = matches.next();
            bounded = appendName(name, QStringView(options.pattern).mid(end, match.capturedStart() - end));
            const auto token = match.captured(1); const auto value = token == "name" ? base : token == "n" ? QString::number(qint64(options.start) + qint64(i) * options.step).rightJustified(options.digits, '0') : stamp.toString(options.dateFormat);
            if (bounded) bounded = appendName(name, value);
            end = match.capturedEnd();
        }
        if (bounded) bounded = appendName(name, QStringView(options.pattern).mid(end)) && appendName(name, suffix);
        if (!bounded && item.error.isEmpty()) item.error = "O nome final excede 255 caracteres.";
        item.name = bounded ? name : QString{}; item.target = QDir(folder).filePath(item.name); item.unchanged = item.name == info.fileName();
        if (item.error.isEmpty()) item.error = validateItemName(item.name);
        if (item.error.isEmpty()) item.error = available(item);
        const auto targetKey = key(item.target);
        if (targets.contains(targetKey)) { item.error = "Dois itens produziriam o mesmo nome final."; plan.items[targets.value(targetKey)].error = item.error; }
        else targets.insert(targetKey, int(plan.items.size()));
        plan.items.append(std::move(item));
    }
    return plan;
}
OperationResult performBatchRename(const BatchRenamePlan &plan, const std::atomic_bool &cancel, const OperationProgress &progress) {
    OperationResult result;
    for (const auto &item : plan.items) result.items.append({item.source, item.target, OperationItemState::NotStarted, {}, {}});
    if (cancel) { result.state = OperationState::Cancelled; return result; }
    if (!plan.valid()) { result.error = "A prévia contém erros ou não é válida."; return result; }
    const auto parent = QFileInfo(plan.items.first().source).absolutePath(); Handle directory(parent);
    BY_HANDLE_FILE_INFORMATION directoryInfo{};
    if (directory.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(directory.value, &directoryInfo)
        || !(directoryInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (directoryInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { result.error = "A pasta de origem não está disponível ou é link/junção."; return result; }
    // Use the pinned directory's resolved path and the documented absolute-path
    // form. Relative RootDirectory handles returned Windows error 87 in testing.
    const DWORD required = GetFinalPathNameByHandleW(directory.value, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!required || required > 32768) { result.error = "Não foi possível resolver a pasta de origem."; return result; }
    std::vector<wchar_t> directoryName(required + 1);
    const DWORD length = GetFinalPathNameByHandleW(directory.value, directoryName.data(), DWORD(directoryName.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!length || length >= directoryName.size()) { result.error = "A pasta de origem não pôde ser confirmada."; return result; }
    const auto resolvedDirectory = QString::fromWCharArray(directoryName.data(), int(length));
    QSet<QString> sources, targets;
    // Revalidate the entire approved plan before the first mutation.
    for (int i = 0; i < plan.items.size(); ++i) {
        if (cancel) { result.state = OperationState::Cancelled; return result; }
        const auto &item = plan.items[i]; auto &out = result.items[i];
        if (QFileInfo(item.source).absolutePath() != parent || item.target != QDir(parent).filePath(item.name)
            || !validateItemName(item.name).isEmpty() || sources.contains(key(item.source)) || targets.contains(key(item.target))) out.error = "O plano de renomeação é inconsistente.";
        else out.error = checkSource(item);
        if (out.error.isEmpty()) out.error = available(item);
        if (!out.error.isEmpty()) { result.error = out.error; out.state = OperationItemState::Failed; return result; }
        sources.insert(key(item.source)); targets.insert(key(item.target));
    }
    quint32 done = 0;
    for (int i = 0; i < plan.items.size(); ++i) {
        const auto &item = plan.items[i]; auto &out = result.items[i];
        if (progress) progress(quint32(plan.items.size()), done, item.source);
        if (cancel) { result.state = OperationState::Cancelled; return result; }
        if (item.unchanged) { out.state = OperationItemState::Skipped; ++done; continue; }
        Handle handle(item.source, DELETE | FILE_READ_ATTRIBUTES);
        const auto before = handle.value == INVALID_HANDLE_VALUE ? RenameProbe{} : inspectRenameHandle(handle.value);
        if (handle.value == INVALID_HANDLE_VALUE) out.error = winError();
        else if (before.state != RenameProbeState::Present) out.error = before.error;
        else if (before.identity != item.identity || before.actualName != QFileInfo(item.source).fileName()) out.error = "A origem ou seu nome mudou desde a prévia. Gere uma nova prévia.";
        else {
            const auto nativeTarget = resolvedDirectory + (resolvedDirectory.endsWith('\\') ? QString{} : QStringLiteral("\\")) + item.name;
            const auto nameBytes = size_t(nativeTarget.size()) * sizeof(wchar_t);
            std::vector<unsigned char> storage(offsetof(FILE_RENAME_INFO, FileName) + nameBytes + sizeof(wchar_t), 0);
            auto *rename = reinterpret_cast<FILE_RENAME_INFO *>(storage.data()); rename->ReplaceIfExists = FALSE; rename->RootDirectory = nullptr;
            rename->FileNameLength = DWORD(nameBytes); memcpy(rename->FileName, nativeTarget.utf16(), nameBytes);
            if (!SetFileInformationByHandle(handle.value, FileRenameInfo, rename, DWORD(storage.size()))) out.error = winError();
            else {
                const auto verification = inspectRenameItem(item.target); const auto &after = verification.identity;
                // The write handle does not share deletion; verification only reads attributes.
                if (verification.state != RenameProbeState::Present || after.volume != item.identity.volume || after.indexHigh != item.identity.indexHigh || after.indexLow != item.identity.indexLow || verification.actualName != item.name) {
                    out.state = OperationItemState::Incomplete; out.error = "O Windows renomeou o item, mas o destino não pôde ser confirmado.";
                } else { out.state = OperationItemState::Completed; out.renamedIdentity = after; }
            }
        }
        if (!out.error.isEmpty()) { if (out.state != OperationItemState::Incomplete) out.state = OperationItemState::Failed; result.error = out.error; result.state = done ? OperationState::Incomplete : OperationState::Failed; return result; }
        ++done;
    }
    if (progress) progress(quint32(plan.items.size()), done, {});
    result.state = OperationState::Completed; return result;
}
BatchRenameJob::BatchRenameJob(QStringList sources, BatchRenameOptions options, QObject *parent) : QThread(parent), m_sources(std::move(sources)), m_options(std::move(options)) {}
BatchRenameJob::BatchRenameJob(BatchRenamePlan plan, QObject *parent, QString historyRoot) : QThread(parent), m_apply(true), m_historyRoot(std::move(historyRoot)), m_plan(std::move(plan)) {}
BatchRenameJob::~BatchRenameJob() { cancel(); wait(); }
void BatchRenameJob::run() {
    if (m_apply) m_result = performRecordedBatchRename(m_plan, m_historyRoot, m_cancel, [this](quint32, quint32 done, const QString &) { m_done = int(done); });
    else m_plan = planBatchRename(m_sources, m_options, m_cancel);
}
