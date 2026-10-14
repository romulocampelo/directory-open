#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Data.Pdf.h>
#include "previewloader.h"
#include "imagepreview.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QStringDecoder>
#include <cmath>
#include <limits>

namespace {
constexpr qint64 maxFileBytes = 64 * 1024 * 1024;
constexpr qint64 maxTextBytes = 1024 * 1024;
constexpr qint64 maxBitmapPixels = 4096 * 4096;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Data::Pdf;
struct PdfCache {
    PdfDocument document{nullptr};
    InMemoryRandomAccessStream stream{nullptr};
    QByteArray digest;
    void clear() { document = nullptr; stream = nullptr; digest.clear(); }
};
QString windows1252(const QByteArray &bytes) {
    const int count = MultiByteToWideChar(1252, 0, bytes.constData(), int(bytes.size()), nullptr, 0);
    QString text(count, Qt::Uninitialized);
    if (count) MultiByteToWideChar(1252, 0, bytes.constData(), int(bytes.size()), reinterpret_cast<wchar_t *>(text.data()), count);
    return text;
}
PreviewResult decode(const PreviewRequest &request, PdfCache &pdf, bool &runtimeReady, const std::atomic<quint64> &latest) {
    PreviewResult result; result.path = request.path; result.serial = request.serial;
    const auto stale = [&] { return latest.load() != request.serial; };
    auto fail = [&](const QString &message) { result.kind = PreviewKind::Error; result.message = message; return result; };
    if (request.path.isEmpty()) { pdf.clear(); return result; }
    const QFileInfo before(request.path);
    if (before.isDir()) { pdf.clear(); result.message = "Pasta: " + request.path; return result; }
    if (!before.isFile()) { pdf.clear(); return fail("Arquivo indisponível ou sem acesso."); }
    QFile file(request.path);
    if (!file.open(QIODevice::ReadOnly)) { pdf.clear(); return fail("Não foi possível ler o arquivo: " + file.errorString()); }
    const auto prefix = file.peek(1024);
    const bool isPdf = before.suffix().compare("pdf", Qt::CaseInsensitive) == 0 || prefix.startsWith("%PDF-");
    QImageReader probe(&file);
    const bool isImage = !isPdf && probe.canRead();
    if (stale()) return result;
    if (isPdf || isImage) {
        if (file.size() > maxFileBytes) { pdf.clear(); return fail("Este arquivo excede o limite de 64 MiB do visualizador."); }
        file.seek(0); const auto bytes = file.read(maxFileBytes + 1);
        if (bytes.size() > maxFileBytes || file.error() != QFileDevice::NoError) { pdf.clear(); return fail("Leitura incompleta ou arquivo acima do limite."); }
        file.close();
        const QFileInfo after(request.path);
        if (!after.exists() || after.size() != before.size() || after.lastModified() != before.lastModified()) {
            pdf.clear(); return fail("O arquivo mudou durante a leitura. Use Atualizar no visualizador.");
        }
        if (stale()) return result;
        if (isImage) {
            pdf.clear(); const auto decoded = decodeImagePreview(bytes, 4096);
            if (!decoded.error.isEmpty()) return fail(decoded.error);
            result.originalSize = decoded.originalSize; result.image = decoded.image;
            result.kind = PreviewKind::Image; return result;
        }
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
            if (request.page < 0 || uint32_t(request.page) >= pages) return fail("Página de PDF fora do documento.");
            if (stale()) return result;
            auto page = pdf.document.GetPage(uint32_t(request.page)); const auto size = page.Size();
            if (!std::isfinite(size.Width) || !std::isfinite(size.Height) || size.Width <= 0 || size.Height <= 0 || size.Width > 100000 || size.Height > 100000) {
                page.Close(); return fail("Dimensões da página de PDF inválidas ou excessivas.");
            }
            const auto renderSize = QSize(qMax(1, int(std::ceil(size.Width))), qMax(1, int(std::ceil(size.Height)))).scaled(1800, 2400, Qt::KeepAspectRatio);
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
            result.kind = PreviewKind::Pdf; result.pages = int(pages); result.page = request.page;
            result.originalSize = QSize(qMax(1, int(std::ceil(size.Width))), qMax(1, int(std::ceil(size.Height))));
            return result;
        } catch (const winrt::hresult_error &error) {
            pdf.clear(); return fail("Não foi possível visualizar o PDF (arquivo inválido, protegido ou indisponível). " + QString::fromStdWString(std::wstring(error.message())));
        }
    }
    pdf.clear(); file.seek(0); auto bytes = file.read(maxTextBytes + 1);
    if (file.error() != QFileDevice::NoError) return fail("Não foi possível terminar a leitura do texto: " + file.errorString());
    result.truncated = bytes.size() > maxTextBytes; if (result.truncated) bytes.truncate(maxTextBytes);
    auto encoding = request.encoding;
    QStringConverter::Encoding codec = QStringConverter::Utf8;
    bool unicodeBom = false;
    if (encoding == PreviewEncoding::Auto) {
        if (bytes.startsWith(QByteArray::fromHex("fffe0000"))) { codec = QStringConverter::Utf32LE; result.encoding = "UTF-32 LE"; unicodeBom = true; }
        else if (bytes.startsWith(QByteArray::fromHex("0000feff"))) { codec = QStringConverter::Utf32BE; result.encoding = "UTF-32 BE"; unicodeBom = true; }
        else if (bytes.startsWith(QByteArray::fromHex("fffe"))) { codec = QStringConverter::Utf16LE; result.encoding = "UTF-16 LE"; unicodeBom = true; }
        else if (bytes.startsWith(QByteArray::fromHex("feff"))) { codec = QStringConverter::Utf16BE; result.encoding = "UTF-16 BE"; unicodeBom = true; }
    } else if (encoding == PreviewEncoding::Utf16LE || encoding == PreviewEncoding::Utf16BE) {
        codec = encoding == PreviewEncoding::Utf16LE ? QStringConverter::Utf16LE : QStringConverter::Utf16BE;
        result.encoding = encoding == PreviewEncoding::Utf16LE ? "UTF-16 LE" : "UTF-16 BE"; unicodeBom = true;
    }
    const QStringList textExtensions{"txt", "log", "md", "json", "csv", "ini", "xml", "cpp", "h", "py", "js", "ts", "css", "html", "cmake"};
    result.textCandidate = textExtensions.contains(before.suffix().toLower()) || before.fileName() == "CMakeLists.txt";
    if (!unicodeBom) {
        qsizetype controls = 0; for (unsigned char c : bytes) if (c < 9 || (c > 13 && c < 32)) ++controls;
        if (bytes.contains(char(0)) || (!bytes.isEmpty() && controls > bytes.size() / 20)) return fail(result.textCandidate ? "Codificação não reconhecida. Escolha a codificação do texto abaixo." : "Este formato não possui visualizador nesta versão. Use Abrir para o aplicativo associado.");
    }
    result.textCandidate = true;
    if (encoding == PreviewEncoding::Windows1252) { result.text = windows1252(bytes); result.encoding = "Windows-1252"; }
    else if (encoding == PreviewEncoding::Latin1) { result.text = QString::fromLatin1(bytes); result.encoding = "ISO-8859-1"; }
    else {
        QStringDecoder decoder(codec); result.text = decoder(bytes);
        if (decoder.hasError()) {
            if (encoding != PreviewEncoding::Auto || unicodeBom) return fail("O texto não é válido na codificação escolhida. Escolha outra codificação.");
            result.text = windows1252(bytes); result.encoding = "Windows-1252 (alternativa ao UTF-8)";
        } else if (result.encoding.isEmpty()) result.encoding = "UTF-8";
    }
    const QFileInfo after(request.path);
    if (!after.exists() || after.size() != before.size() || after.lastModified() != before.lastModified()) return fail("O arquivo mudou durante a leitura. Use Atualizar no visualizador.");
    result.kind = PreviewKind::Text; return result;
}
}

