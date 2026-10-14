#include "previewwidget.h"
#include <QAbstractScrollArea>
#include <QAction>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTextCursor>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cmath>

// Paint only the visible region. Zoom and rotation never allocate an enlarged
// bitmap or change image bytes; original dimensions define the 100% scale.
class ImageCanvas final : public QAbstractScrollArea {
public:
    explicit ImageCanvas(QWidget *parent) : QAbstractScrollArea(parent) {
        setObjectName("previewCanvas"); setFrameShape(QFrame::NoFrame);
        connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
        connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    }
    void content(const QImage &image, const QSize &original, bool reset = true) { m_image = image; m_original = original; if (reset) { m_angle = 0; m_fit = true; } updateGeometryAndPaint(); }
    void zoom(double multiplier) { m_fit = false; m_scale = qBound(0.05, m_scale * multiplier, 4.0); updateGeometryAndPaint(); }
    void actual() { m_fit = false; m_scale = 1.0; updateGeometryAndPaint(); }
    void fit() { m_fit = true; updateGeometryAndPaint(); }
    void rotate() { m_angle = (m_angle + 90) % 360; updateGeometryAndPaint(); }
protected:
    void resizeEvent(QResizeEvent *event) override { QAbstractScrollArea::resizeEvent(event); updateGeometryAndPaint(); }
    void wheelEvent(QWheelEvent *event) override {
        if (event->modifiers().testFlag(Qt::ControlModifier) && !m_image.isNull()) { zoom(event->angleDelta().y() > 0 ? 1.25 : 0.8); event->accept(); }
        else QAbstractScrollArea::wheelEvent(event);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(viewport()); painter.fillRect(viewport()->rect(), palette().brush(QPalette::Base));
        if (m_image.isNull()) return;
        const QSize display = rotatedSize(); const double w = display.width() * m_scale, h = display.height() * m_scale;
        const double x = qMax(0.0, (viewport()->width() - w) / 2) - horizontalScrollBar()->value();
        const double y = qMax(0.0, (viewport()->height() - h) / 2) - verticalScrollBar()->value();
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        if (m_angle == 90) { painter.translate(x + w, y); painter.rotate(90); painter.drawImage(QRectF(0, 0, h, w), m_image); }
        else if (m_angle == 180) { painter.translate(x + w, y + h); painter.rotate(180); painter.drawImage(QRectF(0, 0, w, h), m_image); }
        else if (m_angle == 270) { painter.translate(x, y + h); painter.rotate(270); painter.drawImage(QRectF(0, 0, h, w), m_image); }
        else painter.drawImage(QRectF(x, y, w, h), m_image);
    }
private:
    QImage m_image;
    QSize m_original;
    int m_angle = 0;
    bool m_fit = true;
    double m_scale = 1.0;
    QSize rotatedSize() const { return m_angle % 180 ? m_original.transposed() : m_original; }
    void updateGeometryAndPaint() {
        if (m_image.isNull() || !m_original.isValid()) { horizontalScrollBar()->setRange(0, 0); verticalScrollBar()->setRange(0, 0); viewport()->update(); return; }
        const auto size = rotatedSize();
        if (m_fit) m_scale = qMin(1.0, qMin(double(qMax(1, viewport()->width())) / size.width(), double(qMax(1, viewport()->height())) / size.height()));
        horizontalScrollBar()->setPageStep(viewport()->width()); verticalScrollBar()->setPageStep(viewport()->height());
        horizontalScrollBar()->setRange(0, m_fit ? 0 : qMax(0, int(std::ceil(size.width() * m_scale)) - viewport()->width()));
        verticalScrollBar()->setRange(0, m_fit ? 0 : qMax(0, int(std::ceil(size.height() * m_scale)) - viewport()->height()));
        viewport()->update();
    }
};

