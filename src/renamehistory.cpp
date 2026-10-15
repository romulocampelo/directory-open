#include "renamehistory.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <limits>
#include <algorithm>
#include <climits>

namespace {
constexpr qint64 maxRecordBytes = 8 * 1024 * 1024;
constexpr int maxRecords = 1000;
struct Record {
    QString id, phase;
    QDateTime created;
    BatchRenamePlan plan;
    QVector<std::optional<RenameIdentity>> after;
    QByteArray baseline;
};
QString key(const QString &path) { return QDir::cleanPath(path).toCaseFolded(); }
bool validId(const QString &id) {
    static const QRegularExpression pattern("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
    return pattern.match(id).hasMatch();
}
QStringList recordFiles(const QString &root) {
    QStringList files;
    QDirIterator iterator(root,{"*.json"},QDir::Files | QDir::Hidden,QDirIterator::NoIteratorFlags);
    while (iterator.hasNext() && files.size() <= maxRecords) { iterator.next(); files.append(iterator.fileName()); }
    return files; // At most limit + 1, enough to report capacity without enumerating further.
}
QString recordPath(const QString &root, const QString &id) { return QDir(root).filePath(id + ".json"); }
QString checkRoot(const QString &root, bool create) {
    if (root.isEmpty() || root.contains(QChar(0)) || !QDir::isAbsolutePath(root))
        return "O histórico precisa de uma pasta com caminho completo.";
    if (!QFileInfo::exists(root)) {
        if (!create) return {};
        if (!QDir().mkpath(root)) return "Não foi possível criar a pasta do histórico.";
    }
    const QFileInfo info(root);
    if (!info.isDir() || info.isSymLink() || info.isJunction()) return "A pasta do histórico deve ser uma pasta comum, sem links ou junções.";
    return {};
}
QJsonObject encodeIdentity(const RenameIdentity &id) {
    // Decimal strings preserve the complete 64-bit Win32 values in JSON.
    return {{"volume",QString::number(id.volume)},{"high",QString::number(id.indexHigh)},
        {"low",QString::number(id.indexLow)},{"attributes",QString::number(id.attributes)},
        {"bytes",QString::number(id.bytes)},{"modified",QString::number(id.modified)},{"created",QString::number(id.created)}};
}
bool decodeIdentity(const QJsonValue &value, RenameIdentity &id) {
    if (!value.isObject()) return false;
    const auto obj = value.toObject();
    auto number = [&](const char *name, quint64 maximum, quint64 &target) {
        const auto v = obj.value(QLatin1String(name));
        if (!v.isString()) return false;
        const auto text = v.toString();
        if (text.isEmpty() || text.size() > 20) return false;
        for (auto c : text) if (c < '0' || c > '9') return false;
        bool ok = false; const auto parsed = text.toULongLong(&ok);
        if (!ok || parsed > maximum) return false;
        target = parsed; return true;
    };
    quint64 volume,high,low,attributes,bytes,modified,created;
    if (!number("volume",UINT_MAX,volume) || !number("high",UINT_MAX,high) || !number("low",UINT_MAX,low)
        || !number("attributes",UINT_MAX,attributes) || !number("bytes",ULLONG_MAX,bytes)
        || !number("modified",ULLONG_MAX,modified) || !number("created",ULLONG_MAX,created) || (!high && !low)) return false;
    id = {quint32(volume),quint32(high),quint32(low),quint32(attributes),bytes,modified,created}; return true;
}
QByteArray encode(const Record &record) {
    QJsonArray items;
    for (int i = 0; i < record.plan.items.size(); ++i) {
        const auto &item = record.plan.items[i];
        QJsonObject entry{{"source",item.source},{"target",item.target},{"before",encodeIdentity(item.identity)}};
        if (record.after.value(i)) entry.insert("after",encodeIdentity(*record.after[i]));
        items.append(entry);
    }
    return QJsonDocument(QJsonObject{{"version",1},{"id",record.id},{"created",record.created.toString(Qt::ISODateWithMs)},
        {"phase",record.phase},{"items",items}}).toJson(QJsonDocument::Compact);
}
QString atomicWrite(const QString &path, const QByteArray &bytes) {
    if (bytes.size() > maxRecordBytes) return "O registro excede o limite de 8 MiB.";
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return "Não foi possível salvar o histórico: " + file.errorString();
    return {};
}
QString save(const QString &root, const Record &record, bool backup) {
    const auto path = recordPath(root,record.id);
    if (QFileInfo(path).isSymLink() || QFileInfo(path + ".bak").isSymLink()) return "O registro do histórico foi substituído por um link.";
    if (backup) {
        QFile old(path);
        if (!old.open(QIODevice::ReadOnly) || old.size() > maxRecordBytes) return "Não foi possível preservar a cópia anterior do histórico.";
        const auto bytes = old.read(maxRecordBytes + 1);
        if (old.error() != QFileDevice::NoError) return "Falha ao ler a cópia anterior do histórico.";
        const auto error = atomicWrite(path + ".bak",bytes); if (!error.isEmpty()) return error;
    }
    return atomicWrite(path,encode(record));
}
QString load(const QString &root, const QString &id, Record &record) {
    if (!validId(id)) return "Identificador de histórico inválido.";
    QFile file(recordPath(root,id));
    if (QFileInfo(file).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() > maxRecordBytes)
        return "O registro não pôde ser lido com segurança (limite de 8 MiB).";
    const auto bytes = file.read(maxRecordBytes + 1);
    if (bytes.size() > maxRecordBytes || file.error() != QFileDevice::NoError) return "Falha na leitura do registro.";
    QJsonParseError parse; const auto document = QJsonDocument::fromJson(bytes,&parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) return "Registro de histórico inválido.";
    const auto obj = document.object();
    if (obj.value("version") != QJsonValue(1) || obj.value("id").toString() != id)
        return "Versão ou identidade do registro não reconhecida.";
    record.id = id; record.phase = obj.value("phase").toString();
    record.created = QDateTime::fromString(obj.value("created").toString(),Qt::ISODateWithMs);
    if (!record.created.isValid() || !QStringList{"prepared","finished","undoing","undo-finished"}.contains(record.phase)
        || !obj.value("items").isArray()) return "Cabeçalho de histórico inválido.";
    const auto items = obj.value("items").toArray();
    if (items.isEmpty() || items.size() > 1000) return "Quantidade de itens inválida no histórico.";
    QString parent; QSet<QString> sources,targets; qsizetype characters = 0;
    for (const auto &value : items) {
        const auto entry = value.toObject(); BatchRenameItem item;
        item.source = entry.value("source").toString(); item.target = entry.value("target").toString();
        characters += item.source.size() + item.target.size();
        if (characters > 2 * 1024 * 1024) return "Os caminhos excedem o orçamento do histórico.";
        for (const auto &path : {item.source,item.target})
            if (!QDir::isAbsolutePath(path) || path.contains(QChar(0)) || path != QDir::cleanPath(QDir::fromNativeSeparators(path)))
                return "Caminho inválido no histórico.";
        const auto folder = QFileInfo(item.source).absolutePath();
        item.name = QFileInfo(item.target).fileName();
        if (parent.isEmpty()) parent = folder;
        if (key(parent) != key(folder) || key(folder) != key(QFileInfo(item.target).absolutePath())
            || !validateItemName(QFileInfo(item.source).fileName()).isEmpty() || !validateItemName(item.name).isEmpty()
            || sources.contains(key(item.source)) || targets.contains(key(item.target)) || !decodeIdentity(entry.value("before"),item.identity))
            return "Mapeamento ou identidade inválida no histórico.";
        sources.insert(key(item.source)); targets.insert(key(item.target));
        item.unchanged = item.source == item.target;
        std::optional<RenameIdentity> after;
        if (entry.contains("after")) { RenameIdentity identity; if (!decodeIdentity(entry.value("after"),identity) || identity.volume != item.identity.volume || identity.indexHigh != item.identity.indexHigh || identity.indexLow != item.identity.indexLow) return "Identidade final inválida."; after = identity; }
        record.plan.items.append(item); record.after.append(after);
    }
    record.baseline = QCryptographicHash::hash(bytes,QCryptographicHash::Sha256); return {};
}
UndoRenamePreview preview(const Record &record, const std::atomic_bool &cancel) {
    UndoRenamePreview result; result.id = record.id; result.baseline = record.baseline;
    for (int i = 0; i < record.plan.items.size(); ++i) {
        if (cancel) { result.error = "Prévia cancelada."; break; }
        const auto &item = record.plan.items[i]; const auto expected = record.after[i].value_or(item.identity);
        UndoRenameItem row; row.index = i; row.original = item.source; row.current = item.target;
        row.reverse = {item.target,item.source,QFileInfo(item.source).fileName(),{},expected,false};
        if (item.unchanged) { row.state = UndoRenameState::Unchanged; row.detail = "Sem alteração no lote."; }
        else {
            const auto original = inspectRenameItem(item.source), current = inspectRenameItem(item.target);
            const bool sameName = key(item.source) == key(item.target);
            const bool atOriginal = original.state == RenameProbeState::Present && original.identity == expected
                && original.actualName == QFileInfo(item.source).fileName();
            const bool atCurrent = current.state == RenameProbeState::Present && current.identity == expected
                && current.actualName == QFileInfo(item.target).fileName();
            if (atOriginal && (sameName || current.state == RenameProbeState::Missing)) {
                row.state = UndoRenameState::AlreadyOriginal; row.detail = "Já está com o nome original.";
            } else if (atCurrent && (sameName || original.state == RenameProbeState::Missing)) {
                row.state = UndoRenameState::Ready; row.detail = "Pronto para desfazer.";
            } else {
                if (original.state == RenameProbeState::Failed) row.detail = "Nome original não confirmado: " + original.error;
                else if (current.state == RenameProbeState::Failed) row.detail = "Item não confirmado: " + current.error;
                else if (atCurrent && original.state == RenameProbeState::Present) row.detail = "Bloqueado: nome original ocupado.";
                else if (current.state == RenameProbeState::Missing) row.detail = "Bloqueado: item renomeado ausente.";
                else if (current.identity != expected) row.detail = "Bloqueado: identidade ou metadados alterados.";
                else row.detail = "Bloqueado: nomes não confirmados.";
            }
        }
        result.items.append(row);
    }
    return result;
}
OperationResult failed(const BatchRenamePlan &plan, const QString &error) {
    OperationResult result; result.error = error;
    for (const auto &item : plan.items) result.items.append({item.source,item.target,OperationItemState::NotStarted,{},{}});
    return result;
}
bool containsHistory(const BatchRenamePlan &plan, const QString &root) {
    const auto canonicalRoot = QFileInfo(root).canonicalFilePath();
    QHash<QString,QString> resolvedParents;
    const auto includes = [](const QString &history, const QString &path) {
        return !history.isEmpty() && !path.isEmpty() && (key(history) == key(path) || key(history).startsWith(key(path) + '/'));
    };
    for (const auto &item : plan.items) {
        if (includes(root,item.source) || includes(root,item.target)) return true;
        if (canonicalRoot.isEmpty()) continue;
        const auto parent = QFileInfo(item.source).absolutePath();
        auto found = resolvedParents.constFind(parent);
        if (found == resolvedParents.cend()) found = resolvedParents.insert(parent,QFileInfo(parent).canonicalFilePath());
        if (found.value().isEmpty()) continue;
        // All mutation leaves are revalidated as non-reparse items by the native
        // motor. Resolve their shared parent once, rather than probing every leaf.
        if (includes(canonicalRoot,QDir(found.value()).filePath(QFileInfo(item.source).fileName()))
            || includes(canonicalRoot,QDir(found.value()).filePath(item.name))) return true;
    }
    return false;
}
}
OperationResult performRecordedBatchRename(const BatchRenamePlan &plan, const QString &root,
    const std::atomic_bool &cancel, const OperationProgress &progress) {
    if (root.isEmpty()) return performBatchRename(plan,cancel,progress);
    if (!plan.valid() || !plan.changes()) return failed(plan,"O lote precisa de uma prévia válida com alterações.");
    qsizetype characters = 0;
    for (const auto &item : plan.items) { characters += item.source.size() + item.target.size(); if (characters > 2 * 1024 * 1024) return failed(plan,"O lote excede o orçamento de caminhos do histórico."); }
    if (containsHistory(plan,root)) return failed(plan,"O lote não pode renomear a própria pasta do histórico ou seus ancestrais.");
    const auto error = checkRoot(root,true); if (!error.isEmpty()) return failed(plan,error);
    if (containsHistory(plan,root)) return failed(plan,"O lote afetaria a pasta real do histórico.");
    QLockFile lock(QDir(root).filePath(".lock"));
    if (!lock.tryLock(0)) return failed(plan,"O histórico está em uso por outra janela. Tente novamente após o encerramento do trabalho.");
    if (recordFiles(root).size() >= maxRecords)
        return failed(plan,"O histórico atingiu 1.000 lotes. Arquive os registros antigos antes de continuar.");
    Record record; record.id = QUuid::createUuid().toString(QUuid::WithoutBraces); record.created = QDateTime::currentDateTimeUtc();
    record.phase = "prepared"; record.plan = plan; record.after.resize(plan.items.size());
    if (cancel) { auto result = failed(plan,"Cancelado antes de registrar o lote."); result.state = OperationState::Cancelled; return result; }
    const auto saved = save(root,record,false); if (!saved.isEmpty()) return failed(plan,saved);
    auto result = performBatchRename(plan,cancel,progress);
    for (int i = 0; i < result.items.size(); ++i) record.after[i] = result.items[i].renamedIdentity;
    record.phase = "finished"; const auto finalSave = save(root,record,true);
    if (!finalSave.isEmpty()) { result.state = OperationState::Incomplete; result.error += "\n" + finalSave + "\nMapa de recuperação: " + recordPath(root,record.id); }
    return result;
}
RenameHistoryReport runRenameHistory(const QString &root, const RenameHistoryRequest &request,
    const std::atomic_bool &cancel, const OperationProgress &progress) {
    RenameHistoryReport report; report.error = checkRoot(root,false); if (!report.error.isEmpty()) return report;
    if (request.mode == RenameHistoryMode::List) {
        if (!QFileInfo::exists(root)) return report;
        const auto files = recordFiles(root);
        if (files.size() > maxRecords) { report.error = "Há mais de 1.000 registros. Arquive parte do histórico antes de consultá-lo."; return report; }
        for (const auto &file : files) {
            if (cancel) { report.error = "Consulta cancelada."; break; }
            const auto id = file.chopped(5); Record record;
            const auto error = load(root,id,record);
            report.records.append({id,record.plan.items.isEmpty() ? QString{} : QFileInfo(record.plan.items.first().source).absolutePath(),
                record.phase,error,record.created,int(record.plan.items.size())});
        }
        std::sort(report.records.begin(),report.records.end(),[](const auto &a, const auto &b) { return a.created > b.created; });
        return report;
    }
    Record record; report.error = load(root,request.id,record);
    if (!report.error.isEmpty()) { report.preview.error = report.error; return report; }
    if (request.mode == RenameHistoryMode::Preview) { report.preview = preview(record,cancel); report.error = report.preview.error; return report; }
    QLockFile lock(QDir(root).filePath(".lock"));
    if (!lock.tryLock(0)) { report.error = "O histórico está em uso por outra janela."; return report; }
    // Reload under the lock: a preview never grants authority over a later journal.
    record = {}; report.error = load(root,request.id,record); if (!report.error.isEmpty()) return report;
    if (request.baseline != record.baseline || request.selected.isEmpty() || request.selected.size() > 1000) {
        report.error = "O histórico mudou ou não há itens selecionados. Gere uma nova prévia."; return report;
    }
    report.preview = preview(record,cancel);
    if (!report.preview.error.isEmpty()) { report.error = report.preview.error; return report; }
    BatchRenamePlan reverse; QSet<int> seen;
    for (int index : request.selected) {
        if (index < 0 || index >= report.preview.items.size() || seen.contains(index)
            || report.preview.items[index].state != UndoRenameState::Ready) {
            report.error = "Um item mudou ou não pode ser desfeito. Gere uma nova prévia."; return report;
        }
        seen.insert(index); reverse.items.append(report.preview.items[index].reverse);
    }
    if (containsHistory(reverse,root)) { report.error = "A reversão afetaria a própria pasta do histórico."; return report; }
    record.phase = "undoing"; report.error = save(root,record,true); if (!report.error.isEmpty()) return report;
    report.operation = performBatchRename(reverse,cancel,progress);
    // The map remains intact after a partial undo; the next preview reconciles each identity and name.
    record.phase = "undo-finished"; const auto saved = save(root,record,true);
    if (!saved.isEmpty()) { report.operation.state = OperationState::Incomplete; report.operation.error += "\n" + saved; }
    return report;
}
RenameHistoryJob::RenameHistoryJob(QString root, RenameHistoryRequest request, QObject *parent)
    : QThread(parent),m_root(std::move(root)),m_request(std::move(request)) {}
RenameHistoryJob::~RenameHistoryJob() { cancel(); wait(); }
void RenameHistoryJob::run() {
    try { m_report = runRenameHistory(m_root,m_request,m_cancel); }
    catch (const std::exception &) { m_report.error = "Falha ao consultar o histórico; confira a prévia novamente."; }
}
