#include "previewwidget.h"
#include "mediapreview.h"
#include <QAbstractScrollArea>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMenu>
#include <QContextMenuEvent>
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
        setFocusPolicy(Qt::StrongFocus);
        connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
        connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    }
    void content(const QImage &image, const QSize &original, bool reset = true) { m_image = image; m_original = original; if (reset) { m_angle = 0; m_fit = m_defaultFit; m_scale=1; } updateGeometryAndPaint(); }
    void settings(bool fit,bool smooth){m_defaultFit=fit;m_smooth=smooth;m_fit=fit;m_scale=1;updateGeometryAndPaint();}
    void zoom(double multiplier) { m_fit = false; m_scale = qBound(0.05, m_scale * multiplier, 4.0); updateGeometryAndPaint(); }
    void actual() { m_fit = false; m_scale = 1.0; updateGeometryAndPaint(); }
    void fit() { m_fit = true; updateGeometryAndPaint(); }
    void rotate() { m_angle = (m_angle + 90) % 360; updateGeometryAndPaint(); }
    std::function<void(QPointF,QPointF)> selectText;
    std::function<void()> copyText, selectPage;
    void pdfGeometry(QSizeF size) { m_pdfSize = size; m_bounds.clear(); m_dragging = false; viewport()->setCursor(size.isValid() ? Qt::IBeamCursor : Qt::ArrowCursor); viewport()->update(); }
    void highlight(const QList<QRectF> &bounds, bool reveal = false) {
        m_bounds = bounds;
        if (reveal && !bounds.isEmpty() && m_pdfSize.isValid()) {
            const auto center = bounds.first().center();
            horizontalScrollBar()->setValue(int(center.x()/m_pdfSize.width()*m_original.width()*m_scale - viewport()->width()/2));
            verticalScrollBar()->setValue(int(center.y()/m_pdfSize.height()*m_original.height()*m_scale - viewport()->height()/2));
        }
        viewport()->update();
    }
protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (m_pdfSize.isValid() && event->button() == Qt::LeftButton && paperRect().contains(event->position())) {
            setFocus(); m_dragging = true; m_anchor = toPdf(event->position()); event->accept(); return;
        }
        QAbstractScrollArea::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (m_dragging && event->button() == Qt::LeftButton) {
            m_dragging = false; if (selectText) selectText(m_anchor,toPdf(event->position())); event->accept(); return;
        }
        QAbstractScrollArea::mouseReleaseEvent(event);
    }
    void contextMenuEvent(QContextMenuEvent *event) override {
        if (!m_pdfSize.isValid()) { QAbstractScrollArea::contextMenuEvent(event); return; }
        QMenu menu(this);
        menu.addAction(tr("Selecionar texto da página"),this,[this] { if (selectPage) selectPage(); });
        menu.addAction(tr("Copiar seleção"),this,[this] { if (copyText) copyText(); });
        menu.exec(event->globalPos());
    }
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
        painter.setRenderHint(QPainter::SmoothPixmapTransform,m_smooth);
        if (m_angle == 90) { painter.translate(x + w, y); painter.rotate(90); painter.drawImage(QRectF(0, 0, h, w), m_image); }
        else if (m_angle == 180) { painter.translate(x + w, y + h); painter.rotate(180); painter.drawImage(QRectF(0, 0, w, h), m_image); }
        else if (m_angle == 270) { painter.translate(x, y + h); painter.rotate(270); painter.drawImage(QRectF(0, 0, h, w), m_image); }
        else painter.drawImage(QRectF(x, y, w, h), m_image);
        if (m_pdfSize.isValid()) {
            painter.setPen(Qt::NoPen); painter.setBrush(QColor(55,130,245,90));
            for (const auto &rect : m_bounds) painter.drawRect(QRectF(x+rect.x()/m_pdfSize.width()*w,y+rect.y()/m_pdfSize.height()*h,
                rect.width()/m_pdfSize.width()*w,rect.height()/m_pdfSize.height()*h));
        }
    }
