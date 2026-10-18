#include "mediatools.h"
#include "externalprocess.h"
#include "batchrename.h"
#include "fileorigin.h"
#include <algorithm>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <windows.h>

namespace {
QString clean(const QString &p) { return QDir::cleanPath(QDir::fromNativeSeparators(p)); }
bool ordinaryFile(const QString &p) { const QFileInfo f(p); return QDir::isAbsolutePath(p) && f.isFile() && !f.isSymLink() && !f.isJunction(); }
struct Locks {
    QVector<HANDLE> handles;
    ~Locks() { for (const auto h : handles) CloseHandle(h); }
    bool add(const QString &path, bool directory) {
        const auto native = QDir::toNativeSeparators(path);
        const auto h = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), directory ? FILE_READ_ATTRIBUTES : GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(h, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            || bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory) { CloseHandle(h); return false; }
        handles << h; return true;
    }
};
ExternalResult execute(const QString &exe, const QStringList &args, const std::atomic_bool &cancel, bool exif = false) {
    ExternalCommand c; c.executable = exe; c.arguments = args;
    if (exif) {
        for(const auto &arg:args)if(arg.contains('\r')||arg.contains('\n')){ExternalResult bad;bad.message="Parâmetro contém quebra de linha não suportada.";return bad;}
        auto utf8Args=args;
        if(utf8Args.first()=="-config")utf8Args.remove(0,2);
        c.arguments={"-config","","-charset","filename=UTF8","-@","-"};
        c.input=(utf8Args.join('\n')+'\n').toUtf8();
    }
    for (const auto &key : c.environment.keys()) if (key.startsWith("MAGICK_") || key == "FFREPORT" || key == "PERL5OPT" || key == "PERL5LIB" || key == "EXIFTOOL_HOME") c.environment.remove(key);
    c.environment.insert("MAGICK_THREAD_LIMIT", "2");
    c.environment.insert("LC_ALL", "C"); c.environment.insert("LC_CTYPE", "C"); c.environment.insert("LANG", "C");
    return runExternal(c, cancel);
}
bool success(const ExternalResult &r) { return r.state == ExternalState::Exited && r.exitCode == 0; }
QString failure(const ExternalResult &r) { return r.message.isEmpty() ? QString("A ferramenta retornou código %1: %2").arg(r.exitCode).arg(QString::fromUtf8(r.errors)) : r.message; }
QStringList exifArgs(const QString &source) { return {"-config", "", "-charset", "filename=UTF8", "-j", "-G1", "-s", "--", source}; }
const QString demuxers="mov,mp4,m4a,3gp,3g2,mj2,matroska,webm,avi,mp3,wav,flac,aac,ogg,asf,mpeg,mpegts";
QStringList probeArgs(const QString &source) { return {"-v", "error", "-protocol_whitelist", "file,pipe", "-format_whitelist",demuxers,"-show_format", "-show_streams", "-of", "json", source}; }
QJsonObject readMetadata(const QString &exe, const QString &file, const std::atomic_bool &cancel, QString &error) {
    const auto r = execute(exe, exifArgs(file), cancel, true);
    if (!success(r)) { error = failure(r); return {}; }
    QJsonParseError e; const auto d = QJsonDocument::fromJson(r.output, &e);
    if (e.error != QJsonParseError::NoError || !d.isArray() || d.array().size() != 1 || !d.array().first().isObject()) { error = "Metadados inválidos/incompletos."; return {}; }
    const auto o = d.array().first().toObject();
    for (auto it = o.begin(); it != o.end(); ++it) if (it.key().endsWith(":Error") || it.key() == "Error") { error = it.value().toString(); return {}; }
    return o;
}
QString textValue(const QJsonValue &v) {
    if (v.isUndefined()) return "(ausente)";
    if (v.isString()) return v.toString();
    if (v.isDouble()) return QString::number(v.toDouble(), 'g', 16);
    if (v.isArray()) return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    if (v.isObject()) return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    return v.isBool() ? (v.toBool() ? "sim" : "não") : "(nulo)";
}
QJsonValue tagValue(const QJsonObject &o, const QString &tag) {
    const auto name = tag.section(':', 1); const auto family = tag.section(':', 0, 0);
    if (o.contains(tag)) return o[tag];
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (it.key().section(':', -1) != name) continue;
        if (family == "EXIF" && (it.key().startsWith("ExifIFD:") || it.key().startsWith("IFD0:"))) return it.value();
    }
    return QJsonValue(QJsonValue::Undefined);
}
QString imageCoder(const QString &file) {
    const auto ext = QFileInfo(file).suffix().toLower();
    return ext == "jpg" || ext == "jpeg" ? "jpeg" : ext == "png" ? "png" : ext == "webp" ? "webp" : ext == "tif" || ext == "tiff" ? "tiff" : QString();
}
QStringList imageInput(const QString &file) {
    // Force a raster decoder: brackets, @ and colons in filenames are not syntax.
    return {"-limit", "thread", "2", "-limit", "memory", "256MiB", "-limit", "map", "512MiB", "-limit", "disk", "1GiB", imageCoder(file) + ":" + file + "[0]"};
}
QStringList imageTransform(const MediaOptions &o){
    QStringList args{"-auto-orient"};
    if(o.width||o.height)args<<"-resize"<<(o.width?QString::number(o.width):QString())+"x"+(o.height?QString::number(o.height):QString())+">";
    if(o.rotation)args<<"-rotate"<<QString::number(o.rotation);
    if(o.format=="jpg")args<<"-background"<<"white"<<"-alpha"<<"remove"<<"-alpha"<<"off";
    args<<"-quality"<<QString::number(o.quality);return args;
}
QJsonObject probe(const QString &exe, const QString &file, const std::atomic_bool &cancel, QString &error) {
    const auto r = execute(exe, probeArgs(file), cancel);
    if (!success(r)) { error = failure(r); return {}; }
    QJsonParseError e; const auto d = QJsonDocument::fromJson(r.output, &e);
    if (e.error != QJsonParseError::NoError || !d.isObject() || !d.object()["streams"].isArray() || !d.object()["format"].isObject()) { error = "ffprobe não retornou um catálogo válido."; return {}; }
    return d.object();
}
QString details(const QJsonObject &o) {
    QStringList lines; const auto format = o["format"].toObject();
    lines << QString("Formato: %1; duração: %2 s; tamanho: %3 bytes; taxa: %4 bit/s")
        .arg(format["format_name"].toString(), format["duration"].toString("desconhecida"), format["size"].toString("desconhecido"), format["bit_rate"].toString("desconhecida"));
    for (const auto &v : o["streams"].toArray()) {
        const auto s = v.toObject();
        auto line=QString("Fluxo %1: %2, codec %3").arg(s["index"].toInt()).arg(s["codec_type"].toString("desconhecido"),s["codec_name"].toString("desconhecido"));
        if(s["codec_type"].toString()=="video"){
            if(s["width"].toInt()>0&&s["height"].toInt()>0)line+=QString(", %1×%2").arg(s["width"].toInt()).arg(s["height"].toInt());
            const auto rate=s["avg_frame_rate"].toString().split('/');bool ok=false;const auto numerator=rate.value(0).toDouble(&ok);const auto denominator=rate.value(1).toDouble();if(ok&&denominator>0)line+=QString(", %1 fps").arg(numerator/denominator,0,'g',6);
        }else if(s["codec_type"].toString()=="audio"){
            if(!s["sample_rate"].toString().isEmpty())line+=", "+s["sample_rate"].toString()+" Hz";
            if(s["channels"].toInt()>0)line+=QString(", %1 canais").arg(s["channels"].toInt());
        }
        lines<<line;
    }
    return lines.join('\n');
}
}

