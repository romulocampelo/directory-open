#pragma once
#include <QWidget>
#include "previewloader.h"
#include "viewersettings.h"
class ImageCanvas;
class MediaPreview;
class QLabel;
class QPlainTextEdit;
class QLineEdit;
class QComboBox;
class QStackedWidget;
class QSpinBox;
class QAction;
class QPushButton;
class PreviewWidget final : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget *parent = nullptr);
    ~PreviewWidget() override;
    void setPath(const QString &path);
    void reload();
    void releaseMedia();
    void setSettings(const ViewerSettings &settings);
    const PreviewResult &result() const { return m_result; }
signals:
    void navigateFile(int direction);
    void enlargeRequested(bool fullScreen);
    void settingsRequested();
    void ocrRequested();
    void displayed();
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    PreviewWorker *m_worker;
    MediaPreview *m_media = nullptr;
    QString m_path;
    quint64 m_serial = 0;
    PreviewResult m_result;
    ViewerSettings m_settings;
    bool m_settingsApplied=false;
    ImageCanvas *m_canvas;
    QLabel *m_title, *m_details, *m_status, *m_pageCount;
    QPlainTextEdit *m_text;
    QLineEdit *m_find;
    QComboBox *m_encoding;
    QStackedWidget *m_stack;
    QWidget *m_textTools, *m_pdfTools;
    QSpinBox *m_page;
    QLineEdit *m_pdfFind;
    QLabel *m_pdfTextStatus;
    QPushButton *m_pdfCopy;
    int m_pdfMatchIndex = -1;
    QString m_pdfQuery;
    bool m_pdfPending = false;
    QAction *m_zoomIn, *m_zoomOut, *m_actual, *m_fit, *m_rotate;
    QAction *m_ocr;
    void load();
    void showResult(PreviewResult result);
    void findText(bool backwards);
    void requestPdfText(PdfTextAction action, bool backwards = false, QPointF start = {}, QPointF end = {});
    void copyPdfText();
    void showPdfPage();
};