private:
    QImage m_image;
    QSize m_original;
    int m_angle = 0;
    bool m_fit = true,m_defaultFit=true,m_smooth=true;
    double m_scale = 1.0;
    QSizeF m_pdfSize;
    QList<QRectF> m_bounds;
    bool m_dragging = false;
    QPointF m_anchor;
    QRectF paperRect() const {
        const auto size = rotatedSize(); const double w = size.width()*m_scale, h = size.height()*m_scale;
        return {qMax(0.0,(viewport()->width()-w)/2)-horizontalScrollBar()->value(),qMax(0.0,(viewport()->height()-h)/2)-verticalScrollBar()->value(),w,h};
    }
    QPointF toPdf(QPointF point) const {
        const auto rect = paperRect();
        return {qBound(0.0,(point.x()-rect.x())/rect.width()*m_pdfSize.width(),m_pdfSize.width()),
            qBound(0.0,(point.y()-rect.y())/rect.height()*m_pdfSize.height(),m_pdfSize.height())};
    }
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
    auto *preferences=tools->addAction(tr("Preferências…"));preferences->setObjectName("previewPreferences");connect(preferences,&QAction::triggered,this,&PreviewWidget::settingsRequested);
    m_ocr=tools->addAction(tr("OCR…"));m_ocr->setObjectName("previewOcr");m_ocr->setEnabled(false);connect(m_ocr,&QAction::triggered,this,&PreviewWidget::ocrRequested);
    m_zoomOut = tools->addAction(tr("−")); m_zoomOut->setObjectName("previewZoomOut"); m_zoomOut->setToolTip(tr("Reduzir (Ctrl+roda do mouse)"));
    m_zoomIn = tools->addAction(tr("+")); m_zoomIn->setObjectName("previewZoomIn"); m_zoomIn->setToolTip(tr("Ampliar (Ctrl+roda do mouse)"));
    m_actual = tools->addAction(tr("100%")); m_actual->setObjectName("previewActualSize");
    m_fit = tools->addAction(tr("Ajustar")); m_fit->setObjectName("previewFit");
    m_rotate = tools->addAction(tr("Girar")); m_rotate->setObjectName("previewRotate"); m_rotate->setToolTip(tr("Girar a visualização 90 graus, sem alterar o arquivo"));
    m_stack = new QStackedWidget(this); m_canvas = new ImageCanvas(this);
    m_text = new QPlainTextEdit(this); m_text->setObjectName("previewText"); m_text->setReadOnly(true); m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_status = new QLabel(tr("Imagens, texto, PDF, áudio e vídeo"), this); m_status->setObjectName("previewMessage"); m_status->setTextFormat(Qt::PlainText); m_status->setAlignment(Qt::AlignCenter); m_status->setWordWrap(true);
    m_stack->addWidget(m_status); m_stack->addWidget(m_canvas); m_stack->addWidget(m_text); layout->addWidget(m_stack, 1);
    connect(m_zoomIn, &QAction::triggered, this, [this] { m_canvas->zoom(1.25); }); connect(m_zoomOut, &QAction::triggered, this, [this] { m_canvas->zoom(0.8); });
    connect(m_actual, &QAction::triggered, m_canvas, &ImageCanvas::actual); connect(m_fit, &QAction::triggered, m_canvas, &ImageCanvas::fit); connect(m_rotate, &QAction::triggered, m_canvas, &ImageCanvas::rotate);
    m_pdfTools = new QWidget(this); auto *pdfContainer = new QVBoxLayout(m_pdfTools); pdfContainer->setContentsMargins(0,0,0,0);
    auto *pdfLayout = new QHBoxLayout; pdfContainer->addLayout(pdfLayout);
    auto *previousPage = new QPushButton(tr("◀"), this); previousPage->setObjectName("previewPreviousPage"); previousPage->setAccessibleName(tr("Página anterior do PDF"));
    auto *nextPage = new QPushButton(tr("▶"), this); nextPage->setObjectName("previewNextPage"); nextPage->setAccessibleName(tr("Próxima página do PDF"));
    m_page = new QSpinBox(this); m_page->setObjectName("previewPage"); m_page->setAccessibleName(tr("Página atual do PDF")); m_page->setMinimum(1); m_page->setKeyboardTracking(false);
    m_pageCount = new QLabel(this); m_pageCount->setTextFormat(Qt::PlainText);
    pdfLayout->addWidget(previousPage); pdfLayout->addWidget(m_page); pdfLayout->addWidget(m_pageCount); pdfLayout->addWidget(nextPage); pdfLayout->addStretch(); layout->addWidget(m_pdfTools); m_pdfTools->hide();
    connect(previousPage, &QPushButton::clicked, this, [this] { m_page->setValue(m_page->value() - 1); }); connect(nextPage, &QPushButton::clicked, this, [this] { m_page->setValue(m_page->value() + 1); });
    connect(m_page, &QSpinBox::valueChanged, this, [this] { load(); });
    auto *pdfFindRow = new QHBoxLayout; pdfContainer->addLayout(pdfFindRow);
    m_pdfFind = new QLineEdit(this); m_pdfFind->setObjectName("pdfFind"); m_pdfFind->setPlaceholderText(tr("Pesquisar no PDF…")); m_pdfFind->setMaxLength(256); m_pdfFind->setClearButtonEnabled(true); pdfFindRow->addWidget(m_pdfFind,1);
    auto *pdfPrevious = new QPushButton(tr("◀"),this); pdfPrevious->setObjectName("pdfFindPrevious"); pdfPrevious->setAccessibleName(tr("Ocorrência anterior no PDF")); pdfFindRow->addWidget(pdfPrevious);
    auto *pdfNext = new QPushButton(tr("▶"),this); pdfNext->setObjectName("pdfFindNext"); pdfNext->setAccessibleName(tr("Próxima ocorrência no PDF")); pdfFindRow->addWidget(pdfNext);
    auto *pdfSelectionRow = new QHBoxLayout; pdfContainer->addLayout(pdfSelectionRow);
    auto *selectPage = new QPushButton(tr("Selecionar página"),this); selectPage->setObjectName("pdfSelectPage"); pdfSelectionRow->addWidget(selectPage);
    m_pdfCopy = new QPushButton(tr("Copiar seleção"),this); m_pdfCopy->setObjectName("pdfCopySelection"); m_pdfCopy->setEnabled(false); pdfSelectionRow->addWidget(m_pdfCopy); pdfSelectionRow->addStretch();
    m_pdfTextStatus = new QLabel(this); m_pdfTextStatus->setObjectName("pdfTextStatus"); m_pdfTextStatus->setTextFormat(Qt::PlainText); m_pdfTextStatus->setWordWrap(true); pdfContainer->addWidget(m_pdfTextStatus);
    connect(pdfPrevious,&QPushButton::clicked,this,[this] { requestPdfText(PdfTextAction::Find,true); });
    connect(pdfNext,&QPushButton::clicked,this,[this] { requestPdfText(PdfTextAction::Find); });
    connect(m_pdfFind,&QLineEdit::returnPressed,this,[this] { requestPdfText(PdfTextAction::Find); });
    connect(selectPage,&QPushButton::clicked,this,[this] { requestPdfText(PdfTextAction::SelectAll); });
    connect(m_pdfCopy,&QPushButton::clicked,this,&PreviewWidget::copyPdfText);
    m_canvas->selectText = [this](QPointF start,QPointF end) { requestPdfText(PdfTextAction::Select,false,start,end); };
    m_canvas->selectPage = [this] { requestPdfText(PdfTextAction::SelectAll); };
    m_canvas->copyText = [this] { copyPdfText(); };
    connect(m_pdfFind,&QLineEdit::textChanged,this,[this] {
        m_pdfQuery.clear(); m_pdfMatchIndex = -1;
        if (m_result.kind != PreviewKind::Pdf) return;
        m_serial = m_worker->cancelPending(); m_pdfPending = false; m_result.pdfText = {};
        m_canvas->highlight({}); m_pdfCopy->setEnabled(false); m_pdfTextStatus->clear();
    });
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
    qApp->installEventFilter(this);
}
PreviewWidget::~PreviewWidget() { qApp->removeEventFilter(this); if (m_media) m_media->release(); m_worker->stop(); m_worker->wait(); }
bool PreviewWidget::eventFilter(QObject *object, QEvent *event) {
    if (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress) return false;
    const auto *widget = qobject_cast<QWidget *>(object);
    if (!widget || (widget != this && !isAncestorOf(widget))) return false;
    auto *key = static_cast<QKeyEvent *>(event);
    const bool find = (m_result.kind == PreviewKind::Pdf || hasTextPreview(m_result.kind)) && key->matches(QKeySequence::Find);
    const bool onPdf = m_result.kind == PreviewKind::Pdf && (widget == m_canvas || m_canvas->isAncestorOf(widget));
    const bool copy = onPdf && key->matches(QKeySequence::Copy), all = onPdf && key->matches(QKeySequence::SelectAll);
    if (!find && !copy && !all) return false;
    // Own these shortcuts only inside the preview. The manager's Ctrl+F/C/A
    // continue to target the active file panel everywhere else, including when
    // this same preview widget is reparented into the enlarged viewer.
    key->accept();
    if (event->type() == QEvent::KeyPress) {
        if (find) { auto *field = m_result.kind == PreviewKind::Pdf ? m_pdfFind : m_find; field->setFocus(); field->selectAll(); }
        else if (copy) copyPdfText();
        else requestPdfText(PdfTextAction::SelectAll);
    }
    return true;
}
void PreviewWidget::setPath(const QString &path) {
    if (path == m_path) return;
    m_ocr->setEnabled(false);m_path = path; m_canvas->content({}, {});
    { const QSignalBlocker page(m_page), encoding(m_encoding); m_page->setRange(1, 1); m_page->setValue(1); m_encoding->setCurrentIndex(m_encoding->findData(m_settings.encoding)); }
    m_pdfTools->hide(); m_textTools->hide(); m_find->clear(); load();
}
void PreviewWidget::reload() { const QSignalBlocker blocker(m_page); m_page->setValue(1); load(); }
void PreviewWidget::releaseMedia() {
    if (m_media) { m_media->release(); if (m_stack->currentWidget() == m_media) { m_status->setText(tr("Reprodução encerrada. Use Atualizar para carregar novamente.")); m_stack->setCurrentWidget(m_status); } }
}
void PreviewWidget::load() {
    m_ocr->setEnabled(false);m_pdfPending = false; m_pdfMatchIndex = -1; m_pdfCopy->setEnabled(false); m_pdfTextStatus->clear(); m_canvas->pdfGeometry({});
    if (m_media) m_media->release();
    m_title->setText(m_path.isEmpty() ? tr("Selecione um arquivo") : QFileInfo(m_path).fileName()); m_title->setToolTip(m_path);
    m_result = {}; m_text->clear(); m_canvas->content({}, {}, false); m_details->clear(); m_status->setText(m_path.isEmpty() ? tr("Imagens, texto, PDF, áudio e vídeo") : tr("Carregando…")); m_stack->setCurrentWidget(m_status);
    for (auto *action : {m_zoomIn, m_zoomOut, m_actual, m_fit, m_rotate}) action->setEnabled(false);
    if (!m_path.isEmpty() && MediaPreview::accepts(m_path)) {
        PreviewRequest clear; m_serial = m_worker->submit(clear); if (!m_worker->isRunning()) m_worker->start();
        if (!m_media) {
            m_media = new MediaPreview(this); m_stack->addWidget(m_media);
            connect(m_media, &MediaPreview::loaded, this, [this](bool success, const QString &message) {
                if (m_stack->currentWidget() != m_media) return;
                m_result.kind = success ? PreviewKind::Media : PreviewKind::Error; m_result.path = m_path; m_result.message = message; emit displayed();
            });
        }
        m_textTools->hide(); m_pdfTools->hide(); m_stack->setCurrentWidget(m_media); m_media->setPath(m_path); return;
    }
    PreviewRequest request; request.path = m_path;request.imageEdge=m_settings.imageEdge; request.page = m_page->value() - 1; request.encoding = PreviewEncoding(m_encoding->currentData().toInt()); m_serial = m_worker->submit(request); if (!m_worker->isRunning()) m_worker->start();
}
void PreviewWidget::showResult(PreviewResult result) {
    if (result.serial != m_serial || (!m_path.isEmpty() && MediaPreview::accepts(m_path))) return;
    if (result.pdfInteraction) {
        if (m_result.kind != PreviewKind::Pdf) return;
        m_pdfPending = false;
        if (!result.image.isNull()) {
            m_result.image = result.image; m_result.originalSize = result.originalSize; m_result.page = result.page;
            m_result.pdfPointSize = result.pdfPointSize; showPdfPage();
        }
        m_result.pdfText = std::move(result.pdfText);
        if (m_result.pdfText.matchIndex >= 0) m_pdfMatchIndex = m_result.pdfText.matchIndex;
        m_canvas->highlight(m_result.pdfText.bounds,m_result.pdfText.matchIndex >= 0);
        m_pdfCopy->setEnabled(!m_result.pdfText.text.isEmpty()); m_pdfTextStatus->setText(m_result.pdfText.message);
        emit displayed(); return;
    }
    m_result = std::move(result); const auto &content = m_result;
    m_ocr->setEnabled(!content.image.isNull()&&(content.kind==PreviewKind::Image||content.kind==PreviewKind::Pdf));
    const bool picture = content.kind == PreviewKind::Image || content.kind == PreviewKind::Pdf;
    for (auto *action : {m_zoomIn, m_zoomOut, m_actual, m_fit}) action->setEnabled(picture); m_rotate->setEnabled(content.kind == PreviewKind::Image);
    m_encoding->setVisible(content.kind!=PreviewKind::Document);
    m_textTools->setVisible(hasTextPreview(content.kind) || content.textCandidate); m_pdfTools->setVisible(content.kind == PreviewKind::Pdf);
    if (picture) {
        m_stack->setCurrentWidget(m_canvas);
        if (content.kind == PreviewKind::Pdf) { showPdfPage(); m_pdfTextStatus->setText(tr("Arraste sobre o texto para selecionar. Ctrl+A seleciona a página; Ctrl+C copia.")); }
        else { m_canvas->content(content.image,content.originalSize,false); m_details->setText(tr("%1 × %2 pixels%3").arg(content.originalSize.width()).arg(content.originalSize.height()).arg(content.image.size() != content.originalSize ? tr(" — prévia reduzida") : QString{})); }
    } else if (hasTextPreview(content.kind)) {
        m_text->setPlainText(content.text); m_stack->setCurrentWidget(m_text); m_details->setText(content.kind==PreviewKind::Document?content.message:content.encoding + (content.truncated ? tr(" — primeiros 1 MiB; conteúdo parcial") : QString{}));
    } else { m_stack->setCurrentWidget(m_status); m_status->setText(!content.message.isEmpty() ? content.message : tr("Imagens, texto, PDF, áudio e vídeo")); }
    emit displayed();
}
void PreviewWidget::showPdfPage() {
    m_canvas->content(m_result.image,m_result.originalSize,false); m_canvas->pdfGeometry(m_result.pdfPointSize);
    const QSignalBlocker blocker(m_page); m_page->setMaximum(m_result.pages); m_page->setValue(m_result.page+1);
    m_pageCount->setText(tr("de %1").arg(m_result.pages)); m_details->setText(tr("PDF — página %1 de %2").arg(m_result.page+1).arg(m_result.pages));
}
void PreviewWidget::requestPdfText(PdfTextAction action, bool backwards, QPointF start, QPointF end) {
    if (m_result.kind != PreviewKind::Pdf) return;
    if (action == PdfTextAction::Find && m_pdfFind->text().isEmpty()) return;
    PreviewRequest request; request.path = m_path;request.imageEdge=m_settings.imageEdge; request.page = m_result.page;
    auto &text = request.pdfText; text.action = action; text.digest = m_result.pdfDigest; text.page = m_result.page;
    text.start = start; text.end = end; text.backwards = backwards; text.query = m_pdfFind->text();
    if (text.query != m_pdfQuery) m_pdfMatchIndex = -1;
    m_pdfQuery = text.query; text.matchIndex = m_pdfMatchIndex;
    m_result.pdfText = {}; m_pdfCopy->setEnabled(false); m_canvas->highlight({}); m_pdfPending = true;
    m_pdfTextStatus->setText(action == PdfTextAction::Find ? tr("Pesquisando…") : tr("Selecionando…"));
    m_serial = m_worker->submit(std::move(request));
}
void PreviewWidget::copyPdfText() {
    if (m_result.kind == PreviewKind::Pdf && !m_pdfPending && !m_result.pdfText.text.isEmpty()) QApplication::clipboard()->setText(m_result.pdfText.text);
}
void PreviewWidget::findText(bool backwards) {
    if (m_find->text().isEmpty()) return;
    const auto flags = backwards ? QTextDocument::FindBackward : QTextDocument::FindFlags{};
    bool found = m_text->find(m_find->text(), flags);
    if (!found) { auto cursor = m_text->textCursor(); cursor.movePosition(backwards ? QTextCursor::End : QTextCursor::Start); m_text->setTextCursor(cursor); found = m_text->find(m_find->text(), flags); }
    m_find->setStyleSheet(found ? QString{} : QString("border: 1px solid #c95248;")); m_find->setToolTip(found ? QString{} : tr("Texto não encontrado na parte carregada."));
}

void PreviewWidget::setSettings(const ViewerSettings &value){
    if(!validateViewerSettings(value).isEmpty())return;
    if(m_settingsApplied&&encodeViewerSettings(value)==encodeViewerSettings(m_settings))return;
    const bool reloadNeeded=m_settings.imageEdge!=value.imageEdge||m_settings.encoding!=value.encoding;
    m_settings=value;m_settingsApplied=true;m_text->setFont(QFont(value.textFont,value.textSize));m_text->setLineWrapMode(value.wrapText?QPlainTextEdit::WidgetWidth:QPlainTextEdit::NoWrap);m_canvas->settings(value.fitImages,value.smoothImages);
    {const QSignalBlocker block(m_encoding);m_encoding->setCurrentIndex(m_encoding->findData(value.encoding));}if(reloadNeeded&&!m_path.isEmpty())load();
}
