#ifdef DIROPEN_PRESENTATION_PROBE
#include "../tests/presentationprobe.h"
#endif
#include "foldersizedialog.h"
#include "folderreport.h"
#include "folderreportpreview.h"
#include <QCheckBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

FolderSizeDialog::FolderSizeDialog(FolderSizeRequest request, QWidget *parent) : QDialog(parent), m_request(std::move(request)) {
    setObjectName("folderSizeDialog"); setWindowTitle(tr("Calcular tamanho de pastas")); resize(820, 610);
    setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(this);
    auto *policy = new QLabel(tr("Tamanho lógico dos arquivos por caminho: hard links contam a cada nome. "
        "Raízes sobrepostas são unificadas. Links/reparse points não são seguidos.\n"
        "Leitura ao longo do tempo, sem snapshot: alterações podem escapar à detecção. "
        "Não mede espaço ocupado em disco, ADS ou conteúdo dos links.\n"
        "Limites: 256 raízes, 10 milhões de entradas, 256 níveis e 10 minutos. Cancelamento aguarda a chamada de disco em curso."), this);
    policy->setWordWrap(true); policy->setTextFormat(Qt::PlainText); layout->addWidget(policy);
    m_listing = new QCheckBox(tr("Coletar listagem de arquivos/pastas no próximo cálculo (usa mais memória)"), this);
    m_listing->setObjectName("collectFolderListing"); m_listing->setChecked(m_request.collectListing);
    m_listing->setToolTip(tr("Até 10.000 linhas / 8 MiB contabilizados. Atingir a retenção não interrompe o cálculo. Pastas sem subtotal individual."));
    layout->addWidget(m_listing);
    m_details = new QPlainTextEdit(this); m_details->setObjectName("folderSizeDetails"); m_details->setReadOnly(true);
    m_details->setPlainText(tr("Pastas selecionadas:\n") + m_request.roots.join('\n')); layout->addWidget(m_details, 1);
    m_progress = new QProgressBar(this); m_progress->setObjectName("folderSizeProgress"); m_progress->setRange(0, 1); m_progress->setValue(0); layout->addWidget(m_progress);
    m_progress->setTextVisible(false); m_progress->hide();
    m_status = new QLabel(tr("Pronto. Clique em Calcular para ler as pastas selecionadas."), this);
    m_status->setObjectName("folderSizeStatus"); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
    auto *buttons = new QHBoxLayout;
    m_start = new QPushButton(tr("Calcular"), this); m_start->setObjectName("startFolderSize");
    m_cancel = new QPushButton(tr("Cancelar cálculo"), this); m_cancel->setObjectName("cancelFolderSize"); m_cancel->setEnabled(false);
    auto *close = new QPushButton(tr("Fechar"), this);
    buttons->addWidget(m_start); buttons->addWidget(m_cancel); buttons->addStretch(); buttons->addWidget(close); layout->addLayout(buttons);
    auto *reports = new QHBoxLayout;
    m_csv = new QPushButton(tr("Exportar CSV…"), this); m_csv->setObjectName("exportFolderCsv");
    m_text = new QPushButton(tr("Exportar TXT…"), this); m_text->setObjectName("exportFolderText");
    m_print = new QPushButton(tr("Prévia / imprimir…"), this); m_print->setObjectName("previewFolderReport");
    for (auto *button : {m_csv, m_text, m_print}) { button->setEnabled(false); reports->addWidget(button); }
    layout->addLayout(reports);
    connect(m_csv, &QPushButton::clicked, this, [this] { exportReport(true); });
    connect(m_text, &QPushButton::clicked, this, [this] { exportReport(false); });
    connect(m_print, &QPushButton::clicked, this, [this] {
        if (!m_job || m_job->isRunning()) return;
        FolderReportPreview preview(m_job->result(), this); preview.exec();
    });
    m_poll = new QTimer(this); m_poll->setInterval(150);
    connect(m_poll, &QTimer::timeout, this, &FolderSizeDialog::render);
    connect(m_start, &QPushButton::clicked, this, &FolderSizeDialog::start);
    connect(m_cancel, &QPushButton::clicked, this, [this] { if (m_job) { m_job->cancel(); m_cancel->setEnabled(false); m_status->setText(tr("Cancelamento solicitado; aguardando a leitura em curso…")); } });
    connect(close, &QPushButton::clicked, this, &FolderSizeDialog::reject);
}
FolderSizeDialog::~FolderSizeDialog() { if (m_job) { m_job->cancel(); m_job->wait(); } }
void FolderSizeDialog::reject() {
    if (m_job && m_job->isRunning()) { m_closePending = true; m_job->cancel(); m_cancel->setEnabled(false); return; }
    QDialog::reject();
}
void FolderSizeDialog::start() {
    if (m_job && m_job->isRunning()) return;
    m_request.collectListing = m_listing->isChecked();
    delete m_job; m_job = new FolderSizeJob(m_request, this);
    m_listing->setEnabled(false);
    for (auto *button : {m_csv, m_text, m_print}) button->setEnabled(false);
    m_start->setEnabled(false); m_cancel->setEnabled(true); m_progress->setRange(0, 0); m_progress->show();
    m_status->setText(tr("Calculando… subtotal provisório; cobertura ainda desconhecida."));
    connect(m_job, &QThread::finished, this, [this] {
        m_job->wait(); m_poll->stop(); render(); m_progress->hide();
        m_start->setEnabled(true); m_cancel->setEnabled(false);
        m_listing->setEnabled(true);
        for (auto *button : {m_csv, m_text, m_print}) button->setEnabled(true);
        if (m_closePending) QDialog::reject();
    });
    m_poll->start(); m_job->start();
}
void FolderSizeDialog::render() {
    if (!m_job) return;
    const auto r = [this] {
#ifdef DIROPEN_PRESENTATION_PROBE
        presentationProbe::Scope query(presentationProbe::Query);
#endif
        return m_job->result();
    }();
    const QLocale locale;
    const auto status = [&] {
#ifdef DIROPEN_PRESENTATION_PROBE
        presentationProbe::Scope format(presentationProbe::Format);
#endif
        return folderSizeStateText(r.state) + tr("\n%1 bytes · Arquivos: %2 · Pastas: %3 · Entradas examinadas: %4\n"
            "Erros: %5 · Links omitidos: %6 · Raízes sobrepostas omitidas: %7 · Alterações detectadas: %8\n%9 s decorridos")
            .arg(locale.toString(r.bytes), locale.toString(r.files), locale.toString(r.folders), locale.toString(r.examined),
                 locale.toString(r.errors), locale.toString(r.skippedLinks), locale.toString(r.overlaps), locale.toString(r.changes), locale.toString(r.elapsedMs / 1000.0, 'f', 2))
            + tr("\nListagem: %1 · %2 linhas retidas / %3 registros observados")
                .arg(folderListingStateText(r.listingState), locale.toString(r.listing.size()), locale.toString(r.listingObserved));
    }();
    const auto details = [&] {
#ifdef DIROPEN_PRESENTATION_PROBE
        presentationProbe::Scope format(presentationProbe::Format);
#endif
        QString text = tr("Seleção solicitada:\n") + r.request.roots.join('\n') + tr("\n\nRaízes efetivas (união):\n") + r.roots.join('\n');
        if (r.started.isValid()) text += tr("\n\nInício: ") + r.started.toLocalTime().toString(Qt::ISODate);
        if (r.finished.isValid()) text += tr("\nFim: ") + r.finished.toLocalTime().toString(Qt::ISODate);
        if (!r.currentPath.isEmpty()) text += tr("\nLendo: ") + r.currentPath;
        if (!r.limitReason.isEmpty()) text += '\n' + r.limitReason;
        if (!r.listingLimitReason.isEmpty()) text += '\n' + r.listingLimitReason;
        if (r.request.collectListing) text += tr("\nRetenção da listagem: até %1 linhas / %2 bytes contabilizados. Pastas sem subtotal individual.")
            .arg(r.request.maxListingEntries).arg(r.request.maxListingBytes);
        if (!r.details.isEmpty()) text += tr("\n\nErros (primeiros 20; contagem total acima):\n") + r.details.join('\n');
        return text;
    }();
    {
#ifdef DIROPEN_PRESENTATION_PROBE
        presentationProbe::Scope widgets(presentationProbe::Widgets);
#endif
        // Publish the complete label once; an intermediate label has no consumer.
        m_status->setText(status);
    }
    {
#ifdef DIROPEN_PRESENTATION_PROBE
        presentationProbe::Scope widgets(presentationProbe::Widgets);
#endif
        // The document already owns the displayed text. Preserve selection/scroll on identical delivery.
        if (m_details->toPlainText() != details) m_details->setPlainText(details);
    }
}
void FolderSizeDialog::exportReport(bool csv) {
    if (!m_job || m_job->isRunning()) return;
    const auto stable = m_job->result();
    const auto path = QFileDialog::getSaveFileName(this, tr("Criar relatório em arquivo novo"),
        csv ? "relatorio.csv" : "relatorio.txt", csv ? "CSV (*.csv)" : "Texto (*.txt)", nullptr, QFileDialog::DontConfirmOverwrite);
    if (path.isEmpty()) return;
    QString error;
    if (!writeFolderReport(stable, path, csv ? FolderReportFormat::Csv : FolderReportFormat::Text, &error))
        QMessageBox::warning(this, tr("Relatório não gravado"), tr("Escolha um nome novo. ") + error);
    else QMessageBox::information(this, tr("Relatório criado"), QDir::toNativeSeparators(path));
}
