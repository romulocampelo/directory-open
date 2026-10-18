#include "specializedtools.h"
#include "externalprocess.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {
constexpr qsizetype maxOutput = 8 * 1024 * 1024, maxLine = 1024 * 1024, maxRows = 10000;
bool localPath(const QString &p) {
    // QDir also calls Qt resource paths (":...") absolute; these are not files.
    const auto s = QDir::fromNativeSeparators(p);
    return !p.contains(QChar(0)) && (QRegularExpression("^[A-Za-z]:/").match(s).hasMatch() || s.startsWith("//"));
}
bool inside(const QString &child, const QString &parent) {
    const auto c = QDir::cleanPath(QDir::fromNativeSeparators(child));
    auto p = QDir::cleanPath(QDir::fromNativeSeparators(parent));
    if (!p.endsWith('/')) p += '/';
    return c.compare(p.chopped(1), Qt::CaseInsensitive) == 0 || c.startsWith(p, Qt::CaseInsensitive);
}
bool snapshotId(const QString &s) { return QRegularExpression("^[a-f0-9]{64}$").match(s).hasMatch(); }
bool endpoint(const QString &s) {
    // Deliberately exclude :backend: connection strings and URLs with inline secrets.
    if (localPath(s)) return true;
    if (s.contains(QChar(0)) || s.contains('\r') || s.contains('\n')) return false;
    const auto colon = s.indexOf(':');
    // A one-letter prefix is a Windows drive, including drive-relative C:folder.
    if (colon < 2) return false;
    static const QRegularExpression name("^[\\p{L}\\p{N}_.+@][\\p{L}\\p{N}_ .+@-]*[\\p{L}\\p{N}_.+@-]$");
    return name.match(s.first(colon)).hasMatch();
}
QString jsonText(const QJsonValue &v, bool *ok) {
    if (!v.isObject()) { *ok = false; return {}; }
    const auto o = v.toObject();
    if (o["text"].isString()) return o["text"].toString();
    if (o["bytes"].isString()) {
        const auto b = QByteArray::fromBase64Encoding(o["bytes"].toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (b) return QString::fromUtf8(b.decoded);
    }
    *ok = false; return {};
}
QString diagnosticText(const QByteArray &bytes, bool simulation) {
    QStringList messages;
    for (const auto &line : bytes.split('\n')) {
        if (line.trimmed().isEmpty()) continue;
        const auto o = QJsonDocument::fromJson(line).object();
        if (o.isEmpty()) { messages << QString::fromUtf8(line); continue; }
        const auto stats = o["stats"].toObject();
        if (!stats.isEmpty()) {
            messages << (simulation ? QString("Simulados: %1 de %2 bytes; arquivos: %3; erros: %4.") : QString("Transferidos: %1 de %2 bytes; arquivos: %3; erros: %4."))
                .arg(stats["bytes"].toInteger()).arg(stats["totalBytes"].toInteger())
                .arg(stats["transfers"].toInteger()).arg(stats["errors"].toInteger());
        } else if (simulation && o["skipped"].toString() == "copy") {
            messages << QString("Seria copiado: %1 (%2 bytes).").arg(o["object"].toString()).arg(o["size"].toInteger());
        } else {
            auto message = o["msg"].toString();
            if (message.isEmpty()) message = o["error"].toObject()["message"].toString();
            if (message.isEmpty()) message = o["message"].toString();
            if (!message.isEmpty()) messages << message;
        }
    }
    return messages.join('\n');
}
struct Command { QStringList args; QString workingDirectory; QProcessEnvironment env = QProcessEnvironment::systemEnvironment(); bool lines = false; };
Command command(const ToolRequest &r) {
    Command c;
    // Ambient tool options must not silently change the explicit UI contract.
    for (const auto &key : c.env.keys())
        if (key.startsWith("RESTIC_") || key.startsWith("RCLONE_") || key == "RIPGREP_CONFIG_PATH") c.env.remove(key);
    switch (r.action) {
    case ToolAction::ContentSearch:
        c.lines = true;
        c.args = {"--no-config", "--json", "--threads", "2", "--max-filesize", "64M", "--color", "never"};
        c.args << (r.caseSensitive ? "--case-sensitive" : "--ignore-case");
        if (!r.regex) c.args << "--fixed-strings";
        if (r.hidden) c.args << "--hidden";
        if (r.ignored) c.args << "--no-ignore";
        c.args << "--" << r.pattern << r.root;
        break;
    case ToolAction::InitBackup: case ToolAction::Snapshots: case ToolAction::Backup: case ToolAction::Restore:
        c.env.insert("RESTIC_PASSWORD", r.password);
        c.env.insert("GOMAXPROCS", "2");
        c.args = {"--repo", r.repository, "--json", "--no-cache"};
        if (r.action == ToolAction::InitBackup) { c.args << "init"; c.lines = true; }
        if (r.action == ToolAction::Snapshots) c.args << "snapshots";
        if (r.action == ToolAction::Backup) {
            // Relative roots avoid capturing metadata of unrelated C:/Users ancestors.
            c.workingDirectory = QFileInfo(r.sources.first()).absolutePath();
            c.args << "backup" << "--";
            for (const auto &s : r.sources) c.args << QFileInfo(s).fileName();
            c.lines = true;
        }
        if (r.action == ToolAction::Restore) { c.args << "restore" << r.snapshot << "--target" << r.destination << "--verify" << "--overwrite" << "never"; c.lines = true; }
        break;
    default:
        c.args = {"--config", r.config, "--ask-password=false"};
        if (r.action == ToolAction::Remotes) c.args << "listremotes";
        if (r.action == ToolAction::RemoteList) c.args << "lsjson" << r.source << "--no-mimetype";
        if (r.action == ToolAction::TransferPreview || r.action == ToolAction::TransferCopy) {
            c.args << "copy" << r.source << r.destination << "--immutable" << "--create-empty-src-dirs"
                   << "--transfers" << "2" << "--checkers" << "2" << "--retries" << "1"
                   << "--use-json-log" << "--stats" << "1s" << "--stats-log-level" << "NOTICE";
            if (r.action == ToolAction::TransferPreview) c.args << "--dry-run" << "--combined" << "-";
        }
        break;
    }
    return c;
}
}

