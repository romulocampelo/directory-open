#include "archive.h"
#include "fileorigin.h"
#include "fileoperation.h"
#include "batchrename.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QSet>
#include <QStorageInfo>
#include <QStringConverter>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <limits>
#include <windows.h>

namespace {
constexpr qsizetype ListingLimit = 8 * 1024 * 1024;
constexpr int EntryLimit = 10000;
bool linked(const QFileInfo &info) { return info.isSymLink() || info.isJunction(); }
QString clean(const QString &path) { return QDir::cleanPath(QDir::fromNativeSeparators(path)); }
bool within(const QString &path, const QString &parent) {
    const auto p = clean(path), base = clean(parent);
    return p.compare(base, Qt::CaseInsensitive) == 0 || p.startsWith(base + '/', Qt::CaseInsensitive);
}
QString safeMember(QString path) {
    path.replace('\\', '/');
    if (path.isEmpty() || path.size() > 30000 || path.startsWith('/') || path.endsWith('/')) return "Caminho vazio, absoluto ou longo demais no compactado.";
    for (const auto &part : path.split('/')) {
        const auto error = validateItemName(part);
        if (!error.isEmpty()) return "Nome não suportado no compactado: " + path + ". " + error;
    }
    return {};
}
struct ReadLock {
    HANDLE handle = INVALID_HANDLE_VALUE;
    explicit ReadLock(const QString &path) {
        const auto native = QDir::toNativeSeparators(path);
        handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    }
    ~ReadLock() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
struct CommandResult { ArchiveState state = ArchiveState::Failed; QByteArray output; QString error, diagnostics; };
// Only this direct child is controlled. No PID attachment or detached launcher.
CommandResult command(const QString &exe, QStringList args, const QString &cwd, const std::atomic_bool &cancel,
                      QIODevice *sink = nullptr, quint64 expectedBytes = 0) {
    CommandResult result;
    if (cancel.load()) { result.state = ArchiveState::Cancelled; return result; }
    QProcess process; process.setProgram(exe); process.setArguments(args); if (!cwd.isEmpty()) process.setWorkingDirectory(cwd);
    process.start();
    if (!process.waitForStarted(10000)) { result.error = "Não foi possível iniciar o 7-Zip: " + process.errorString(); return result; }
    process.closeWriteChannel(); // Encrypted archives cannot leave an invisible password prompt waiting.
    QByteArray diagnostics; quint64 written = 0;
    auto drain = [&] {
        while (process.bytesAvailable() > 0) {
            const auto bytes = process.read(64 * 1024);
            if (sink) {
                if (quint64(bytes.size()) > expectedBytes - written) { result.error = "O 7-Zip produziu mais dados que o tamanho listado."; break; }
                if (sink->write(bytes) != bytes.size()) { result.error = "Falha ao gravar o arquivo extraído: " + sink->errorString(); break; }
                written += quint64(bytes.size());
            } else {
                if (result.output.size() + bytes.size() > ListingLimit) { result.error = "A saída do 7-Zip excede o limite de 8 MiB."; break; }
                result.output += bytes;
            }
        }
        const auto errors = process.readAllStandardError();
        if (diagnostics.size() + errors.size() > 1024 * 1024) result.error = "O diagnóstico do 7-Zip excede 1 MiB.";
        else diagnostics += errors;
    };
    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(50); drain();
        if (cancel.load() || !result.error.isEmpty()) {
            process.terminate(); if (!process.waitForFinished(250)) { process.kill(); process.waitForFinished(-1); }
            break;
        }
    }
    drain(); process.waitForFinished(-1); result.diagnostics = QString::fromUtf8(diagnostics);
    if (cancel.load()) { result.state = ArchiveState::Cancelled; return result; }
    if (!result.error.isEmpty()) return result;
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.state = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 1 ? ArchiveState::Incomplete : ArchiveState::Failed;
        result.error = QStringLiteral("O 7-Zip não confirmou a operação (código %1). Confira o diagnóstico abaixo.").arg(process.exitCode()); return result;
    }
    if (sink && written != expectedBytes) { result.error = "O tamanho extraído difere da listagem. O arquivo não foi publicado."; return result; }
    result.state = ArchiveState::Completed; return result;
}
QStringList readArgs(const QString &verb, const ArchiveRequest &request) {
    const QStringList types{"-tzip","-t7z","-trar","-trar5"};
    return {verb, types.value(int(request.format)), "-sccUTF-8", "-bsp0", "-bb0", "-y"};
}
struct RarHeader { ArchiveFormat format=ArchiveFormat::Rar;QString error; };
RarHeader rarHeader(const QByteArray &bytes){
    const auto invalid=[](){return RarHeader{ArchiveFormat::Rar,"Cabeçalho RAR não suportado ou incompleto. Abra nas opções avançadas do 7-Zip."};};
    const auto volumes=[](){return RarHeader{ArchiveFormat::Rar,"RAR dividido em volumes: use as opções avançadas do 7-Zip. A extração integrada exige um único arquivo."};};
    if(bytes.startsWith(QByteArray::fromHex("526172211a0700"))){
        if(bytes.size()<14||quint8(bytes[9])!=0x73)return invalid();const auto flags=quint8(bytes[10])|(quint16(quint8(bytes[11]))<<8);if(flags&1)return volumes();if(flags&0x80)return invalid();return {};
    }
    if(!bytes.startsWith(QByteArray::fromHex("526172211a070100")))return invalid();
    qsizetype at=12;auto number=[&]()->std::optional<quint64>{quint64 value=0;for(int shift=0;shift<64;shift+=7){if(at>=bytes.size())return {};const auto unit=quint8(bytes[at++]);if(shift==63&&(unit&0xfe))return {};value|=quint64(unit&0x7f)<<shift;if(!(unit&0x80))return value;}return {};};
    const auto size=number();if(!size||*size>2*1024*1024)return invalid();const auto begin=at;const auto type=number(),flags=number();if(!type||*type!=1||!flags||(*flags&~quint64(5)))return invalid();if(*flags&1)if(!number())return invalid();const auto archiveFlags=number();if(!archiveFlags||(*archiveFlags&~quint64(31))||quint64(at-begin)>*size)return invalid();if(*archiveFlags&3)return volumes();return {ArchiveFormat::Rar5,{}};
}
struct OutputDirectories {
    QVector<HANDLE> handles;
    ~OutputDirectories() { for (const auto handle : handles) CloseHandle(handle); }
    QString prepare(const QString &root, const QString &relative) {
        auto lock = [&](const QString &path) -> bool {
            const auto native = QDir::toNativeSeparators(path);
            const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle == INVALID_HANDLE_VALUE) return false;
            BY_HANDLE_FILE_INFORMATION info{};
            if (!GetFileInformationByHandle(handle, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { CloseHandle(handle); return false; }
            handles.append(handle); return true;
        };
        if (!lock(root)) return "A pasta de extração mudou, está ocupada ou contém um ponto de redirecionamento.";
        if (relative.isEmpty() || relative == ".") return {};
        QString path = root;
        for (const auto &part : relative.split('/')) {
            path = QDir(path).filePath(part); QDir().mkdir(path);
            if (!lock(path)) return "Não foi possível reservar uma pasta normal dentro do destino: " + path;
        }
        return {};
    }
};
}
QString sevenZipExecutable() {
    for (const auto key : {"ProgramW6432", "ProgramFiles", "ProgramFiles(x86)"}) {
        const auto root = qEnvironmentVariable(key); if (root.isEmpty()) continue;
        const QFileInfo info(QDir(root).filePath("7-Zip/7z.exe"));
        if (info.isFile() && !linked(info)) return info.absoluteFilePath();
    }
    return {};
}
bool supportedArchive(const QString &path) {
    const auto suffix = QFileInfo(path).suffix();
    return suffix.compare("zip", Qt::CaseInsensitive) == 0 || suffix.compare("7z", Qt::CaseInsensitive) == 0 || suffix.compare("rar",Qt::CaseInsensitive)==0;
}
QString validateArchiveRequest(const ArchiveRequest &request) {
    if (request.action == ArchiveAction::Create) {
        if(request.format!=ArchiveFormat::Zip&&request.format!=ArchiveFormat::SevenZip)return "Criar compactados aceita somente ZIP e 7z. RAR tem suporte de leitura.";
        if (request.sources.isEmpty()) return "Selecione arquivos ou pastas para compactar.";
        const auto parent = QFileInfo(request.sources.first()).absolutePath(); QSet<QString> seen;
        for (const auto &source : request.sources) {
            const QFileInfo info(source);
            if (!QDir::isAbsolutePath(source) || !info.exists() || (!info.isDir() && !info.isFile()) || linked(info) || QDir(source).isRoot()) return "Origem indisponível ou não suportada: " + source;
            if (info.absolutePath().compare(parent, Qt::CaseInsensitive) != 0) return "Selecione itens de uma mesma pasta.";
            if (!validateItemName(info.fileName()).isEmpty()) return "Nome de origem não suportado: " + info.fileName();
            const auto key = info.canonicalFilePath().toCaseFolded(); if (key.isEmpty() || seen.contains(key)) return "Seleção repetida ou caminho não confirmado."; seen.insert(key);
            if (info.isDir() && within(QFileInfo(request.output).absolutePath(), info.canonicalFilePath())) return "O compactado não pode ficar dentro de uma pasta selecionada.";
        }
    } else {
        const QFileInfo info(request.archive);
        if (!QDir::isAbsolutePath(request.archive) || !info.isFile() || linked(info) || !supportedArchive(request.archive)) return "Escolha um arquivo ZIP, 7z ou RAR normal e disponível.";
    }
    if (request.action == ArchiveAction::Create || request.action == ArchiveAction::Extract) {
        const QFileInfo output(request.output), parent(output.absolutePath());
        if (!QDir::isAbsolutePath(request.output) || !validateItemName(output.fileName()).isEmpty() || !parent.isDir() || parent.canonicalFilePath().isEmpty() || linked(parent)) return "Escolha um destino completo dentro de uma pasta normal existente.";
        if (output.exists() || linked(output)) return "O destino já existe. Escolha outro nome; nada será substituído.";
        if (request.action == ArchiveAction::Create && QFileInfo(request.output).suffix().compare(request.format == ArchiveFormat::Zip ? "zip" : "7z", Qt::CaseInsensitive) != 0) return "O nome deve terminar na extensão do formato escolhido.";
    }
    return {};
}
ArchiveResult parseArchiveListing(const QByteArray &bytes) {
    ArchiveResult result;
    if (bytes.size() > ListingLimit) { result.message = "A listagem excede 8 MiB."; return result; }
    QStringDecoder decode(QStringDecoder::Utf8); QString text = decode(bytes);
    if (decode.hasError()) { result.message = "A listagem não é UTF-8 válido."; return result; }
    text.replace("\r\n", "\n");
    QMap<QString, QString> fields; QMap<QString, bool> paths;
    const QSet<QString> keys{"Path", "Folder", "Size", "Packed Size", "Modified", "Created", "Accessed", "Attributes", "Encrypted", "Comment", "CRC", "Method", "Characteristics", "Host OS", "Version", "Volume Index", "Offset", "Block", "Solid", "Anti", "Symbolic Link", "Hard Link", "Reparse Point", "Alternate Stream","Commented","Split Before","Split After","Copy Link","Checksum","NT Security"};
    auto finish = [&]() -> QString {
        if (fields.isEmpty()) return {};
        if (!fields.contains("Path") || !fields.contains("Size") || !fields.contains("Attributes") || !fields.contains("Encrypted")) return "Listagem incompleta; extração recusada.";
        ArchiveEntry entry; entry.path = fields["Path"]; entry.path.replace('\\', '/'); entry.modified = fields["Modified"];
        const auto error = safeMember(entry.path); if (!error.isEmpty()) return error;
        for (const auto ch : fields["Size"]) if (ch < '0' || ch > '9') return "Tamanho inválido na listagem.";
        bool sizeOk = false; entry.size = fields["Size"].toULongLong(&sizeOk);
        if (!sizeOk || fields["Size"].startsWith('-') || (fields["Encrypted"] != "+" && fields["Encrypted"] != "-")) return "Tamanho ou criptografia inválidos na listagem.";
        const auto attributes = fields["Attributes"];
        entry.directory = fields.contains("Folder") ? fields["Folder"] == "+" : attributes.startsWith('D');
        if (fields.contains("Folder") && fields["Folder"] != "+" && fields["Folder"] != "-") return "Tipo de item desconhecido.";
        for(const auto key:{"Anti","Alternate Stream","Split Before","Split After","Commented"})if(fields.contains(key)&&fields[key]!="-")return "Links, anti-itens, fluxos, comentários especiais ou volumes não são suportados.";
        for(const auto key:{"Symbolic Link","Hard Link","Copy Link","Reparse Point","NT Security"})if(!fields.value(key).isEmpty())return "Links, dados de segurança ou volumes não são suportados.";
        if(!fields.value("Volume Index").isEmpty()&&fields.value("Volume Index")!="0")return "Índice de volume não suportado.";
        if(attributes.contains('L')||attributes.contains('l'))return "Links não são suportados.";
        if (entry.directory && entry.size != 0) return "Pasta com tamanho ambíguo na listagem.";
        entry.encrypted = fields["Encrypted"] == "+";
        const auto key = entry.path.toCaseFolded();
        if (paths.contains(key)) return "O compactado contém nomes repetidos ou equivalentes no Windows.";
        paths.insert(key, entry.directory);
        if (result.entries.size() >= EntryLimit || entry.size > std::numeric_limits<quint64>::max() - result.totalBytes) return "A listagem excede 10.000 itens ou o tamanho representável.";
        result.totalBytes += entry.size; result.entries.append(entry); fields.clear(); return {};
    };
    for (const auto &line : text.split('\n')) {
        if (line.isEmpty()) { result.message = finish(); if (!result.message.isEmpty()) return result; continue; }
        const auto separator = line.indexOf(" = ");
        if (separator < 1 || !keys.contains(line.left(separator)) || fields.contains(line.left(separator))) { result.message = "Listagem ambígua ou campo não suportado; extração recusada."; return result; }
        fields.insert(line.left(separator), line.mid(separator + 3));
    }
    result.message = finish(); if (!result.message.isEmpty()) return result;
    for (auto it = paths.cbegin(); it != paths.cend(); ++it) {
        auto parent = it.key();
        while (parent.contains('/')) { parent.truncate(parent.lastIndexOf('/')); if (paths.contains(parent) && !paths[parent]) { result.message = "Um arquivo também aparece como pasta no compactado."; return result; } }
    }
    result.state = ArchiveState::Completed; return result;
}
ArchiveResult performArchive(const ArchiveRequest &input, const std::atomic_bool &cancel, const ArchiveProgress &progress) {
    auto request = input;
    if (request.action != ArchiveAction::Create) request.format = QFileInfo(request.archive).suffix().compare("zip", Qt::CaseInsensitive) == 0 ? ArchiveFormat::Zip : ArchiveFormat::SevenZip;
    ArchiveResult result;
    if (cancel.load()) { result.state = ArchiveState::Cancelled; return result; }
    result.message = validateArchiveRequest(request); if (!result.message.isEmpty()) return result;
    const auto exe = sevenZipExecutable(); if (exe.isEmpty()) { result.message = "7-Zip não encontrado em Arquivos de Programas."; return result; }
    auto take = [&](const CommandResult &cmd) { result.state = cmd.state; result.message = cmd.error; result.diagnostics = cmd.diagnostics; return cmd.state == ArchiveState::Completed; };
    if (request.action == ArchiveAction::Create) {
        OperationRequest sources; sources.sources = request.sources;
        result.message = validateOperationTree(sources, cancel);
        if (cancel.load()) { result.state = ArchiveState::Cancelled; return result; }
        if (!result.message.isEmpty()) return result;
        const auto parent = QFileInfo(request.output).absolutePath();
        QTemporaryDir work(QDir(parent).filePath(".diropen-pack-XXXXXX"));
        if (!work.isValid()) { result.message = "Não foi possível preparar o compactado temporário."; return result; }
        work.setAutoRemove(false);
        result = [&]() -> ArchiveResult {
        QByteArray names;
        const QDir sourceParent(QFileInfo(request.sources.first()).absolutePath());
        for (const auto &source : request.sources) {
            names += sourceParent.relativeFilePath(source).toUtf8() + '\n';
            if (names.size() > ListingLimit) { result.message = "A seleção excede 8 MiB de caminhos."; return result; }
        }
        const auto listPath = work.filePath("selection.txt"); QFile list(listPath);
        if (!list.open(QIODevice::WriteOnly) || list.write(names) != names.size()) { result.message = "Não foi possível gravar a seleção."; return result; } list.close();
        const auto packed = work.filePath(request.format == ArchiveFormat::Zip ? "archive.zip" : "archive.7z");
        QStringList args{"a", request.format == ArchiveFormat::Zip ? "-tzip" : "-t7z", "-scsUTF-8", "-sccUTF-8", "-spd", "-r-", "-mmt=2", "-mx=5", "-bb0", "-bsp0", "-y", packed, "@" + listPath};
        if (request.format == ArchiveFormat::SevenZip) args.insert(1, "-ms=off");
        if (progress) progress(0, 0, "Compactando e testando integridade…");
        if (!take(command(exe, args, sourceParent.path(), cancel))) return result;
        auto verify = readArgs("t", request); verify << "--" << packed;
        if (!take(command(exe, verify, {}, cancel))) return result;
        if (cancel.load()) { result.state = ArchiveState::Cancelled; return result; }
        // QFile::rename refuses an occupied name; never updates an existing archive.
        result.message = validateArchiveRequest(request); if (!result.message.isEmpty()) { result.state = ArchiveState::Failed; return result; }
        if (!QFile::rename(packed, request.output)) { result.state = ArchiveState::Failed; result.message = "Não foi possível publicar o compactado sem substituir um destino."; return result; }
        result.output = request.output; result.state = ArchiveState::Completed; result.message = "Compactado criado e teste de integridade aprovado pelo 7-Zip."; return result;
        }();
        if (!work.remove()) result.diagnostics += "\nPasta temporária própria preservada por falha na limpeza: " + work.path();
        return result;
    }
    ReadLock lock(request.archive);
    if (lock.handle == INVALID_HANDLE_VALUE) { result.message = "Não foi possível bloquear o compactado para leitura estável. Feche programas que o estejam alterando."; return result; }
    const auto probe = inspectRenameItem(request.archive);
    if (probe.state != RenameProbeState::Present || !probe.error.isEmpty() || (request.expectedIdentity && probe.identity != *request.expectedIdentity)) { result.message = "O compactado mudou desde a listagem ou sua identidade não pôde ser conferida. Liste novamente."; return result; }
    if(QFileInfo(request.archive).suffix().compare("rar",Qt::CaseInsensitive)==0){
        QByteArray header(128,Qt::Uninitialized);DWORD received=0;if(!ReadFile(lock.handle,header.data(),DWORD(header.size()),&received,nullptr)){result.message="Não foi possível ler o cabeçalho RAR reservado.";return result;}header.resize(received);const auto parsed=rarHeader(header);if(!parsed.error.isEmpty()){result.message=parsed.error;return result;}request.format=parsed.format;
    }
    auto args = readArgs("l", request); args << "-slt" << "-ba" << "--" << request.archive;
    const auto listing = command(exe, args, {}, cancel); if (!take(listing)) return result;
    result = parseArchiveListing(listing.output); if (result.state != ArchiveState::Completed) return result;
    result.identity = probe.identity;
    if (request.action == ArchiveAction::List) { result.message = QStringLiteral("Listagem completa; %1 itens, %2 bytes descompactados.").arg(result.entries.size()).arg(result.totalBytes); return result; }
    for (const auto &entry : result.entries) if (entry.encrypted) { result.state = ArchiveState::Failed; result.message = "Compactados com senha não são suportados nesta versão."; return result; }
    if (request.action == ArchiveAction::Test) {
        auto verify = readArgs("t", request); verify << "--" << request.archive;
        if (take(command(exe, verify, {}, cancel))) result.message = "Teste de integridade aprovado pelo 7-Zip.";
        return result;
    }
    result.message = validateArchiveRequest(request);
    if (!result.message.isEmpty()) { result.state = ArchiveState::Failed; return result; }
    std::optional<QByteArray> origin;
    result.message = readFileOrigin(request.archive, origin);
    if (!result.message.isEmpty()) { result.state = ArchiveState::Failed; return result; }
    const QStorageInfo storage(QFileInfo(request.output).absolutePath());
    if (storage.isValid() && storage.isReady() && storage.bytesAvailable() >= 0 && result.totalBytes > quint64(storage.bytesAvailable())) {
        result.state = ArchiveState::Failed; result.message = "O tamanho listado excede o espaço disponível no destino."; return result;
    }
    if (cancel.load()) { result.state = ArchiveState::Cancelled; return result; }
    if (!QDir().mkdir(request.output)) { result.state = ArchiveState::Failed; result.message = "Não foi possível criar uma pasta de destino exclusiva."; return result; }
    result.output = request.output;
    // 7-Zip writes only to stdout. This owner alone creates Windows-safe paths;
    // even a malformed member name cannot instruct the external tool to write out.
    for (int i = 0; i < result.entries.size(); ++i) {
        const auto &entry = result.entries[i];
        if (cancel.load()) { result.state = ArchiveState::Cancelled; result.message = "Extração cancelada. Arquivos já concluídos foram preservados."; return result; }
        if (progress) progress(i, int(result.entries.size()), entry.path);
        const auto target = QDir(request.output).filePath(entry.path);
        const auto relativeParent = entry.directory ? entry.path : QFileInfo(entry.path).path();
        OutputDirectories directories;
        result.message = directories.prepare(request.output, relativeParent);
        if (!result.message.isEmpty()) { result.state = ArchiveState::Incomplete; return result; }
        const auto parent = entry.directory ? target : QFileInfo(target).absolutePath();
        if (entry.directory) continue;
        QTemporaryFile file(QDir(parent).filePath(".diropen-extract-XXXXXX"));
        if (!file.open()) { result.state = ArchiveState::Incomplete; result.message = file.errorString(); return result; }
        auto extract = readArgs("x", request); extract << "-so" << "-spd" << "-ssc" << "-r-" << "--" << request.archive << entry.path;
        if (!take(command(exe, extract, {}, cancel, &file, entry.size))) {
            if (result.completedFiles && result.state == ArchiveState::Failed) result.state = ArchiveState::Incomplete;
            return result;
        }
        if (!file.flush()) { result.state = ArchiveState::Incomplete; result.message = file.errorString(); return result; }
        file.close();
        result.message = writeFileOrigin(file.fileName(), origin);
        if (!result.message.isEmpty()) { result.state = ArchiveState::Incomplete; return result; }
        if (!file.rename(target)) { result.state = ArchiveState::Incomplete; result.message = "Não foi possível publicar o arquivo sem substituir um destino: " + entry.path; return result; }
        file.setAutoRemove(false); ++result.completedFiles;
    }
    if (progress) progress(int(result.entries.size()), int(result.entries.size()), {});
    result.state = ArchiveState::Completed; result.message = QStringLiteral("Extração concluída: %1 arquivos. Os arquivos existentes foram preservados.").arg(result.completedFiles); return result;
}
void ArchiveJob::run() { m_result = performArchive(m_request, m_cancel, [this](int done, int total, const QString &path) { emit progress(done, total, path); }); }
