#include "teracopy.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryFile>

QString teraCopyExecutable() {
    // Use installation locations, never PATH, the current folder or a shell command.
    for (const auto &variable : {"ProgramW6432", "ProgramFiles", "ProgramFiles(x86)"}) {
        const auto root = qEnvironmentVariable(variable);
        if (root.isEmpty()) continue;
        const QFileInfo info(QDir(root).filePath("TeraCopy/TeraCopy.exe"));
        if (info.isFile() && !info.isSymLink() && !info.isJunction()) return info.absoluteFilePath();
    }
    return {};
}
QString teraCopyRecordsPath() {
    const auto root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return root.isEmpty() ? QString{} : QDir(root).filePath("transfers/teracopy");
}
OperationResult dispatchTeraCopy(const OperationRequest &request, const std::atomic_bool &cancel,
                                const OperationProgress &progress, const QString &recordsPath) {
    OperationResult result;
    for (const auto &source : request.sources) result.items.append({source, {}, OperationItemState::NotStarted, {}, {}});
    if (cancel.load()) { result.state = OperationState::Cancelled; return result; }
    if (request.engine != TransferEngine::TeraCopy) { result.error = "Selecione o motor TeraCopy."; return result; }
    result.error = validateOperationRequest(request);
    if (!result.error.isEmpty()) return result;
    result.error = validateOperationTree(request, cancel, progress);
    if (cancel.load()) { result.state = OperationState::Cancelled; return result; }
    if (!result.error.isEmpty()) return result;
    const auto executable = teraCopyExecutable();
    if (executable.isEmpty()) { result.error = "TeraCopy não encontrado em Arquivos de Programas. Escolha Windows para copiar."; return result; }

    // UTF-16LE BOM is accepted by the installed TeraCopy and preserves Unicode.
    QByteArray bytes("\xff\xfe", 2);
    for (const auto &source : request.sources) {
        const auto path = QDir::toNativeSeparators(source) + "\r\n";
        if (bytes.size() + path.size() * 2 > 8 * 1024 * 1024) { result.error = "A lista de caminhos excede 8 MiB. Divida a seleção."; return result; }
        for (const auto unit : path) { bytes.append(char(unit.unicode() & 0xff)); bytes.append(char(unit.unicode() >> 8)); }
    }
    const auto root = recordsPath.isEmpty() ? teraCopyRecordsPath() : recordsPath;
    if (root.isEmpty() || !QDir::isAbsolutePath(root) || !QDir().mkpath(root)
        || QFileInfo(root).isSymLink() || QFileInfo(root).isJunction()) {
        result.error = "Não foi possível preparar a pasta de registros da cópia."; return result;
    }
    QTemporaryFile list(QDir(root).filePath("copy-XXXXXX.txt"));
    if (!list.open() || list.write(bytes) != bytes.size() || !list.flush()) { result.error = "Não foi possível gravar a seleção para o TeraCopy: " + list.errorString(); return result; }
    const auto listPath = list.fileName(); list.close();
    const auto recordPath = listPath + ".json";
    const QJsonObject record{{"version", 1}, {"state", "dispatch-requested"},
        {"createdUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"executable", executable}, {"operation", "Copy"}, {"sources", QJsonArray::fromStringList(request.sources)},
        {"destination", request.destination}, {"conflict", "rename-copied"}, {"list", listPath}};
    QSaveFile receipt(recordPath); receipt.setDirectWriteFallback(false);
    const auto json = QJsonDocument(record).toJson();
    if (!receipt.open(QIODevice::WriteOnly) || receipt.write(json) != json.size() || !receipt.commit()) {
        result.error = "Não foi possível registrar o encaminhamento: " + receipt.errorString(); return result;
    }
    if (cancel.load()) { QFile::remove(recordPath); result.state = OperationState::Cancelled; return result; }
    // Revalidate immediately before the external boundary; never retry with another engine.
    result.error = validateOperationRequest(request);
    if (!result.error.isEmpty()) { QFile::remove(recordPath); return result; }
    // A forwarded job can read the list after the launcher exits or DirOpen closes.
    // Keep these owned records until the user removes them after checking TeraCopy.
    list.setAutoRemove(false);
    QProcess process;
    process.setProgram(executable);
    process.setArguments({"Copy", "*" + QDir::toNativeSeparators(listPath),
                          QDir::toNativeSeparators(request.destination), "/RenameCopied", "/NoClose"});
    process.setWorkingDirectory(QFileInfo(executable).absolutePath());
    result.externalRecord = recordPath;
    if (!process.startDetached()) {
        result.error = "Não foi possível encaminhar a cópia ao TeraCopy: " + process.errorString()
            + ". Confira a janela dele antes de tentar novamente. A seleção foi preservada no registro.";
        return result;
    }
    result.state = OperationState::Delegated;
    for (auto &item : result.items) item.state = OperationItemState::Delegated;
    return result;
}