QMap<QString, QString> editableMetadataTags() {
    return {{"EXIF:DateTimeOriginal", "EXIF — Data da captura"}, {"EXIF:CreateDate", "EXIF — Data de criação"}, {"EXIF:ModifyDate", "EXIF — Data de modificação"},
        {"EXIF:Artist", "EXIF — Autor"}, {"EXIF:Copyright", "EXIF — Direitos autorais"}, {"IPTC:Keywords", "IPTC — Palavra-chave (substitui lista)"},
        {"IPTC:Caption-Abstract", "IPTC — Descrição"}, {"XMP-dc:Title", "XMP — Título"}, {"XMP-dc:Description", "XMP — Descrição"},
        {"XMP-dc:Creator", "XMP — Autor (substitui lista)"}, {"XMP-dc:Rights", "XMP — Direitos"}, {"XMP-dc:Subject", "XMP — Assunto (substitui lista)"}};
}
QString mediaToolExecutable(const QString &name, const QString &configured) {
    if (!configured.isEmpty()) return discoverTool(name, configured);
    const QDir app(QCoreApplication::applicationDirPath());
    QStringList folders{app.filePath("tools"), app.filePath("../tools")};
    for (const auto &base : folders) {
        QStringList candidates{base + "/exiftool/exiftool.exe", base + "/imagemagick/magick.exe"};
        if (name == "exiftool") for(const auto &d:QDir(base+"/exiftool").entryList(QDir::Dirs|QDir::NoDotAndDotDot)) candidates << base+"/exiftool/"+d+"/exiftool.exe";
        if (name == "ffmpeg" || name == "ffprobe") {
            const auto dirs = QDir(base + "/ffmpeg").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const auto &d : dirs) candidates << base + "/ffmpeg/" + d + "/bin/" + name + ".exe";
        }
        for (const auto &p : candidates) if (QFileInfo(p).baseName() == name && QFileInfo(p).isFile()) return clean(p);
    }
    return discoverTool(name);
}
MediaPlan inspectMedia(const QStringList &sources, const MediaOptions &options, const std::atomic_bool &cancel) {
    MediaPlan plan; plan.options = options;
    if (sources.isEmpty() || sources.size() > 200) { plan.error = "Selecione de 1 a 200 arquivos."; return plan; }
    if (!ordinaryFile(options.executable) || (options.task == MediaTask::AudioVideo && !ordinaryFile(options.probeExecutable))) { plan.error = "Selecione os executáveis disponíveis da ferramenta."; return plan; }
    if (options.width < 0 || options.width > 16384 || options.height < 0 || options.height > 16384 || (options.rotation != 0 && options.rotation != 90 && options.rotation != 180 && options.rotation != 270) || options.quality < 1 || options.quality > 100) { plan.error = "Dimensões, rotação ou qualidade inválidas."; return plan; }
    if (options.task == MediaTask::Images && !QStringList{"png", "jpg", "webp", "tiff"}.contains(options.format)) { plan.error = "Escolha PNG, JPEG, WebP ou TIFF."; return plan; }
    if (options.task == MediaTask::AudioVideo && !QStringList{"mp4", "mp3", "wav", "flac"}.contains(options.format)) { plan.error = "Escolha MP4, MP3, WAV ou FLAC."; return plan; }
    const auto allowed = editableMetadataTags();
    for (auto it = options.tags.begin(); it != options.tags.end(); ++it) {
        if (!allowed.contains(it.key()) || it.value().isEmpty() || it.value().size() > 2048 || it.value().contains(QChar(0)) || it.value().contains('\r') || it.value().contains('\n')) { plan.error = "Campo/valor não permitido. Valores vazios e remoção de campos não fazem parte desta etapa."; return plan; }
        if (it.key().startsWith("EXIF:") && it.key().contains("Date")) {
            const auto date = QDateTime::fromString(it.value(), "yyyy:MM:dd HH:mm:ss");
            if (!date.isValid() || date.toString("yyyy:MM:dd HH:mm:ss") != it.value()) { plan.error = "Datas EXIF devem usar AAAA:MM:DD HH:MM:SS, sem fuso nesta etapa."; return plan; }
        }
    }
    QSet<QString> outputs;
    for (const auto &source : sources) {
        if (cancel.load()) { plan.cancelled = true; return plan; }
        if (!ordinaryFile(source)) { plan.error = "A origem não é um arquivo normal: " + source; return plan; }
        Locks locks; if (!locks.add(source, false)) { plan.error = "A origem está ocupada ou mudou: " + source; return plan; }
        const auto identity = inspectRenameItem(source); if (identity.state != RenameProbeState::Present) { plan.error = identity.error; return plan; }
        MediaItem item; item.source = clean(source); item.identity = identity.identity;
        const auto ext = options.task == MediaTask::Metadata ? QFileInfo(source).suffix() : options.format;
        item.output = QFileInfo(source).completeBaseName() + "." + ext;
        if (!validateItemName(item.output).isEmpty() || outputs.contains(item.output.toCaseFolded())) { plan.error = "Há nomes de saída inválidos ou repetidos. Separe a seleção."; return plan; }
        outputs.insert(item.output.toCaseFolded());
        if (options.task == MediaTask::Metadata) {
            const auto o = readMetadata(options.executable, source, cancel, plan.error); if (!plan.error.isEmpty()) return plan;
            QStringList rows;
            if (options.tags.isEmpty()) {
                for (auto it = o.begin(); it != o.end(); ++it) {
                    if (rows.size() == 256) { rows << "(primeiros 256 campos; consulta limitada)"; break; }
                    rows << it.key() + ": " + textValue(it.value()).left(1024);
                }
            } else for (auto it = options.tags.begin(); it != options.tags.end(); ++it) rows << allowed[it.key()] + ": " + textValue(tagValue(o, it.key())) + " → " + it.value();
            item.details = rows.join('\n');
        } else if (options.task == MediaTask::Images) {
            if (imageCoder(source).isEmpty()) { plan.error = "Nesta etapa, use JPEG, PNG, WebP ou TIFF raster."; return plan; }
            auto args = QStringList{"identify", "-ping", "-format", "%w %h"} + imageInput(source);
            const auto r = execute(options.executable, args, cancel);
            if (!success(r)) { plan.error = failure(r); return plan; }
            const auto parts = QString::fromUtf8(r.output).trimmed().split(' '); bool w=false,h=false;
            const auto width = parts.value(0).toInt(&w), height = parts.value(1).toInt(&h);
            if (!w || !h || width < 1 || height < 1 || qint64(width)*height > 100000000) { plan.error = "Imagem inválida ou maior que 100 megapixels."; return plan; }
            item.details = QString("%1×%2 → %3; limite %4×%5 (0 = livre, sem ampliar); girar %6°; qualidade %7. Primeira página/quadro; orientação EXIF aplicada.")
                .arg(width).arg(height).arg(ext).arg(options.width).arg(options.height).arg(options.rotation).arg(options.quality);
            if(plan.items.isEmpty()){
                const auto preview=execute(options.executable,imageInput(source)+imageTransform(options)+QStringList{"-thumbnail","480x320>","png:-"},cancel);
                if(!success(preview)){plan.error=failure(preview);return plan;}
                item.preview=QImage::fromData(preview.output,"PNG");
                if(item.preview.isNull()||item.preview.width()>480||item.preview.height()>320){plan.error="Prévia visual inválida.";return plan;}
            }
        } else {
            const auto o = probe(options.probeExecutable, source, cancel, plan.error); if (!plan.error.isEmpty()) return plan;
            bool video=false,audio=false;
            for (const auto &v : o["streams"].toArray()) { const auto type=v.toObject()["codec_type"].toString(); video |= type=="video"; audio |= type=="audio"; }
            if(options.format=="mp4")for(const auto &v:o["streams"].toArray()){const auto transfer=v.toObject()["color_transfer"].toString();if(transfer=="smpte2084"||transfer=="arib-std-b67"){plan.error="Vídeo HDR ainda não é suportado por este perfil de conversão.";return plan;}}
            if ((options.format == "mp4" && !video) || (options.format != "mp4" && !audio)) { plan.error = "O arquivo não contém o fluxo necessário para a saída escolhida."; return plan; }
            item.details = details(o) + "\nSaída: " + options.format + (options.format == "mp4" ? " (H.264/AAC, primeiro vídeo/áudio; sem legendas/anexos/HDR preservado)" : " (primeiro áudio)");
        }
        plan.items << item;
    }
    if (cancel.load()) plan.cancelled = true;
    return plan;
}

