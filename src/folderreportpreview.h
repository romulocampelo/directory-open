#pragma once
#include "foldersize.h"
#include <QDialog>
class QPrinter; class QPrintPreviewWidget;
class FolderReportPreview final : public QDialog {
    Q_OBJECT
public:
    explicit FolderReportPreview(FolderSizeResult result, QWidget *parent = nullptr);
    ~FolderReportPreview() override;
private:
    const FolderSizeResult m_result;
    QPrinter *m_printer;
    QPrintPreviewWidget *m_preview;
};