QString discoverTool(const QString &name, const QString &configured) {
    if (!configured.isEmpty()) return QFileInfo(configured).isFile() && localPath(configured) ? configured : QString();
    // Portable deployment beside the application (or beside its build directory).
    const QDir app(QCoreApplication::applicationDirPath());
    const QStringList candidates{app.filePath("tools/" + name + ".exe"), app.filePath("../tools/" + name + ".exe"),
        QDir(qEnvironmentVariable("ProgramFiles")).filePath(name + "/" + name + ".exe")};
    for (const auto &p : candidates) if (QFileInfo(p).isFile()) return QFileInfo(p).absoluteFilePath();
    const auto p = QStandardPaths::findExecutable(name + ".exe");
    return !p.isEmpty() && localPath(p) ? p : QString();
}

QString validateToolRequest(const ToolRequest &r) {
    if (!localPath(r.executable) || !QFileInfo(r.executable).isFile()) return "Selecione o executável instalado da ferramenta.";
    if (r.action == ToolAction::ContentSearch) {
        if (!localPath(r.root) || !QFileInfo(r.root).isDir()) return "Escolha uma pasta existente para pesquisar.";
        if (r.pattern.isEmpty() || r.pattern.size() > 4096 || r.pattern.contains(QChar(0))) return "Informe texto ou expressão de até 4.096 caracteres.";
    } else if (r.action <= ToolAction::Restore) {
        if (!localPath(r.repository) || r.password.isEmpty() || r.password.contains(QChar(0))) return "Escolha um repositório local e informe a senha. A senha não será salva.";
        if (r.action == ToolAction::InitBackup && QFileInfo::exists(r.repository)) return "Para inicializar, escolha uma pasta de repositório que ainda não exista.";
        if (r.action == ToolAction::Backup) {
            if (r.sources.isEmpty() || r.sources.size() > 1000) return "Selecione entre 1 e 1.000 arquivos ou pastas para o backup.";
            for (const auto &s : r.sources) {
                if (!localPath(s) || !QFileInfo::exists(s) || QFileInfo(s).isSymLink()) return "A origem deve existir e não pode ser um link: " + s;
                if (inside(r.repository, s) || inside(s, r.repository)) return "Separe o repositório das origens do backup.";
                if (QFileInfo(s).fileName().isEmpty() || QFileInfo(s).absolutePath().compare(QFileInfo(r.sources.first()).absolutePath(), Qt::CaseInsensitive) != 0) return "Selecione arquivos/pastas de uma mesma pasta para cada versão de backup.";
            }
        }
        if (r.action == ToolAction::Restore && (!snapshotId(r.snapshot) || !localPath(r.destination)
            || QFileInfo::exists(r.destination) || !QFileInfo(QFileInfo(r.destination).absolutePath()).isDir()
            || inside(r.destination, r.repository) || inside(r.repository, r.destination)))
            return "Escolha uma versão válida e uma pasta nova, fora do repositório, em um diretório existente.";
    } else {
        if (!localPath(r.config) || !QFileInfo(r.config).isFile()) return "Selecione um arquivo de configuração rclone existente (rclone.conf).";
        if (r.action != ToolAction::Remotes && !endpoint(r.source)) return "Use uma pasta local absoluta ou conexão configurada no formato nome:pasta.";
        if (r.action == ToolAction::TransferPreview || r.action == ToolAction::TransferCopy) {
            const auto sensitivity = localPath(r.source) && localPath(r.destination) ? Qt::CaseInsensitive : Qt::CaseSensitive;
            if (!endpoint(r.destination) || r.source.compare(r.destination, sensitivity) == 0) return "Informe um destino diferente e válido.";
            if (localPath(r.source) && !QFileInfo(r.source).isDir()) return "Esta transferência copia o conteúdo de uma pasta; selecione uma pasta de origem.";
            if (localPath(r.source) && localPath(r.destination) && (inside(r.source, r.destination) || inside(r.destination, r.source))) return "Origem e destino não podem conter um ao outro.";
        }
    }
    return {};
}

