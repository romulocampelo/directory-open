#include "folderreport.h"
#include "fileoperation.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QPainter>
#include <QPagedPaintDevice>
#include <QPdfWriter>
#include <QTextLayout>
#include <QTextOption>
#include <functional>

namespace {
using Row = std::function<void(const QString &, const QString &, const QString &, const QString &)>;
bool openNew(QFile &file, QString *error) {
    const auto path = QDir::fromNativeSeparators(file.fileName());
    const auto nameError = validateItemName(QFileInfo(path).fileName());
    if (!QDir::isAbsolutePath(path) || path.size() > 32767 || path.contains(QChar(0))
        || path.startsWith("//?/") || path.startsWith("//./") || path.mid(2).contains(':') || !nameError.isEmpty()) {
        if (error) *error = "Escolha um caminho absoluto e nome de arquivo normal, sem streams ou dispositivos. " + nameError;
        return false;
    }
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        if (error) *error = file.errorString(); return false;
    }
    return true;
}
QString printable(QString text) {
    text.replace('\\', "\\\\"); text.replace('\r', "\\r"); text.replace('\n', "\\n"); text.replace('\t', "\\t");
    // Keep data from creating apparent report headings or bidi-reordered paths.
    QString out; out.reserve(text.size());
    for (const auto c : text) {
        const auto u = c.unicode();
        if (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format || u == 0x2028 || u == 0x2029)
            out += QString("\\u%1").arg(uint(u), 4, 16, QChar('0'));
        else out += c;
    }
    return out;
}
QString spreadsheetSafe(QString value) {
    // Prefix untrusted formula-like cells, including leading whitespace/control.
    // The protective apostrophe is documented in the report; ordinary paths stay exact.
    qsizetype first = 0;
    while (first < value.size() && (value[first].isSpace() || value[first].category() == QChar::Other_Control
           || value[first].category() == QChar::Other_Format)) ++first;
    if (first < value.size() && QString("=+-@'＝＋－＠").contains(value[first])) value.prepend('\'');
    return value;
}
QString csv(QString value) {
    value = spreadsheetSafe(std::move(value));
    value.replace('"', "\"\""); return '"' + value + '"';
}
bool valid(const FolderSizeResult &r, QString *error) {
    quint64 chars = 0; for (const auto &root : r.request.roots) chars += quint64(root.size());
    if (r.state == FolderSizeState::Running || !r.finished.isValid()) {
        if (error) *error = "Aguarde o cálculo terminar ou ser cancelado antes de gerar o relatório."; return false;
    }
    if (r.request.roots.size() > 256 || chars > 1048576) {
        if (error) *error = "Seleção excede os limites de relatório (256 raízes / 2 MiB UTF-16)."; return false;
    }
    return true;
}
void visit(const FolderSizeResult &r, const Row &row) {
    auto field = [&](const QString &key, const QString &value) { row("Relatório", key, value, {}); };
    auto number = [&](const QString &key, quint64 value) { field(key, QString::number(value)); };
    field("Produto", "Directory Open " DIROPEN_VERSION);
    field("Cobertura", folderReportCoverage(r));
    field("Cálculo", folderSizeStateText(r.state)); field("Listagem", folderListingStateText(r.listingState));
    field("Política", "Leitura ao longo do tempo, sem snapshot. Bytes lógicos do fluxo principal por caminho; hard links por nome; raízes em união; reparse points omitidos. Sem tamanho recursivo por raiz/pasta.");
    field("Codificação", "UTF-8 BOM. CSV: vírgula, campos entre aspas. CSV/TXT: apóstrofo protetor em células semelhantes a fórmulas (inclusive após espaços/controles) ou iniciadas por apóstrofo. TXT/impressão: barras, controles e quebras têm representação textual escapada.");
    field("Início UTC", r.started.toUTC().toString(Qt::ISODateWithMs));
    field("Fim UTC", r.finished.toUTC().toString(Qt::ISODateWithMs));
    field("Duração ms", QString::number(r.elapsedMs));
    number(r.state == FolderSizeState::Complete ? "Tamanho lógico observado bytes" : "Subtotal lógico observado bytes (não é total)", r.bytes);
    number("Arquivos", r.files); number("Pastas", r.folders); number("Entradas examinadas", r.examined);
    number("Erros", r.errors); number("Links omitidos", r.skippedLinks); number("Sobreposições omitidas", r.overlaps); number("Alterações detectadas", r.changes);
    field("Overflow", r.overflow ? "Sim; subtotal anterior preservado" : "Não");
    field("Limite do cálculo atingido", r.limitReason.isEmpty() ? "Nenhum registrado" : r.limitReason);
    number("Limite entradas", r.request.maxEntries); field("Limite profundidade", QString::number(r.request.maxDepth));
    field("Limite prazo ms", QString::number(r.request.maxElapsedMs));
    field("Limites fixos", "256 raízes / 2 MiB UTF-16; 512 componentes iniciais; 4096 handles ancestrais; primeiros 20 detalhes de erro. I/O em curso pode exceder o prazo.");
    number("Listagem retida linhas", quint64(r.listing.size())); number("Registros observados para listagem", r.listingObserved);
    number("Listagem retida bytes contabilizados", r.listingBytes);
    number("Limite retenção linhas", r.request.maxListingEntries); number("Limite retenção bytes", r.request.maxListingBytes);
    field("Limite da listagem atingido", r.listingLimitReason.isEmpty() ? "Nenhum registrado" : r.listingLimitReason);
    number("Detalhes de erro retidos", quint64(r.details.size()));
    for (const auto &path : r.request.roots) row("Seleção solicitada", "Pasta", path, {});
    for (const auto &path : r.roots) row("Raízes efetivas (união)", "Pasta", path, {});
    for (const auto &detail : r.details) row("Erros (primeiros 20)", "Detalhe", detail, {});
    for (const auto &entry : r.listing)
        row("Listagem observada", entry.directory ? "Pasta (sem subtotal)" : "Arquivo", entry.path,
            entry.directory ? QString{} : QString::number(entry.bytes));
}
}
QString folderReportCoverage(const FolderSizeResult &r) {
    if (r.state != FolderSizeState::Complete) return "COBERTURA INCOMPLETA DO CÁLCULO — SUBTOTAL; listagem: " + folderListingStateText(r.listingState);
    if (r.listingState == FolderListingState::NotRequested) return "Cálculo completo no escopo lido; LISTAGEM NÃO SOLICITADA";
    if (r.listingState != FolderListingState::Complete) return "Cálculo completo no escopo lido; COBERTURA INCOMPLETA DA LISTAGEM";
    return "Cálculo e listagem completos no escopo lido; sem snapshot";
}
bool writeFolderReport(const FolderSizeResult &r, const QString &path, FolderReportFormat format, QString *error) {
    if (!valid(r, error)) return false;
    QFile file(path);
    if (!openNew(file, error)) return false;
    bool ok = true;
    auto write = [&](const QByteArray &data) { if (ok && file.write(data) != data.size()) ok = false; };
    write(QByteArray::fromHex("efbbbf"));
    write(format == FolderReportFormat::Csv ? "\"Seção\",\"Campo/tipo\",\"Valor/caminho\",\"Bytes lógicos do arquivo\"\r\n" : "Directory Open - relatório observado\r\n");
    visit(r, [&](const QString &a, const QString &b, const QString &c, const QString &d) {
        const QString line = format == FolderReportFormat::Csv ? csv(a)+','+csv(b)+','+csv(c)+','+csv(d)+"\r\n"
            : printable(spreadsheetSafe(a))+"\t"+printable(spreadsheetSafe(b))+"\t"+printable(spreadsheetSafe(c))+"\t"+printable(spreadsheetSafe(d))+"\r\n";
        write(line.toUtf8());
    });
    ok = file.flush() && ok;
    if (!ok && error) *error = "Falha ao gravar; saída incompleta preservada em " + path + ": " + file.errorString();
    file.close(); return ok;
}