MediaResult convertMedia(const MediaPlan &plan, const std::atomic_bool &cancel, const std::function<void(QString)> &progress) {
    MediaResult result; const auto &o = plan.options;
    if (!plan.valid()) { result.message = "Gere uma prévia válida antes de executar."; return result; }
    if (o.task == MediaTask::Metadata && o.tags.isEmpty()) { result.message = "Acrescente campos para editar em cópias."; return result; }
    if (!QDir::isAbsolutePath(o.destination) || QFileInfo::exists(o.destination) || !QFileInfo(QFileInfo(o.destination).absolutePath()).isDir()) { result.message = "Escolha uma pasta nova dentro de uma pasta existente."; return result; }
    if (cancel.load()) { result.state = ToolState::Cancelled; result.message = "Cancelado antes de criar a saída."; return result; }
    Locks outputLocks;
    if (!outputLocks.add(QFileInfo(o.destination).absolutePath(), true) || !QDir().mkdir(o.destination) || !outputLocks.add(o.destination, true)) { result.message = "Não foi possível reservar uma pasta nova normal para a saída."; return result; }
    QTemporaryDir work(QDir(o.destination).filePath(".trabalho-XXXXXX"));
    if (!work.isValid()) { result.message = "Não foi possível preparar a pasta temporária."; return result; }
    work.setAutoRemove(false);
    auto items = [&] {
    for (const auto &item : plan.items) {
        if (cancel.load()) { result.state = ToolState::Cancelled; result.message = "Cancelado; saídas concluídas preservadas."; return; }
        Locks sourceLock;
        if(!sourceLock.add(QFileInfo(item.source).absolutePath(),true)||!sourceLock.add(item.source,false)){result.state=ToolState::Incomplete;result.message="Não foi possível reservar a origem: "+item.source;return;}
        const auto now = inspectRenameItem(item.source);
        if (now.state != RenameProbeState::Present || now.identity != item.identity) { result.state = ToolState::Incomplete; result.message = "A origem mudou desde a prévia: " + item.source; return; }
        if (progress) progress(QString("Processando %1 de %2: %3").arg(result.completed + 1).arg(plan.items.size()).arg(QFileInfo(item.source).fileName()));
        const auto temp = work.filePath(QString("item-%1.%2").arg(result.completed).arg(QFileInfo(item.output).suffix())); QStringList args;
        if (o.task == MediaTask::Metadata) {
            args = {"-config", "", "-charset", "filename=UTF8", "-charset", "IPTC=UTF8", "-o", temp};
            for (auto it=o.tags.begin(); it!=o.tags.end(); ++it) args << "-"+it.key()+"="+it.value();
            if (std::any_of(o.tags.keyBegin(),o.tags.keyEnd(),[](const QString &s){return s.startsWith("IPTC:");})) args << "-IPTC:CodedCharacterSet=UTF8";
            args << "--" << item.source;
        } else if (o.task == MediaTask::Images) {
            args = imageInput(item.source)+imageTransform(o);args << (o.format + ":" + temp);
        } else {
            args = {"-hide_banner", "-loglevel", "error", "-nostdin", "-n", "-threads", "2", "-protocol_whitelist", "file,pipe", "-format_whitelist",demuxers,"-i", item.source};
            if (o.format == "mp4") args << "-map" << "0:v:0" << "-map" << "0:a:0?" << "-c:v" << "libx264" << "-preset" << "medium" << "-crf" << "23" << "-pix_fmt" << "yuv420p" << "-vf" << "pad=ceil(iw/2)*2:ceil(ih/2)*2" << "-c:a" << "aac" << "-b:a" << "192k" << "-movflags" << "+faststart";
            else { args << "-map" << "0:a:0" << "-vn" << "-c:a" << (o.format == "mp3" ? "libmp3lame" : o.format == "wav" ? "pcm_s16le" : "flac"); if (o.format == "mp3") args << "-b:a" << "192k"; }
            args << "-map_metadata" << "0" << "-threads" << "2" << "-filter_threads" << "2" << temp;
        }
        const auto r = execute(o.executable, args, cancel, o.task==MediaTask::Metadata);
        if (!success(r)) { result.state = cancel.load() ? ToolState::Cancelled : ToolState::Incomplete; result.message = failure(r); result.diagnostics = QString::fromUtf8(r.errors); return; }
        QString error;
        if (o.task == MediaTask::Metadata) {
            const auto values = readMetadata(o.executable, temp, cancel, error);
            for (auto it=o.tags.begin(); it!=o.tags.end() && error.isEmpty(); ++it) {
                const auto value = tagValue(values, it.key());
                if (value.isArray() ? value.toArray() != QJsonArray{it.value()} : value.toString() != it.value()) error = "O formato não confirmou o valor gravado de " + it.key();
            }
        } else if (o.task == MediaTask::Images) {
            const auto verify = execute(o.executable, QStringList{"identify"} + imageInput(temp), cancel);
            if (!success(verify)) error = failure(verify);
        } else {
            const auto verified = probe(o.probeExecutable, temp, cancel, error);
            if (error.isEmpty() && verified["streams"].toArray().isEmpty()) error = "A saída não contém fluxos válidos.";
            if (error.isEmpty()) {
                const auto decode = execute(o.executable, {"-v", "error", "-xerror", "-nostdin", "-threads", "2", "-protocol_whitelist", "file,pipe", "-format_whitelist",demuxers,"-i", temp, "-f", "null", "-"}, cancel);
                if (!success(decode)) error = failure(decode);
            }
        }
        if (!error.isEmpty() || cancel.load()) { result.state = cancel.load() ? ToolState::Cancelled : ToolState::Incomplete; result.message = error.isEmpty() ? "Cancelado antes de publicar." : error; return; }
        // Preserve the origin mark on all derived files; fail rather than strip it.
        std::optional<QByteArray> origin;
        error = readFileOrigin(item.source, origin);
        if (error.isEmpty()) error = writeFileOrigin(temp, origin);
        if (!error.isEmpty()) { result.state=ToolState::Incomplete; result.message=error; return; }
        const auto output = QDir(o.destination).filePath(item.output);
        if (!QFile::rename(temp, output)) { result.state=ToolState::Incomplete; result.message="Não foi possível publicar a saída sem substituição: "+output; return; }
        result.outputs << output; ++result.completed;
    }
    result.state=ToolState::Completed; result.message=QString("Concluído: %1 arquivos novos. Originais preservados.").arg(result.completed); return;
    };
    items();
    if (!work.remove()) { result.diagnostics += "\nA pasta temporária não pôde ser removida: " + work.path(); if(result.state==ToolState::Completed) {result.state=ToolState::Incomplete;result.message="Conversão concluída, com limpeza pendente. Confira o diagnóstico.";} }
    return result;
}
MediaToolsJob::MediaToolsJob(QStringList sources, MediaOptions options, QObject *parent) : QThread(parent), m_sources(std::move(sources)), m_options(std::move(options)) {}
MediaToolsJob::MediaToolsJob(MediaPlan plan, QObject *parent) : QThread(parent), m_apply(true), m_plan(std::move(plan)) {}
MediaToolsJob::~MediaToolsJob() { cancel(); wait(); }
void MediaToolsJob::run() { if(m_apply) m_result=convertMedia(m_plan,m_cancel,[this](QString s){emit progress(std::move(s));}); else m_plan=inspectMedia(m_sources,m_options,m_cancel); }