ToolResult performTool(const ToolRequest &r, const std::atomic_bool &cancel, const std::function<void(QString)> &progress) {
    ToolResult result;
    const auto invalid = validateToolRequest(r); if (!invalid.isEmpty()) { result.message = invalid; return result; }
    if (cancel.load()) { result.state = ToolState::Cancelled; result.message = "Cancelado antes de iniciar."; return result; }
    // Restore exclusively reserves a fresh directory. Cancellation preserves partial output.
    if (r.action == ToolAction::Restore && !QDir().mkdir(r.destination)) { result.message = "Não foi possível reservar a pasta nova de restauração."; return result; }
    if (r.action == ToolAction::Restore) result.output = r.destination;
    auto c = command(r);
    QByteArray output, pending, stderrPending;
    bool limited = false, badProtocol = false, summary = false, skippedRestore = false; QString snapshot;
    QElapsedTimer throttle; throttle.start();
    auto notify = [&](const QString &s) { if (progress && throttle.elapsed() >= 150) { progress(s); throttle.restart(); } };
    auto line = [&](const QByteArray &bytes) {
        if (bytes.trimmed().isEmpty()) return;
        QJsonParseError error; const auto doc = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) { badProtocol = true; return; }
        const auto o = doc.object();
        if (r.action == ToolAction::ContentSearch) {
            if (!o["type"].isString() || !o["data"].isObject()) { badProtocol = true; return; }
            const auto type = o["type"].toString(); const auto d = o["data"].toObject();
            if (type == "summary") summary = true;
            if (type != "match") return;
            bool ok = true; const auto path = QDir::fromNativeSeparators(jsonText(d["path"], &ok)); const auto text = jsonText(d["lines"], &ok);
            const auto number = d["line_number"].toInteger(-1);
            if (!ok || !localPath(path) || !inside(path, r.root) || number < 1) { badProtocol = true; return; }
            if (result.rows.size() == maxRows) { limited = true; return; }
            result.rows.append({path, {}, text.left(1024).trimmed(), number});
            notify(QString("%1 linhas encontradas…").arg(result.rows.size()));
        } else {
            const auto type = o["message_type"].toString();
            if (type == "summary" || type == "initialized") {
                summary = true; snapshot = o["snapshot_id"].toString();
                if (r.action == ToolAction::Backup && !snapshotId(snapshot)) badProtocol = true;
                if (r.action == ToolAction::Restore && o["files_skipped"].toInteger() > 0) skippedRestore = true;
            }
            if (type == "status") notify(QString("Processados: %1 arquivos, %2 bytes")
                .arg(o[r.action == ToolAction::Restore ? "files_restored" : "files_done"].toInteger())
                .arg(o[r.action == ToolAction::Restore ? "bytes_restored" : "bytes_done"].toInteger()));
        }
    };
    ExternalCommand invocation{r.executable, c.args, c.workingDirectory, c.env};
    const auto external = runExternal(invocation, cancel, [&](const QByteArray &chunk) {
        if (c.lines) {
            pending += chunk; qsizetype end;
            while ((end = pending.indexOf('\n')) >= 0 && !limited && !badProtocol) { line(pending.first(end)); pending.remove(0, end + 1); }
            if (pending.size() > maxLine) limited = true;
        } else if (output.size() + chunk.size() <= maxOutput) output += chunk; else limited = true;
        return !limited && !badProtocol;
    }, [&](const QByteArray &chunk) {
        if (r.action == ToolAction::TransferCopy || r.action == ToolAction::TransferPreview) {
            stderrPending += chunk; qsizetype end;
            while ((end = stderrPending.indexOf('\n')) >= 0) {
                const auto o = QJsonDocument::fromJson(stderrPending.first(end)).object(); stderrPending.remove(0, end + 1);
                const auto stats = o["stats"].toObject();
                if (!stats.isEmpty()) notify((r.action == ToolAction::TransferPreview ? QString("Simulados: %1 bytes; arquivos: %2; erros: %3") : QString("Transferidos: %1 bytes; arquivos: %2; erros: %3"))
                    .arg(stats["bytes"].toInteger()).arg(stats["transfers"].toInteger()).arg(stats["errors"].toInteger()));
            }
            if (stderrPending.size() > maxLine) stderrPending.clear();
        }
        return true;
    });
    if (c.lines && !pending.isEmpty() && !limited && !badProtocol) line(pending);
    result.elapsedMs = external.elapsedMs; result.diagnostics = diagnosticText(external.errors, r.action == ToolAction::TransferPreview);
    if (external.state == ExternalState::Incomplete) { result.state = ToolState::Incomplete; result.message = external.message; return result; }
    if (cancel.load()) { result.state = ToolState::Cancelled; result.message = "Cancelado. Os itens já gravados foram preservados; a tarefa pode estar parcial."; return result; }
    if (limited || badProtocol) { result.state = ToolState::Incomplete; result.message = limited ? "Resultado parcial: limite de 10.000 linhas, 1 MiB por linha ou 8 MiB de catálogo atingido." : "Resposta inesperada da ferramenta; cobertura incompleta."; return result; }
    const auto code = external.exitCode;
    if (external.state != ExternalState::Exited || (code != 0 && !(r.action == ToolAction::ContentSearch && code == 1))) {
        result.state = (r.action == ToolAction::Backup || r.action == ToolAction::Restore || r.action == ToolAction::TransferCopy) ? ToolState::Incomplete : ToolState::Failed;
        result.message = QString("A ferramenta retornou erro (%1). Confira o diagnóstico; resultados recebidos podem ser parciais.").arg(code); return result;
    }
    if (c.lines && !summary) { result.state = ToolState::Incomplete; result.message = "A ferramenta terminou sem o resumo esperado; conclusão não confirmada."; return result; }
    if (skippedRestore) { result.state = ToolState::Incomplete; result.message = "Restauração parcial: o restic informou arquivos ignorados; confira o destino reservado."; return result; }
    auto invalidResponse = [&] { result.state = ToolState::Incomplete; result.message = "Catálogo inválido ou maior que 10.000 itens; não presumir ausência."; };
    if (r.action == ToolAction::Snapshots || r.action == ToolAction::RemoteList) {
        // restic emits null for an empty repository on some versions.
        if (r.action == ToolAction::Snapshots && output.trimmed() == "null") output = "[]";
        QJsonParseError e; const auto arrayDoc = QJsonDocument::fromJson(output, &e);
        if (e.error != QJsonParseError::NoError || !arrayDoc.isArray()) { invalidResponse(); return result; }
        for (const auto &v : arrayDoc.array()) {
            if (!v.isObject() || result.rows.size() >= maxRows) { invalidResponse(); return result; }
            const auto o = v.toObject();
            if (r.action == ToolAction::Snapshots) {
                QStringList paths;
                if (!snapshotId(o["id"].toString()) || !o["time"].isString() || !o["paths"].isArray()) { invalidResponse(); return result; }
                for (const auto &p : o["paths"].toArray()) { if (!p.isString()) { invalidResponse(); return result; } paths << p.toString(); }
                result.rows.append({o["id"].toString(), o["time"].toString(), paths.join("; ")});
            } else {
                if (!o["Path"].isString() || !o["IsDir"].isBool() || !o["Size"].isDouble()) { invalidResponse(); return result; }
                result.rows.append({o["Path"].toString(), o["ModTime"].toString(), {}, o["Size"].toInteger(), o["IsDir"].toBool()});
            }
        }
    }
    if (r.action == ToolAction::Remotes) {
        for (const auto &s : QString::fromUtf8(output).split('\n', Qt::SkipEmptyParts)) {
            if (result.rows.size() >= maxRows || !endpoint(s.trimmed()) || !s.trimmed().endsWith(':')) { invalidResponse(); return result; }
            result.rows.append({s.trimmed()});
        }
    }
    if (r.action == ToolAction::TransferPreview) result.diagnostics = QString::fromUtf8(output) + "\n" + result.diagnostics;
    result.state = ToolState::Completed;
    if (r.action == ToolAction::ContentSearch) result.message = QString("Busca concluída no escopo permitido: %1 linhas. Binários, arquivos >64 MiB e regras de exclusão podem limitar a cobertura; PDF/Office sem extração.").arg(result.rows.size());
    else if (r.action == ToolAction::TransferPreview) result.message = "Simulação concluída. + novo, * diferente, = igual. Nenhum arquivo copiado. A origem/destino serão relidos ao executar; diferenças existentes impedem substituição.";
    else if (r.action == ToolAction::TransferCopy) result.message = "Cópia concluída pelo rclone. Arquivos existentes diferentes não são substituídos; exclusões não são propagadas.";
    else if (r.action == ToolAction::Backup) result.message = "Backup concluído. Versão: " + snapshot;
    else if (r.action == ToolAction::Restore) result.message = "Restauração concluída e verificada pelo restic em: " + r.destination;
    else result.message = QString("Concluído. %1 itens no catálogo.").arg(result.rows.size());
    return result;
}

ToolJob::ToolJob(ToolRequest request, QObject *parent) : QThread(parent), m_request(std::move(request)) {}
ToolJob::~ToolJob() { cancel(); wait(); }
void ToolJob::run() {
    m_result = performTool(m_request, m_cancel, [this](QString s) { emit progress(std::move(s)); });
    m_request.password.fill(QChar(0)); m_request.password.clear();
}
