#pragma once
#include <QByteArray>
#include <QImage>
#include <QString>
#include <functional>
#include <memory>
struct PdfRenderResult {
    QImage image;
    QSize originalSize;
    int pages = 0, page = 0;
    QString message;
};
// Construct, use and destroy on the owning worker thread.
class PdfRenderer final {
public:
    PdfRenderer();
    ~PdfRenderer();
    void clear();
    PdfRenderResult render(const QByteArray &bytes, int page, QSize maximum, const std::function<bool()> &stale);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
