#include "foldercomparison.h"
#include "windowspath.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QMap>
#include <QSet>
#include <filesystem>
#include <vector>
#include <windows.h>

namespace {
namespace fs = std::filesystem;
constexpr qsizetype MaxPathCharacters = 2 * 1024 * 1024;
constexpr DWORD BlockSize = 256 * 1024;
quint64 value(const FILETIME &time) { return (quint64(time.dwHighDateTime) << 32) | time.dwLowDateTime; }
quint64 size(const WIN32_FILE_ATTRIBUTE_DATA &data) { return (quint64(data.nFileSizeHigh) << 32) | data.nFileSizeLow; }
quint64 size(const BY_HANDLE_FILE_INFORMATION &data) { return (quint64(data.nFileSizeHigh) << 32) | data.nFileSizeLow; }
QString windowsError(DWORD code) { return QCoreApplication::translate("FolderComparison", "Erro do Windows %1").arg(code); }
struct Record {
    QString path;
    DWORD attributes = 0;
    quint64 bytes = 0, modified = 0;
    QString error;
    bool directory() const { return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0; }
    bool link() const { return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0; }
    QString describe() const {
        if (!error.isEmpty()) return error;
        if (link()) return QStringLiteral("Link/junção — ignorado");
        if (directory()) return QStringLiteral("Pasta");
        const auto stamp = QDateTime::fromMSecsSinceEpoch(qint64(modified / 10000) - 11644473600000LL).toLocalTime();
        return QStringLiteral("%1 bytes · %2").arg(bytes).arg(stamp.toString("yyyy-MM-dd HH:mm:ss.zzz"));
    }
};
struct Inventory {
    QMap<QString, Record> records;
    QStringList unexamined;
    bool failed = false, limited = false;
    bool absenceKnown(const QString &relative) const {
        for (const auto &prefix : unexamined)
            if (prefix.isEmpty() || relative == prefix || relative.startsWith(prefix + '/')) return false;
        return true;
    }
};
void error(ComparisonReport &report, const QString &path, const QString &reason) {
    ++report.errors;
    if (report.details.size() < 20) report.details.append(QDir::toNativeSeparators(path) + ": " + reason);
}
Inventory inventory(const QString &root, const ComparisonRequest &request, ComparisonReport &report,
                    const std::atomic_bool &cancel, const ComparisonProgress &progress) {
    Inventory result;
    const auto rootNative = WindowsPath::forFileApi(root).toStdWString();
    const auto attributes = GetFileAttributesW(rootNative.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        result.failed = true;
        error(report, root, QStringLiteral("Pasta indisponível ou raiz é link/junção. Escolha a pasta de destino diretamente."));
        return result;
    }
    struct Frame { fs::path path; QString relative; fs::directory_iterator iterator; int depth; };
    std::vector<Frame> stack;
    auto enter = [&](const fs::path &path, const QString &relative, int depth) {
        // Revalidate at the traversal boundary, after callbacks/UI may have changed the item.
        const auto currentAttributes = GetFileAttributesW(path.c_str());
        if (currentAttributes == INVALID_FILE_ATTRIBUTES || !(currentAttributes & FILE_ATTRIBUTE_DIRECTORY)
            || (currentAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            result.unexamined.append(relative);
            error(report, QDir(root).filePath(relative), QStringLiteral("A pasta mudou, ficou indisponível ou passou a ser link/junção; não foi percorrida."));
            return;
        }
        std::error_code ec; fs::directory_iterator it(path, ec);
        if (ec) {
            result.unexamined.append(relative);
            error(report, root + '/' + relative, QString::fromStdString(ec.message()));
            return;
        }
        stack.push_back({path, relative, std::move(it), depth});
    };
    enter(fs::path(rootNative), {}, 0);
    if (stack.empty()) { result.failed = true; return result; }
    qsizetype pathCharacters = 0;
    while (!stack.empty() && !cancel) {
        auto &frame = stack.back();
        if (frame.iterator == fs::directory_iterator{}) { stack.pop_back(); continue; }
        if (result.records.size() >= request.maxEntriesPerSide) { result.limited = true; result.unexamined = {QString{}}; break; }
        const auto entry = *frame.iterator;
        const auto relative = frame.relative.isEmpty() ? QString::fromStdWString(entry.path().filename().native())
            : frame.relative + '/' + QString::fromStdWString(entry.path().filename().native());
        const int childDepth = frame.depth + 1;
        std::error_code ec; frame.iterator.increment(ec);
        if (ec) {
            result.unexamined.append(frame.relative); error(report, root + '/' + frame.relative, QString::fromStdString(ec.message())); frame.iterator = {};
        }
        const qsizetype retainedCharacters = root.size() + 1 + 2 * relative.size();
        if (retainedCharacters > MaxPathCharacters - pathCharacters) { result.limited = true; result.unexamined = {QString{}}; break; }
        pathCharacters += retainedCharacters;
        Record record; record.path = QDir(root).filePath(relative);
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(entry.path().c_str(), GetFileExInfoStandard, &data)) {
            record.error = windowsError(GetLastError()); result.unexamined.append(relative); error(report, record.path, record.error);
        } else {
            record.attributes = data.dwFileAttributes; record.bytes = size(data); record.modified = value(data.ftLastWriteTime);
        }
        result.records.insert(relative, record); ++report.examined;
        if (progress) progress(report.examined, report.bytesRead);
        if (record.link()) {
            ++report.skippedLinks; result.unexamined.append(relative);
        } else if (record.error.isEmpty() && record.directory() && request.recursive) {
            if (childDepth >= request.maxDepth) {
                result.limited = true; result.unexamined.append(relative);
            } else enter(entry.path(), relative, childDepth);
        }
    }
    return result;
}
class Handle {
public:
    explicit Handle(const QString &path) : value(CreateFileW(WindowsPath::forFileApi(path).toStdWString().c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    HANDLE value;
};
bool sameFile(const BY_HANDLE_FILE_INFORMATION &a, const BY_HANDLE_FILE_INFORMATION &b) {
    return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow
        && a.dwFileAttributes == b.dwFileAttributes && size(a) == size(b) && value(a.ftLastWriteTime) == value(b.ftLastWriteTime);
}
bool matches(const Record &record, const BY_HANDLE_FILE_INFORMATION &info) {
    return !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        && record.bytes == size(info) && record.modified == value(info.ftLastWriteTime);
}
ComparisonKind content(const Record &left, const Record &right, ComparisonReport &report,
                       const std::atomic_bool &cancel, const ComparisonProgress &progress, QString &detail) {
    Handle a(left.path), b(right.path);
    if (a.value == INVALID_HANDLE_VALUE || b.value == INVALID_HANDLE_VALUE) {
        detail = QStringLiteral("Não foi possível abrir os dois arquivos para leitura estável. Um arquivo pode estar em uso ou indisponível.");
        return ComparisonKind::Unknown;
    }
    BY_HANDLE_FILE_INFORMATION beforeA{}, beforeB{};
    if (!GetFileInformationByHandle(a.value, &beforeA) || !GetFileInformationByHandle(b.value, &beforeB)
        || !matches(left, beforeA) || !matches(right, beforeB)) {
        detail = QStringLiteral("O arquivo mudou desde a listagem ou não é um arquivo regular. Compare novamente."); return ComparisonKind::Unknown;
    }
    QByteArray blockA(BlockSize, Qt::Uninitialized), blockB(BlockSize, Qt::Uninitialized);
    bool equal = true; quint64 offset = 0;
    while (offset < left.bytes) {
        if (cancel) { detail = QStringLiteral("Conferência de conteúdo cancelada."); return ComparisonKind::Unknown; }
        const DWORD amount = DWORD(qMin<quint64>(BlockSize, left.bytes - offset)); DWORD readA = 0, readB = 0;
        if (!ReadFile(a.value, blockA.data(), amount, &readA, nullptr) || !ReadFile(b.value, blockB.data(), amount, &readB, nullptr)
            || readA != amount || readB != amount) {
            detail = QStringLiteral("Leitura incompleta ou arquivo alterado durante a conferência."); return ComparisonKind::Unknown;
        }
        report.bytesRead += quint64(readA) + readB; offset += amount;
        if (progress) progress(report.examined, report.bytesRead);
        if (memcmp(blockA.constData(), blockB.constData(), amount) != 0) { equal = false; break; }
    }
    BY_HANDLE_FILE_INFORMATION afterA{}, afterB{};
    if (!GetFileInformationByHandle(a.value, &afterA) || !GetFileInformationByHandle(b.value, &afterB)
        || !sameFile(beforeA, afterA) || !sameFile(beforeB, afterB)) {
        detail = QStringLiteral("O arquivo mudou durante a conferência. Compare novamente."); return ComparisonKind::Unknown;
    }
    detail = equal ? QStringLiteral("Todos os bytes foram comparados diretamente; tamanho e identidade dos arquivos abertos permaneceram estáveis.")
                   : QStringLiteral("Foi encontrada uma diferença nos bytes dos arquivos abertos.");
    return equal ? ComparisonKind::ContentEqual : ComparisonKind::ContentDifferent;
}
}

ComparisonReport compareFolders(const ComparisonRequest &request, const std::atomic_bool &cancel, const ComparisonProgress &progress) {
    ComparisonReport report;
    auto validRoot = [](const QString &path) { return QDir::isAbsolutePath(path) && !path.contains(QChar(0)) && !path.startsWith("//?/") && !path.startsWith("\\\\?\\"); };
    if (!validRoot(request.left) || !validRoot(request.right) || request.maxEntriesPerSide < 1 || request.maxEntriesPerSide > 5000
        || request.maxDepth < 1 || request.maxDepth > 128) {
        error(report, {}, QStringLiteral("Escolha duas pastas absolutas válidas e limites suportados.")); return report;
    }
    if (cancel) { report.state = ComparisonState::Cancelled; return report; }
    auto normalized = request; normalized.left = QDir::cleanPath(QDir::fromNativeSeparators(request.left)); normalized.right = QDir::cleanPath(QDir::fromNativeSeparators(request.right));
    const auto left = inventory(normalized.left, normalized, report, cancel, progress);
    if (cancel) { report.state = ComparisonState::Cancelled; return report; }
    const auto right = inventory(normalized.right, normalized, report, cancel, progress);
    if (left.failed || right.failed) return report;
    // Exact names preserve case-sensitive Windows directories and avoid ambiguous pairing.
    QSet<QString> names; for (auto it = left.records.cbegin(); it != left.records.cend(); ++it) names.insert(it.key());
    for (auto it = right.records.cbegin(); it != right.records.cend(); ++it) names.insert(it.key());
    auto ordered = names.values(); ordered.sort(Qt::CaseSensitive);
    report.entries.reserve(ordered.size());
    for (const auto &name : ordered) {
        if (cancel) break;
        const auto a = left.records.constFind(name), b = right.records.constFind(name);
        const bool hasA = a != left.records.cend(), hasB = b != right.records.cend();
        ComparisonEntry entry; entry.relativePath = name;
        if (hasA) { entry.leftPath = a->path; entry.leftDescription = a->describe(); }
        if (hasB) { entry.rightPath = b->path; entry.rightDescription = b->describe(); }
        if ((hasA && !a->error.isEmpty()) || (hasB && !b->error.isEmpty())) entry.detail = QStringLiteral("Não foi possível examinar os metadados deste item.");
        else if ((hasA && a->link()) || (hasB && b->link())) {
            entry.kind = ComparisonKind::IgnoredLink; entry.detail = QStringLiteral("Links e junções não são seguidos nem comparados.");
        } else if (!hasA || !hasB) {
            const auto &other = hasA ? right : left;
            if (!other.absenceKnown(name)) entry.detail = QStringLiteral("A outra pasta não pôde ser examinada por completo; ausência não confirmada.");
            else entry.kind = hasA ? ComparisonKind::LeftOnly : ComparisonKind::RightOnly;
        } else if (a->directory() != b->directory()) entry.kind = ComparisonKind::TypeConflict;
        else if (a->directory()) {
            entry.kind = ComparisonKind::Folders; entry.detail = normalized.recursive ? QStringLiteral("Pastas correspondentes. Os itens internos têm resultados próprios; esta linha não confirma igualdade da árvore.")
                : QStringLiteral("Pastas correspondentes; conteúdo interno fora do escopo (subpastas desmarcadas).");
        } else if (normalized.content) {
            if (a->bytes != b->bytes) { entry.kind = ComparisonKind::ContentDifferent; entry.detail = QStringLiteral("Tamanhos diferentes comprovam conteúdo diferente; não foi necessário ler os bytes."); }
            else {
                entry.kind = content(*a, *b, report, cancel, progress, entry.detail);
                if (entry.kind == ComparisonKind::Unknown && !cancel) error(report, a->path, entry.detail);
            }
        } else {
            entry.kind = a->bytes == b->bytes && a->modified == b->modified ? ComparisonKind::MetadataMatch : ComparisonKind::MetadataDifferent;
            entry.detail = QStringLiteral("Comparação somente de tamanho e data de modificação; o conteúdo não foi conferido.");
        }
        report.entries.append(std::move(entry));
    }
    report.state = cancel ? ComparisonState::Cancelled : (left.limited || right.limited) ? ComparisonState::Limited
        : report.errors ? ComparisonState::Partial : ComparisonState::Completed;
    return report;
}

QString comparisonLabel(ComparisonKind kind) {
    const char *text = "Não confirmado";
    switch (kind) {
    case ComparisonKind::LeftOnly: text = "Somente à esquerda"; break;
    case ComparisonKind::RightOnly: text = "Somente à direita"; break;
    case ComparisonKind::MetadataMatch: text = "Tamanho/data iguais — conteúdo não conferido"; break;
    case ComparisonKind::MetadataDifferent: text = "Tamanho/data diferentes"; break;
    case ComparisonKind::ContentEqual: text = "Conteúdo igual (bytes conferidos)"; break;
    case ComparisonKind::ContentDifferent: text = "Conteúdo diferente"; break;
    case ComparisonKind::Folders: text = "Pasta em ambos"; break;
    case ComparisonKind::TypeConflict: text = "Tipos diferentes (arquivo/pasta)"; break;
    case ComparisonKind::IgnoredLink: text = "Link/junção ignorado"; break;
    case ComparisonKind::Unknown: break;
    }
    return QCoreApplication::translate("FolderComparison", text);
}
ComparisonJob::ComparisonJob(ComparisonRequest request, QObject *parent) : QThread(parent), m_request(std::move(request)) {}
ComparisonJob::~ComparisonJob() { cancel(); wait(); }
ComparisonReport ComparisonJob::takeReport() { Q_ASSERT(!isRunning()); return std::move(m_report); }
void ComparisonJob::run() {
    m_report = compareFolders(m_request, m_cancel, [this](quint64 examined, quint64 bytes) { m_examined = examined; m_bytesRead = bytes; });
}
