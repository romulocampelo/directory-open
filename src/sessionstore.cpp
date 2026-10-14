#include "sessionstore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

namespace {
constexpr qint64 maxRecordBytes = 1024 * 1024;
constexpr int maxTabs = 256;
struct Record { bool exists = false; QByteArray bytes; QString error; };
Record readRecord(const QString &path) {
    Record record; const QFileInfo info(path); record.exists = info.exists() || info.isSymLink();
    if (!record.exists) return record;
    if (!info.isFile() || info.isSymLink()) { record.error = "O registro da sessão não é um arquivo normal."; return record; }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { record.error = file.errorString(); return record; }
    record.bytes = file.read(maxRecordBytes + 1);
    if (record.bytes.size() > maxRecordBytes) record.error = "O registro da sessão excede o tamanho permitido.";
    else if (file.error() != QFileDevice::NoError) record.error = file.errorString();
    return record;
}
QByteArray digest(const QByteArray &bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); }
bool integer(const QJsonValue &v, int minimum, int maximum) { return v.isDouble() && v.toDouble() == v.toInt(-1) && v.toInt(-1) >= minimum && v.toInt(-1) <= maximum; }
bool decodePanel(const QJsonValue &value, PanelSession &panel) {
    if (!value.isObject()) return false; const auto object = value.toObject();
    if (!object["tabs"].isArray()) return false; const auto tabs = object["tabs"].toArray();
    if (tabs.isEmpty() || tabs.size() > maxTabs || !integer(object["currentTab"], 0, int(tabs.size()) - 1)
        || !integer(object["sortColumn"], 0, 3) || !object["descending"].isBool()) return false;
    for (const auto &entry : tabs) {
        if (!entry.isObject()) return false; const auto tab = entry.toObject();
        if (!tab["path"].isString() || !QDir::isAbsolutePath(tab["path"].toString()) || tab["path"].toString().contains(QChar(0)) || !tab["filter"].isString()) return false;
        panel.tabs.append({tab["path"].toString(), tab["filter"].toString()});
    }
    panel.currentTab = object["currentTab"].toInt(); panel.sortColumn = object["sortColumn"].toInt(); panel.descending = object["descending"].toBool(); return true;
}
bool decodeBytes(const QJsonValue &v, QByteArray &bytes) {
    if (!v.isString()) return false;
    const auto decoded = QByteArray::fromBase64Encoding(v.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded) return false; bytes = decoded.decoded; return true;
}
SessionLoadState parse(const QByteArray &bytes, WindowSession &session, QJsonObject &document) {
    QJsonParseError error; const auto json = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !json.isObject()) return SessionLoadState::Invalid;
    document = json.object();
    if (!integer(document["version"], 1, 2147483647)) return SessionLoadState::Invalid;
    if (document["version"].toInt() != 1) return SessionLoadState::Unsupported;
    if (document.contains("folderViews")) {
        session.folderViews = decodeFolderViews(document["folderViews"]);
        if (!session.folderViews) return SessionLoadState::Invalid;
    }
    if (document.contains("favorites")) {
        if (!document["favorites"].isArray()) return SessionLoadState::Invalid;
        const auto entries = document["favorites"].toArray();
        if (entries.size() > 64) return SessionLoadState::Invalid;
        QVector<FavoriteEntry> favorites;
        for (const auto &value : entries) {
            if (!value.isObject()) return SessionLoadState::Invalid;
            const auto entry = value.toObject();
            if (!entry["id"].isString() || !entry["label"].isString() || !entry["path"].isString()) return SessionLoadState::Invalid;
            favorites.append({entry["id"].toString(), entry["label"].toString(), entry["path"].toString()});
        }
        if (!validateFavorites(favorites).isEmpty()) return SessionLoadState::Invalid;
        session.favorites = favorites;
    }
    if (!decodePanel(document["left"], session.left) || !decodePanel(document["right"], session.right)
        || !integer(document["activePanel"], 0, 1) || !decodeBytes(document["geometry"], session.geometry)
        || !decodeBytes(document["windowState"], session.windowState) || !decodeBytes(document["splitterState"], session.splitterState)) return SessionLoadState::Invalid;
    for (const auto &key : {"dark", "dualPane", "treeVisible", "previewVisible"}) if (!document[key].isBool()) return SessionLoadState::Invalid;
    session.dark = document["dark"].toBool(); session.dualPane = document["dualPane"].toBool(); session.treeVisible = document["treeVisible"].toBool();
    session.previewVisible = document["previewVisible"].toBool(); session.activePanel = document["activePanel"].toInt(); return SessionLoadState::Loaded;
}
QJsonObject encodePanel(const PanelSession &panel) {
    QJsonArray tabs; for (const auto &tab : panel.tabs) tabs.append(QJsonObject{{"path", tab.path}, {"filter", tab.filter}});
    return {{"tabs", tabs}, {"currentTab", panel.currentTab}, {"sortColumn", panel.sortColumn}, {"descending", panel.descending}};
}
QString writeRecord(const QString &path, const QByteArray &bytes) {
    if (QFileInfo(path).isSymLink() || (QFileInfo::exists(path) && !QFileInfo(path).isFile())) return "O destino do registro não é um arquivo normal.";
    QSaveFile file(path); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return file.errorString();
    if (file.write(bytes) != bytes.size()) { file.cancelWriting(); return file.errorString(); }
    if (!file.commit()) return file.errorString(); return {};
}
}

