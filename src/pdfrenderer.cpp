#include "pdfrenderer.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QPdfDocument>
#include <QPdfSearchModel>
#include <cmath>

namespace {
constexpr int maxPageCharacters = 65536;
constexpr int maxSearchPages = 128;
constexpr int maxDocumentPages = 10000;
constexpr int maxBounds = 4096;
}
struct PdfRenderer::Impl {
    // QPdfDocument and its QIODevice never cross their worker's boundary.
    QByteArray bytes, digest;
    QBuffer input;
    std::unique_ptr<QPdfDocument> document;
    void clear() { document.reset(); input.close(); input.setData({}); bytes.clear(); digest.clear(); }
};
PdfRenderer::PdfRenderer() : m_impl(std::make_unique<Impl>()) {}
PdfRenderer::~PdfRenderer() = default;
void PdfRenderer::clear() { m_impl->clear(); }
PdfRenderResult PdfRenderer::render(const QByteArray &bytes, int page, QSize maximum, const std::function<bool()> &stale) {
    PdfRenderResult result;
    if (bytes.isEmpty() || bytes.size() > 64 * 1024 * 1024) { clear(); result.message = "O PDF excede o limite de 64 MiB ou está vazio."; return result; }
    if (stale()) return result;
    const auto digest = QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
    auto &pdf = *m_impl;
    if (pdf.digest != digest) {
        pdf.clear(); pdf.bytes = bytes; pdf.input.setData(pdf.bytes); pdf.input.open(QIODevice::ReadOnly);
        pdf.document = std::make_unique<QPdfDocument>(); pdf.document->load(&pdf.input);
        if (pdf.document->status() != QPdfDocument::Status::Ready || pdf.document->pageCount() < 1) {
            pdf.clear(); result.message = "Não foi possível visualizar o PDF (arquivo inválido, protegido ou indisponível)."; return result;
        }
        if (pdf.document->pageCount() > maxDocumentPages) { pdf.clear(); result.message = "O PDF excede o limite de 10.000 páginas."; return result; }
        pdf.digest = digest;
    }
    return renderPage(page,maximum,stale);
}
PdfRenderResult PdfRenderer::renderPage(int page, QSize maximum, const std::function<bool()> &stale) {
    PdfRenderResult result;
    auto &pdf = *m_impl;
    if (!pdf.document || page < 0 || page >= pdf.document->pageCount()) { result.message = "Página de PDF fora do documento."; return result; }
    if (!maximum.isValid() || maximum.width() > 4096 || maximum.height() > 4096) { result.message = "O PDF excede o orçamento do renderizador."; return result; }
    if (stale()) return result;
    const auto size = pdf.document->pagePointSize(page);
    if (!std::isfinite(size.width()) || !std::isfinite(size.height()) || size.width() <= 0 || size.height() <= 0 || size.width() > 100000 || size.height() > 100000) {
        result.message = "Dimensões da página de PDF inválidas ou excessivas."; return result;
    }
    // 100% remains 96 dpi; backend geometry is kept separately in PDF points.
    result.originalSize = QSize(int(std::ceil(size.width()*96/72)),int(std::ceil(size.height()*96/72)));
    const auto bitmapSize = result.originalSize.scaled(maximum,Qt::KeepAspectRatio);
    result.image = pdf.document->render(page,bitmapSize);
    if (stale()) return {};
    if (result.image.isNull()) { result.message = "O renderizador não retornou uma página de PDF válida."; return result; }
    result.pages = pdf.document->pageCount(); result.page = page; result.digest = pdf.digest; result.pointSize = size;
    return result;
}
PdfTextResult PdfRenderer::interact(const PdfTextRequest &request, const std::function<bool()> &stale) {
    PdfTextResult result;
    auto fail = [&](const QString &message) { result.complete = false; result.message = message; return result; };
    auto &pdf = *m_impl;
    if (!pdf.document || request.digest.isEmpty() || request.digest != pdf.digest || request.page < 0 || request.page >= pdf.document->pageCount())
        return fail("A prévia do PDF mudou. Use Atualizar antes de pesquisar ou selecionar.");
    if (stale()) return result;
    if (request.action == PdfTextAction::Find) {
        if (request.query.isEmpty() || request.query.size() > 256) return fail("Informe entre 1 e 256 caracteres para pesquisar.");
        if (request.matchIndex < -1 || request.matchIndex > maxPageCharacters) return fail("Posição de busca do PDF inválida.");
        result.complete = true;
        const int pages = pdf.document->pageCount(), direction = request.backwards ? -1 : 1;
        // Each model lives for ONE examined page. Its timers never run in this
        // worker loop, so it cannot index other pages or accumulate the document.
        for (int offset = 0; offset <= pages && offset < maxSearchPages; ++offset) {
            if (stale()) return {};
            const int page = (request.page + direction*offset%pages + pages)%pages;
            ++result.examinedPages;
            const auto sample = pdf.document->getSelectionAtIndex(page,0,maxPageCharacters+1);
            if (sample.text().size() > maxPageCharacters) { result.complete = false; continue; }
            QPdfSearchModel search;
            search.setDocument(pdf.document.get()); search.setSearchString(request.query);
            const auto matches = search.resultsOnPage(page);
            const int index = offset == 0 && request.matchIndex >= 0 ? request.matchIndex + direction
                : request.backwards ? int(matches.size())-1 : 0;
            if (index < 0 || index >= matches.size()) continue;
            const auto bounds = matches[index].rectangles();
            if (bounds.size() > maxBounds) { result.complete = false; continue; }
            result.found = true; result.page = page; result.matchIndex = index; result.bounds = bounds;
            result.message = QString("Ocorrência %1 na página %2%3").arg(index+1).arg(page+1).arg(result.complete ? QString{} : " — busca parcial");
            return result;
        }
        if (pages+1 > maxSearchPages) result.complete = false;
        result.message = result.complete ? "Texto não encontrado no PDF. Páginas sem texto pesquisável não usam OCR."
            : QString("Busca parcial: %1 páginas examinadas; limite de 128 páginas por busca ou 65.536 caracteres por página. Vá a outra página para continuar; não foi possível concluir a ausência.").arg(result.examinedPages);
        return result;
    }
    result.page = request.page;
    result.complete = true;
    const auto sample = pdf.document->getSelectionAtIndex(request.page,0,maxPageCharacters+1);
    if (sample.text().size() > maxPageCharacters) return fail("A página excede o limite de 65.536 caracteres para seleção e pesquisa.");
    auto selection = sample;
    if (request.action == PdfTextAction::Select) {
        const auto size = pdf.document->pagePointSize(request.page);
        auto inside = [&size](QPointF p) { return std::isfinite(p.x()) && std::isfinite(p.y()) && p.x() >= 0 && p.y() >= 0 && p.x() <= size.width() && p.y() <= size.height(); };
        if (!inside(request.start) || !inside(request.end)) return fail("Seleção fora da página de PDF.");
        selection = pdf.document->getSelection(request.page,request.start,request.end);
    } else if (request.action != PdfTextAction::SelectAll) return fail("Ação de texto do PDF inválida.");
    if (stale()) return {};
    if (selection.text().size() > maxPageCharacters || selection.bounds().size() > maxBounds) return fail("A seleção excede o orçamento de texto/geometria do visualizador.");
    result.text = selection.text();
    for (const auto &polygon : selection.bounds()) result.bounds.append(polygon.boundingRect());
    result.found = !result.text.isEmpty();
    result.message = result.found ? QString("%1 caracteres selecionados — Ctrl+C para copiar").arg(result.text.size()) : "Sem texto selecionável nesse trecho. Não é feita leitura OCR.";
    return result;
}