PreviewWorker::PreviewWorker(QObject *parent) : QThread(parent) {}
PreviewWorker::~PreviewWorker() { stop(); wait(); }
quint64 PreviewWorker::submit(PreviewRequest request) {
    QMutexLocker lock(&m_mutex); request.serial = ++m_serial; m_pending = request; m_wake.wakeOne(); return request.serial;
}
void PreviewWorker::stop() {
    QMutexLocker lock(&m_mutex); m_stopping = true; ++m_serial; m_pending.reset(); m_result.reset(); m_wake.wakeOne();
}
std::optional<PreviewResult> PreviewWorker::takeResult() {
    QMutexLocker lock(&m_mutex); m_notified = false; auto result = std::move(m_result); m_result.reset(); return result;
}
void PreviewWorker::run() {
    // This private worker owns its DPI context. Native PDF rendering uses system
    // scaling even when the caller UI is offscreen/DPI-unaware.
    const auto previousDpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    bool runtimeReady = false; // Activate the native runtime only for the first PDF.
    {
        PdfCache pdf;
        while (true) {
            PreviewRequest request;
            { QMutexLocker lock(&m_mutex); while (!m_pending && !m_stopping) m_wake.wait(&m_mutex); if (m_stopping) break; request = *m_pending; m_pending.reset(); }
            PreviewResult result;
            try { result = decode(request, pdf, runtimeReady, m_serial); }
            catch (const std::exception &) { result.path = request.path; result.serial = request.serial; result.kind = PreviewKind::Error; result.message = "Falha ao preparar a visualização."; pdf.clear(); }
            bool notify = false;
            { QMutexLocker lock(&m_mutex); if (request.serial == m_serial.load()) { m_result = std::move(result); notify = !m_notified; m_notified = true; } }
            if (notify) emit ready();
        }
    }
    if (runtimeReady) winrt::uninit_apartment();
    if (previousDpi) SetThreadDpiAwarenessContext(previousDpi);
}
