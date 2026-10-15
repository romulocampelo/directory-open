#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Data.Pdf.h>
#include "pdfrenderer.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QImageReader>
#include <cmath>
#include <limits>
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Data::Pdf;
struct PdfRenderer::Impl {
    bool runtimeReady = false;
    PdfDocument document{nullptr};
    InMemoryRandomAccessStream stream{nullptr};
    QByteArray digest;
    void clear() { document = nullptr; stream = nullptr; digest.clear(); }
};

PdfRenderer::PdfRenderer() : m_impl(std::make_unique<Impl>()) {}
PdfRenderer::~PdfRenderer() { m_impl->clear(); if (m_impl->runtimeReady) winrt::uninit_apartment(); }
void PdfRenderer::clear() { m_impl->clear(); }
PdfRenderResult PdfRenderer::render(const QByteArray &bytes, int pageNumber, QSize maximum, const std::function<bool()> &stale) {
    PdfRenderResult result;
    auto fail = [&](const QString &message) { result.message = message; return result; };
    constexpr qint64 maxFileBytes = 64 * 1024 * 1024;
    constexpr qint64 maxBitmapPixels = 4096 * 4096;
    if (bytes.size() > maxFileBytes || !maximum.isValid() || maximum.width() > 4096 || maximum.height() > 4096)
        return fail("O PDF excede o orçamento do renderizador.");
    struct DpiScope {
        DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        ~DpiScope() { if (previous) SetThreadDpiAwarenessContext(previous); }
    } dpi;
    auto &pdf = *m_impl; auto &runtimeReady = pdf.runtimeReady;
    if (!runtimeReady) {
        try { winrt::init_apartment(winrt::apartment_type::multi_threaded); runtimeReady = true; }
        catch (const winrt::hresult_error &) { return fail("O renderizador de PDF do Windows não está disponível."); }
    }
    try {
        const auto digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
        if (!pdf.document || digest != pdf.digest) {
            pdf.clear(); pdf.stream = InMemoryRandomAccessStream();
            DataWriter writer(pdf.stream);
            writer.WriteBytes(winrt::array_view<const uint8_t>(reinterpret_cast<const uint8_t *>(bytes.constData()), reinterpret_cast<const uint8_t *>(bytes.constData()) + bytes.size()));
            writer.StoreAsync().get(); writer.DetachStream(); pdf.stream.Seek(0);
            if (stale()) { pdf.clear(); return result; }
            pdf.document = PdfDocument::LoadFromStreamAsync(pdf.stream).get(); pdf.digest = digest;
        }
        const auto pages = pdf.document.PageCount();
        if (pages == 0 || pages > uint32_t(std::numeric_limits<int>::max())) return fail("O PDF não contém uma contagem de páginas utilizável.");
        if (pageNumber < 0 || uint32_t(pageNumber) >= pages) return fail("Página de PDF fora do documento.");
        if (stale()) return result;
        auto page = pdf.document.GetPage(uint32_t(pageNumber)); const auto size = page.Size();
        if (!std::isfinite(size.Width) || !std::isfinite(size.Height) || size.Width <= 0 || size.Height <= 0 || size.Width > 100000 || size.Height > 100000) {
            page.Close(); return fail("Dimensões da página de PDF inválidas ou excessivas.");
        }
        const auto renderSize = QSize(qMax(1, int(std::ceil(size.Width))), qMax(1, int(std::ceil(size.Height)))).scaled(maximum, Qt::KeepAspectRatio);
        // Destination sizes are DIPs. Normalize at this owning boundary, before
        // native allocation, so system scaling cannot inflate the bitmap.
        const double dpiScale = qMax(1.0, double(GetDpiForSystem()) / 96.0);
        PdfPageRenderOptions options; options.DestinationWidth(uint32_t(qMax(1, int(std::floor(renderSize.width() / dpiScale))))); options.DestinationHeight(uint32_t(qMax(1, int(std::floor(renderSize.height() / dpiScale)))));
        InMemoryRandomAccessStream rendered; page.RenderToStreamAsync(rendered, options).get(); page.Close();
        if (rendered.Size() > uint64_t(maxFileBytes)) return fail("A página renderizada excede o limite de memória.");
        DataReader reader(rendered.GetInputStreamAt(0)); const auto length = uint32_t(rendered.Size()); reader.LoadAsync(length).get();
        QByteArray png(length, Qt::Uninitialized);
        reader.ReadBytes(winrt::array_view<uint8_t>(reinterpret_cast<uint8_t *>(png.data()), reinterpret_cast<uint8_t *>(png.data()) + png.size()));
        QBuffer renderedInput; renderedInput.setData(png); renderedInput.open(QIODevice::ReadOnly);
        QImageReader renderedReader(&renderedInput, "PNG");
        const auto bitmapSize = renderedReader.size();
        if (!bitmapSize.isValid() || qint64(bitmapSize.width()) * bitmapSize.height() > maxBitmapPixels) return fail("A página renderizada excede o limite de resolução do visualizador.");
        result.image = renderedReader.read();
        if (result.image.isNull()) return fail("O Windows não retornou uma página de PDF válida.");
        result.pages = int(pages); result.page = pageNumber;
        result.originalSize = QSize(qMax(1, int(std::ceil(size.Width))), qMax(1, int(std::ceil(size.Height))));
        return result;
    } catch (const winrt::hresult_error &error) {
        pdf.clear(); return fail("Não foi possível visualizar o PDF (arquivo inválido, protegido ou indisponível). " + QString::fromStdWString(std::wstring(error.message())));
    }
}