bool paintFolderReport(const FolderSizeResult &r, QPagedPaintDevice &device, QString *error) {
    if (!valid(r, error)) return false;
    // Fixed point coordinates keep preview, PDF and printer pagination compatible.
    const QRectF page = device.pageLayout().paintRect(QPageLayout::Point);
    const qreal width = page.width(), height = page.height();
    if (width < 180 || height < 220) { if (error) *error = "Área de impressão muito pequena; escolha papel maior."; return false; }
    QFont body("Segoe UI"), heading("Segoe UI"), small("Segoe UI");
    body.setPixelSize(10); heading.setPixelSize(11); heading.setBold(true); small.setPixelSize(9);
    const auto coverage = printable(folderReportCoverage(r));
    QPainter painter;
    auto pass = [&](bool draw) {
        int pageNumber = 0; qreal y = 0;
        auto next = [&] {
            if (++pageNumber > 500) { if (error) *error = "Relatório excede 500 páginas. Use CSV/TXT ou colete uma listagem menor."; return false; }
            if (draw && pageNumber > 1 && !device.newPage()) { if (error) *error = "Falha ao criar página."; return false; }
            if (draw) {
                painter.setFont(heading); painter.drawText(QRectF(0,0,width,20), "Directory Open - relatório de pastas");
                auto boldSmall=small; boldSmall.setBold(true); painter.setFont(boldSmall);
                painter.drawText(QRectF(0,23,width,50), Qt::TextWordWrap, coverage);
                painter.drawLine(QPointF(0,77),QPointF(width,77));
                painter.setFont(small);
                painter.drawText(QRectF(0,height-20,width,20), Qt::AlignRight, QString("Página %1 | sem snapshot").arg(pageNumber));
            }
            y=85; return true;
        };
        bool ok=next();
        visit(r, [&](const QString &a, const QString &b, const QString &c, const QString &d) {
            if (!ok) return;
            QString text = printable(a)+" | "+printable(b)+": "+printable(c);
            if (!d.isEmpty()) text += " | "+d+" bytes lógicos";
            QTextLayout layout(text, body); QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); layout.setTextOption(option);
            layout.beginLayout();
            while (true) {
                auto line=layout.createLine(); if (!line.isValid()) break;
                line.setLineWidth(width);
                const qreal lineHeight=line.height()+2;
                if (y+lineHeight>height-32 && !next()) {ok=false;break;}
                if (draw) line.draw(&painter,QPointF(0,y));
                y+=lineHeight;
            }
            layout.endLayout(); y+=5;
        });
        return ok;
    };
    // Refuse excessive reports before producing pages or queuing a print job.
    if (!pass(false)) return false;
    if (!painter.begin(&device)) { if (error) *error = "Não foi possível iniciar a saída de impressão."; return false; }
    const qreal scale=device.logicalDpiX()/72.0;
    painter.scale(scale,device.logicalDpiY()/72.0);
    // Explicit non-default ink is recorded by Qt's QPicture preview. An unchanged
    // default black pen can be omitted and inherit NoPen when the scene replays it.
    painter.setPen(QColor(24,24,24));
    const bool ok=pass(true); const bool ended=painter.end();
    if (!ended && error) *error="Falha ao finalizar a saída de impressão.";
    return ok && ended;
}
bool writeFolderReportPdf(const FolderSizeResult &r, const QString &path, QString *error) {
    if (!valid(r,error)) return false;
    QFile file(path);
    if (!openNew(file, error)) return false;
    bool ok;
    {
        QPdfWriter pdf(&file); pdf.setTitle("Directory Open - relatório de pastas"); pdf.setCreator("Directory Open " DIROPEN_VERSION);
        pdf.setPageSize(QPageSize(QPageSize::A4)); pdf.setPageMargins(QMarginsF(15,15,15,15)); pdf.setResolution(144);
        ok=paintFolderReport(r,pdf,error);
    }
    ok=file.flush() && ok;
    if (!ok && error) *error="Saída PDF incompleta preservada em "+path+". "+*error;
    file.close(); return ok;
}
