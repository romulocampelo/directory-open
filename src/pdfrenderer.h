#pragma once
#include <QByteArray>
#include <QImage>
#include <QString>
#include <QRectF>
#include <QList>
#include <functional>
#include <memory>
struct PdfRenderResult {
    QImage image;
    QSize originalSize;
    int pages = 0, page = 0;
    QString message;
    QByteArray digest;
    QSizeF pointSize;
};
enum class PdfTextAction { None, Select, SelectAll, Find };
struct PdfTextRequest {
    PdfTextAction action = PdfTextAction::None;
    QByteArray digest;
    int page = 0, matchIndex = -1;
    QPointF start, end;
    QString query;
    bool backwards = false;
};
struct PdfTextResult {
    QString text, message;
    QList<QRectF> bounds;
    int page = -1, matchIndex = -1, examinedPages = 0;
    bool found = false, complete = false;
};
// Construct, use and destroy on the owning worker thread.
class PdfRenderer final {
public:
    PdfRenderer();
    ~PdfRenderer();
    void clear();
    PdfRenderResult render(const QByteArray &bytes, int page, QSize maximum, const std::function<bool()> &stale);
    PdfRenderResult renderPage(int page, QSize maximum, const std::function<bool()> &stale);
    PdfTextResult interact(const PdfTextRequest &request, const std::function<bool()> &stale);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
