#include "folderreportpreview.h"
#include "folderreport.h"
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPrintDialog>
#include <QPrinter>
#include <QPrintPreviewWidget>
#include <QPushButton>
#include <QVBoxLayout>

FolderReportPreview::FolderReportPreview(FolderSizeResult result, QWidget *parent)
    : QDialog(parent), m_result(std::move(result)) {
    setObjectName("folderReportPreview"); setWindowTitle(tr("Prévia do relatório — resultado concluído")); resize(1020,780);
    auto *layout=new QVBoxLayout(this);
    auto *coverage=new QLabel(folderReportCoverage(m_result),this); coverage->setObjectName("reportCoverage");
    coverage->setTextFormat(Qt::PlainText); coverage->setWordWrap(true); layout->addWidget(coverage);
    QFont font=coverage->font();font.setBold(true);coverage->setFont(font);
    auto *printer=m_printer=new QPrinter(QPrinter::HighResolution);
    // Preview has its own PDF metrics; no dependency on the default physical driver's DPI.
    printer->setOutputFormat(QPrinter::PdfFormat); printer->setResolution(144);
    printer->setPageSize(QPageSize(QPageSize::A4)); printer->setPageMargins(QMarginsF(15,15,15,15));
    auto *preview=m_preview=new QPrintPreviewWidget(printer,this);preview->setObjectName("reportPages"); layout->addWidget(preview,1);
    auto *status=new QLabel(this);status->setObjectName("reportPrintStatus");status->setWordWrap(true);status->setTextFormat(Qt::PlainText);layout->addWidget(status);
    connect(preview,&QPrintPreviewWidget::paintRequested,this,[this,status](QPrinter *p){
        QString error; const bool ok=paintFolderReport(m_result,*p,&error); status->setText(ok?tr("Prévia paginada. PDF e impressão usam estes mesmos fatos; sem reler o disco."):error);
    });
    auto *buttons=new QHBoxLayout;
    auto *previous=new QPushButton(tr("Anterior"),this); auto *next=new QPushButton(tr("Próxima"),this);
    auto *fit=new QPushButton(tr("Ajustar página"),this); auto *zoom=new QPushButton(tr("Ampliar"),this);
    auto *pdf=new QPushButton(tr("Salvar PDF novo…"),this);pdf->setObjectName("saveFolderPdf");
    auto *print=new QPushButton(tr("Imprimir…"),this);print->setObjectName("printFolderReport");
    auto *close=new QPushButton(tr("Fechar"),this);close->setObjectName("closeFolderPreview");
    for(auto *button:{previous,next,fit,zoom,pdf,print,close})buttons->addWidget(button);
    layout->addLayout(buttons);
    connect(previous,&QPushButton::clicked,this,[preview]{preview->setCurrentPage(qMax(1,preview->currentPage()-1));});
    connect(next,&QPushButton::clicked,this,[preview]{preview->setCurrentPage(qMin(preview->pageCount(),preview->currentPage()+1));});
    connect(fit,&QPushButton::clicked,preview,&QPrintPreviewWidget::fitInView);
    connect(zoom,&QPushButton::clicked,this,[preview]{preview->zoomIn();});
    connect(close,&QPushButton::clicked,this,&QDialog::reject);
    connect(pdf,&QPushButton::clicked,this,[this]{
        const auto path=QFileDialog::getSaveFileName(this,tr("Criar PDF em arquivo novo"),"relatorio.pdf","PDF (*.pdf)",nullptr,QFileDialog::DontConfirmOverwrite);
        if(path.isEmpty())return; QString error;
        if(!writeFolderReportPdf(m_result,path,&error))QMessageBox::warning(this,tr("PDF não gravado"),tr("Escolha um nome novo. ")+error);
        else QMessageBox::information(this,tr("PDF criado"),path);
    });
    connect(print,&QPushButton::clicked,this,[this,status]{
        QPrinter destination(QPrinter::HighResolution); destination.setPageLayout(m_printer->pageLayout());
        QPrintDialog dialog(&destination,this);dialog.setOption(QAbstractPrintDialog::PrintToFile,false);
        dialog.setOption(QAbstractPrintDialog::PrintPageRange,false);dialog.setOption(QAbstractPrintDialog::PrintSelection,false);
        if(dialog.exec()!=QDialog::Accepted)return;
        if(destination.outputFormat()!=QPrinter::NativeFormat || !destination.outputFileName().isEmpty()){
            status->setText(tr("Para arquivo, use Salvar PDF novo (criação exclusiva)."));return;
        }
        QString error; if(!paintFolderReport(m_result,destination,&error))QMessageBox::warning(this,tr("Falha de impressão"),error);
    });
}
FolderReportPreview::~FolderReportPreview() { delete m_preview; delete m_printer; }