PreviewWidget::PreviewWidget(QWidget *parent) : QWidget(parent) {
    setObjectName("filePreview"); setMinimumWidth(320);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(6, 6, 6, 6); layout->setSpacing(4);
    m_title = new QLabel(tr("Selecione um arquivo"), this); m_title->setObjectName("previewTitle"); m_title->setTextFormat(Qt::PlainText); m_title->setWordWrap(true); layout->addWidget(m_title);
    auto *navigation = new QToolBar(this); navigation->setMovable(false); layout->addWidget(navigation);
    auto *previous = navigation->addAction(tr("Anterior")); previous->setObjectName("previewPreviousFile");
    auto *next = navigation->addAction(tr("Próximo")); next->setObjectName("previewNextFile");
    auto *enlarge = navigation->addAction(tr("Ampliar")); enlarge->setObjectName("previewEnlarge");
    connect(previous, &QAction::triggered, this, [this] { emit navigateFile(-1); });
    connect(next, &QAction::triggered, this, [this] { emit navigateFile(1); });
    connect(enlarge, &QAction::triggered, this, &PreviewWidget::enlargeRequested);
    auto *refresh = navigation->addAction(tr("Atualizar")); refresh->setObjectName("previewReload");
    connect(refresh, &QAction::triggered, this, &PreviewWidget::reload);
    auto *tools = new QToolBar(this); tools->setMovable(false); layout->addWidget(tools);
    m_zoomOut = tools->addAction(tr("−")); m_zoomOut->setObjectName("previewZoomOut"); m_zoomOut->setToolTip(tr("Reduzir (Ctrl+roda do mouse)"));
    m_zoomIn = tools->addAction(tr("+")); m_zoomIn->setObjectName("previewZoomIn"); m_zoomIn->setToolTip(tr("Ampliar (Ctrl+roda do mouse)"));
    m_actual = tools->addAction(tr("100%")); m_actual->setObjectName("previewActualSize");
    m_fit = tools->addAction(tr("Ajustar")); m_fit->setObjectName("previewFit");
    m_rotate = tools->addAction(tr("Girar")); m_rotate->setObjectName("previewRotate"); m_rotate->setToolTip(tr("Girar a visualização 90 graus, sem alterar o arquivo"));
    m_stack = new QStackedWidget(this); m_canvas = new ImageCanvas(this);
    m_text = new QPlainTextEdit(this); m_text->setObjectName("previewText"); m_text->setReadOnly(true); m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_status = new QLabel(tr("Imagens, texto e PDF"), this); m_status->setObjectName("previewMessage"); m_status->setTextFormat(Qt::PlainText); m_status->setAlignment(Qt::AlignCenter); m_status->setWordWrap(true);
    m_stack->addWidget(m_status); m_stack->addWidget(m_canvas); m_stack->addWidget(m_text); layout->addWidget(m_stack, 1);
    connect(m_zoomIn, &QAction::triggered, this, [this] { m_canvas->zoom(1.25); }); connect(m_zoomOut, &QAction::triggered, this, [this] { m_canvas->zoom(0.8); });
    connect(m_actual, &QAction::triggered, m_canvas, &ImageCanvas::actual); connect(m_fit, &QAction::triggered, m_canvas, &ImageCanvas::fit); connect(m_rotate, &QAction::triggered, m_canvas, &ImageCanvas::rotate);
    m_pdfTools = new QWidget(this); auto *pdfLayout = new QHBoxLayout(m_pdfTools); pdfLayout->setContentsMargins(0, 0, 0, 0);
    auto *previousPage = new QPushButton(tr("◀"), this); previousPage->setObjectName("previewPreviousPage"); previousPage->setAccessibleName(tr("Página anterior do PDF"));
    auto *nextPage = new QPushButton(tr("▶"), this); nextPage->setObjectName("previewNextPage"); nextPage->setAccessibleName(tr("Próxima página do PDF"));
    m_page = new QSpinBox(this); m_page->setObjectName("previewPage"); m_page->setAccessibleName(tr("Página atual do PDF")); m_page->setMinimum(1); m_page->setKeyboardTracking(false);
    m_pageCount = new QLabel(this); m_pageCount->setTextFormat(Qt::PlainText);
    pdfLayout->addWidget(previousPage); pdfLayout->addWidget(m_page); pdfLayout->addWidget(m_pageCount); pdfLayout->addWidget(nextPage); pdfLayout->addStretch(); layout->addWidget(m_pdfTools); m_pdfTools->hide();
    connect(previousPage, &QPushButton::clicked, this, [this] { m_page->setValue(m_page->value() - 1); }); connect(nextPage, &QPushButton::clicked, this, [this] { m_page->setValue(m_page->value() + 1); });
    connect(m_page, &QSpinBox::valueChanged, this, [this] { load(); });
    m_textTools = new QWidget(this); auto *textLayout = new QVBoxLayout(m_textTools); textLayout->setContentsMargins(0, 0, 0, 0);
    m_encoding = new QComboBox(this); m_encoding->setObjectName("previewEncoding"); m_encoding->setAccessibleName(tr("Codificação do texto"));
    const std::pair<const char *, PreviewEncoding> encodings[] = {
        {"Automática", PreviewEncoding::Auto}, {"UTF-8", PreviewEncoding::Utf8},
        {"UTF-16 LE", PreviewEncoding::Utf16LE}, {"UTF-16 BE", PreviewEncoding::Utf16BE},
        {"Windows-1252", PreviewEncoding::Windows1252}, {"ISO-8859-1", PreviewEncoding::Latin1}
    };
    for (const auto &[label, encoding] : encodings) m_encoding->addItem(QString::fromUtf8(label), int(encoding));
    textLayout->addWidget(m_encoding); auto *findLayout = new QHBoxLayout;
    m_find = new QLineEdit(this); m_find->setObjectName("previewFind"); m_find->setPlaceholderText(tr("Procurar no texto…")); m_find->setClearButtonEnabled(true); findLayout->addWidget(m_find, 1);
    auto *findPrevious = new QPushButton(tr("◀"), this); findPrevious->setAccessibleName(tr("Ocorrência anterior")); auto *findNext = new QPushButton(tr("▶"), this); findNext->setObjectName("previewFindNext"); findNext->setAccessibleName(tr("Próxima ocorrência"));
    findLayout->addWidget(findPrevious); findLayout->addWidget(findNext); textLayout->addLayout(findLayout); layout->addWidget(m_textTools); m_textTools->hide();
    connect(m_encoding, &QComboBox::currentIndexChanged, this, [this] { load(); }); connect(m_find, &QLineEdit::returnPressed, this, [this] { findText(false); });
    connect(findNext, &QPushButton::clicked, this, [this] { findText(false); }); connect(findPrevious, &QPushButton::clicked, this, [this] { findText(true); });
    m_details = new QLabel(this); m_details->setObjectName("previewDetails"); m_details->setTextFormat(Qt::PlainText); m_details->setWordWrap(true); layout->addWidget(m_details);
    for (auto *action : {m_zoomIn, m_zoomOut, m_actual, m_fit, m_rotate}) action->setEnabled(false);
    m_worker = new PreviewWorker(this); connect(m_worker, &PreviewWorker::ready, this, [this] { auto result = m_worker->takeResult(); if (result) showResult(std::move(*result)); });
}
PreviewWidget::~PreviewWidget() { m_worker->stop(); m_worker->wait(); }
void PreviewWidget::setPath(const QString &path) {
    if (path == m_path) return;
    m_path = path; m_canvas->content({}, {});
    { const QSignalBlocker page(m_page), encoding(m_encoding); m_page->setRange(1, 1); m_page->setValue(1); m_encoding->setCurrentIndex(m_encoding->findData(int(PreviewEncoding::Auto))); }
    m_pdfTools->hide(); m_textTools->hide(); m_find->clear(); load();
}
void PreviewWidget::reload() { const QSignalBlocker blocker(m_page); m_page->setValue(1); load(); }
void PreviewWidget::load() {
    m_title->setText(m_path.isEmpty() ? tr("Selecione um arquivo") : QFileInfo(m_path).fileName()); m_title->setToolTip(m_path);
    m_result = {}; m_text->clear(); m_canvas->content({}, {}, false); m_details->clear(); m_status->setText(m_path.isEmpty() ? tr("Imagens, texto e PDF") : tr("Carregando…")); m_stack->setCurrentWidget(m_status);
    for (auto *action : {m_zoomIn, m_zoomOut, m_actual, m_fit, m_rotate}) action->setEnabled(false);
    PreviewRequest request; request.path = m_path; request.page = m_page->value() - 1; request.encoding = PreviewEncoding(m_encoding->currentData().toInt()); m_serial = m_worker->submit(request); if (!m_worker->isRunning()) m_worker->start();
}
void PreviewWidget::showResult(PreviewResult result) {
    if (result.serial != m_serial) return;
    m_result = std::move(result); const auto &content = m_result;
    const bool picture = content.kind == PreviewKind::Image || content.kind == PreviewKind::Pdf;
    for (auto *action : {m_zoomIn, m_zoomOut, m_actual, m_fit}) action->setEnabled(picture); m_rotate->setEnabled(content.kind == PreviewKind::Image);
    m_textTools->setVisible(content.kind == PreviewKind::Text || content.textCandidate); m_pdfTools->setVisible(content.kind == PreviewKind::Pdf);
    if (picture) {
        m_canvas->content(content.image, content.originalSize, false); m_stack->setCurrentWidget(m_canvas);
        if (content.kind == PreviewKind::Pdf) { const QSignalBlocker blocker(m_page); m_page->setMaximum(content.pages); m_page->setValue(content.page + 1); m_pageCount->setText(tr("de %1").arg(content.pages)); m_details->setText(tr("PDF — página %1 de %2").arg(content.page + 1).arg(content.pages)); }
        else m_details->setText(tr("%1 × %2 pixels%3").arg(content.originalSize.width()).arg(content.originalSize.height()).arg(content.image.size() != content.originalSize ? tr(" — prévia reduzida") : QString{}));
    } else if (content.kind == PreviewKind::Text) {
        m_text->setPlainText(content.text); m_stack->setCurrentWidget(m_text); m_details->setText(content.encoding + (content.truncated ? tr(" — primeiros 1 MiB; conteúdo parcial") : QString{}));
    } else { m_stack->setCurrentWidget(m_status); m_status->setText(!content.message.isEmpty() ? content.message : tr("Imagens, texto e PDF")); }
    emit displayed();
}
void PreviewWidget::findText(bool backwards) {
    if (m_find->text().isEmpty()) return;
    const auto flags = backwards ? QTextDocument::FindBackward : QTextDocument::FindFlags{};
    bool found = m_text->find(m_find->text(), flags);
    if (!found) { auto cursor = m_text->textCursor(); cursor.movePosition(backwards ? QTextCursor::End : QTextCursor::Start); m_text->setTextCursor(cursor); found = m_text->find(m_find->text(), flags); }
    m_find->setStyleSheet(found ? QString{} : QString("border: 1px solid #c95248;")); m_find->setToolTip(found ? QString{} : tr("Texto não encontrado na parte carregada."));
}
