#pragma once
#include <QWidget>
#include "previewloader.h"
class ImageCanvas;
class QLabel;
class QPlainTextEdit;
class QLineEdit;
class QComboBox;
class QStackedWidget;
class QSpinBox;
class QAction;
class PreviewWidget final : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget *parent = nullptr);
    ~PreviewWidget() override;
    void setPath(const QString &path);
    void reload();
    const PreviewResult &result() const { return m_result; }
signals:
    void navigateFile(int direction);
    void enlargeRequested(bool fullScreen);
    void displayed();
private:
    PreviewWorker *m_worker;
    QString m_path;
    quint64 m_serial = 0;
    PreviewResult m_result;
    ImageCanvas *m_canvas;
    QLabel *m_title, *m_details, *m_status, *m_pageCount;
    QPlainTextEdit *m_text;
    QLineEdit *m_find;
    QComboBox *m_encoding;
    QStackedWidget *m_stack;
    QWidget *m_textTools, *m_pdfTools;
    QSpinBox *m_page;
    QAction *m_zoomIn, *m_zoomOut, *m_actual, *m_fit, *m_rotate;
    void load();
    void showResult(PreviewResult result);
    void findText(bool backwards);
};