QString validateFavorites(const QVector<FavoriteEntry> &favorites) {
    if (favorites.size() > 64) return "Você pode guardar até 64 favoritos.";
    QSet<QString> ids;
    for (const auto &entry : favorites) {
        if (QUuid(entry.id).isNull() || ids.contains(QUuid(entry.id).toString())) return "A identificação de um favorito é inválida ou repetida.";
        ids.insert(QUuid(entry.id).toString());
        if (entry.label.trimmed().isEmpty() || entry.label.size() > 256 || entry.label.contains(QChar(0))) return "Dê um nome de até 256 caracteres a cada favorito.";
        if (!QDir::isAbsolutePath(entry.path) || entry.path.size() > 32768 || entry.path.contains(QChar(0))) return "Use um caminho completo para cada favorito.";
    }
    return {};
}

SessionStore::SessionStore(QString path) : m_path(std::move(path)) {}
QString SessionStore::defaultPath() {
    // Stable storage identity: branding changes must not abandon existing sessions.
    const auto location = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (location.isEmpty()) return {};
    return QFileInfo(location).dir().filePath("Navega/session.json");
}
SessionLoadResult SessionStore::load() {
    m_loaded = true; m_document = {}; m_writable = true; SessionLoadResult result;
    if (m_path.isEmpty()) { m_writable = false; return result; }
    const auto record = readRecord(m_path); m_existed = record.exists; m_baseline = digest(record.bytes);
    if (!record.error.isEmpty()) { m_writable = false; result.state = SessionLoadState::Invalid; result.message = "Não foi possível ler a sessão salva: " + record.error; return result; }
    if (record.exists) {
        result.state = parse(record.bytes, result.session, m_document);
        if (result.state == SessionLoadState::Loaded) return result;
        if (result.state == SessionLoadState::Unsupported) { m_writable = false; result.message = "A sessão foi salva por uma versão mais recente e será preservada."; return result; }
    }
    const auto backup = readRecord(m_path + ".bak"); WindowSession recovered; QJsonObject document;
    if (backup.exists && backup.error.isEmpty() && parse(backup.bytes, recovered, document) == SessionLoadState::Loaded) {
        result.state = SessionLoadState::Recovered; result.session = recovered; m_document = document;
        result.message = "A sessão foi recuperada pela cópia de segurança."; return result;
    }
    m_document = {};
    if (record.exists) { result.state = SessionLoadState::Invalid; result.message = "A sessão salva está incompleta. As opções padrão serão usadas e o registro anterior será preservado."; }
    return result;
}
QString SessionStore::save(const WindowSession &session) {
    if (!m_loaded) load();
    if (!m_writable) return "O registro anterior da sessão será preservado.";
    auto document = m_document;
    document["version"] = 1; document["left"] = encodePanel(session.left); document["right"] = encodePanel(session.right);
    document["geometry"] = QString::fromLatin1(session.geometry.toBase64()); document["windowState"] = QString::fromLatin1(session.windowState.toBase64());
    document["splitterState"] = QString::fromLatin1(session.splitterState.toBase64()); document["dark"] = session.dark; document["dualPane"] = session.dualPane;
    document["treeVisible"] = session.treeVisible; document["previewVisible"] = session.previewVisible; document["activePanel"] = session.activePanel;
    if (session.favorites) {
        QJsonArray entries;
        for (const auto &entry : *session.favorites) entries.append(QJsonObject{{"id", entry.id}, {"label", entry.label}, {"path", entry.path}});
        document["favorites"] = entries;
    } else document.remove("favorites");
    if (session.folderViews) document["folderViews"] = encodeFolderViews(*session.folderViews);
    else document.remove("folderViews");
    const auto bytes = QJsonDocument(document).toJson(); WindowSession checked; QJsonObject parsed;
    if (bytes.size() > maxRecordBytes || parse(bytes, checked, parsed) != SessionLoadState::Loaded) return "A sessão atual não pôde ser representada no registro.";
    if (!QDir::isAbsolutePath(m_path)) return "O caminho do registro da sessão não é completo.";
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) return "Não foi possível preparar a pasta da sessão.";
    QLockFile lock(m_path + ".lock"); if (!lock.tryLock(0)) return "Outra janela está guardando a sessão. O registro existente foi preservado.";
    const auto previous = readRecord(m_path);
    if (!previous.error.isEmpty()) return "O registro anterior não pôde ser conferido: " + previous.error;
    if (previous.exists != m_existed || digest(previous.bytes) != m_baseline) return "Outra janela alterou a sessão salva. O registro existente foi preservado.";
    if (previous.exists && previous.bytes == bytes) return {}; // Idempotent, no backup churn.
    if (previous.exists) {
        WindowSession old; QJsonObject oldDocument;
        const auto oldState = parse(previous.bytes, old, oldDocument);
        const auto backupPath = oldState == SessionLoadState::Loaded ? m_path + ".bak" : m_path + ".preserved-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".json";
        const auto error = writeRecord(backupPath, previous.bytes); if (!error.isEmpty()) return "Não foi possível preservar a sessão anterior: " + error;
    }
    const auto error = writeRecord(m_path, bytes); if (!error.isEmpty()) return error;
    m_document = document; m_baseline = digest(bytes); m_existed = true; return {};
}
