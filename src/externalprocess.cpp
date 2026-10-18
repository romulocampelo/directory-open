#include "externalprocess.h"
#include <QElapsedTimer>
#include <QProcess>
#include <QScopeGuard>
#include <QThread>
#include <windows.h>

ExternalResult runExternal(const ExternalCommand &c, const ExternalCancellation &cancel, const ExternalConsumer &out, const ExternalConsumer &err) {
    ExternalResult r; QElapsedTimer timer; timer.start();
    const auto elapsed = qScopeGuard([&] { r.elapsedMs = timer.elapsed(); });
    if (cancel()) { r.state = ExternalState::Cancelled; r.message = "Cancelado antes de iniciar."; return r; }
    const auto job = CreateJobObjectW(nullptr, nullptr);
    if (!job) { r.message = "Não foi possível reservar a supervisão do processo."; return r; }
    const auto closeJob = qScopeGuard([&] { CloseHandle(job); });
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) { r.message = "Falha ao configurar a supervisão."; return r; }
    SIZE_T size = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    QByteArray storage(qsizetype(size), Qt::Uninitialized);
    auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &size)) { r.message = "Supervisão indisponível."; return r; }
    const auto freeAttributes = qScopeGuard([&] { DeleteProcThreadAttributeList(attributes); });
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, const_cast<HANDLE *>(&job), sizeof(job), nullptr, nullptr)) { r.message = "Não foi possível vincular a família de processos."; return r; }
    STARTUPINFOEXW startup{}; startup.lpAttributeList = attributes;
    QProcess p;
    p.setCreateProcessArgumentsModifier([&](QProcess::CreateProcessArguments *a) {
        startup.StartupInfo = *a->startupInfo; startup.StartupInfo.cb = sizeof(startup);
        a->startupInfo = &startup.StartupInfo; a->flags |= EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW;
    });
    p.setProcessEnvironment(c.environment); if (!c.workingDirectory.isEmpty()) p.setWorkingDirectory(c.workingDirectory);
    p.start(c.executable, c.arguments);
    if (!p.waitForStarted(10000)) { r.message = "Não foi possível iniciar a ferramenta: " + p.errorString(); return r; }
    if (c.input.size() > 1024*1024 || (!c.input.isEmpty() && p.write(c.input) != c.input.size())) { TerminateJobObject(job,1);p.waitForFinished(-1);r.message="Não foi possível fornecer os parâmetros à ferramenta.";return r; }
    p.closeWriteChannel();
    bool limited = false;
    auto drain = [&] {
        p.setReadChannel(QProcess::StandardOutput);
        while (p.bytesAvailable() && !limited && !cancel()) {
            const auto chunk = p.read(65536);
            if (out) { if (!out(chunk)) limited = true; }
            else if (r.output.size() + chunk.size() <= 8 * 1024 * 1024) r.output += chunk;
            else limited = true;
        }
        p.setReadChannel(QProcess::StandardError);
        while (p.bytesAvailable() && !cancel()) {
            const auto chunk = p.read(65536); r.errors += chunk;
            if (r.errors.size() > 65536) r.errors = r.errors.last(65536);
            if (err && !err(chunk)) limited = true;
        }
        p.setReadChannel(QProcess::StandardOutput);
    };
    while (p.state() != QProcess::NotRunning) {
        p.waitForReadyRead(50); drain();
        if (cancel() || limited) { p.terminate(); if (!p.waitForFinished(500)) { TerminateJobObject(job, 1); p.waitForFinished(-1); } break; }
    }
    drain(); JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{}; QElapsedTimer reap; reap.start();
    bool supervisionFailed = false;
    do {
        if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr)) { supervisionFailed = true; break; }
        if (accounting.ActiveProcesses) QThread::msleep(10);
    } while (accounting.ActiveProcesses && reap.elapsed() < 500);
    if (accounting.ActiveProcesses) {
        TerminateJobObject(job, 1); supervisionFailed = true;
        do { QThread::msleep(10); if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr)) break; } while (accounting.ActiveProcesses);
    }
    r.elapsedMs = timer.elapsed(); r.exitCode = p.exitCode();
    if (cancel()) { r.state = ExternalState::Cancelled; r.message = "Cancelado. Itens já concluídos foram preservados."; }
    else if (limited || supervisionFailed) { r.state = ExternalState::Incomplete; r.message = limited ? "Saída excedeu o limite ou a resposta foi recusada pelo adaptador." : "Um auxiliar permaneceu ativo e foi encerrado; conclusão incompleta."; }
    else if (p.exitStatus() != QProcess::NormalExit) { r.message = "A ferramenta encerrou inesperadamente."; }
    else r.state = ExternalState::Exited;
    return r;
}

ExternalResult runExternal(const ExternalCommand &command,const std::atomic_bool &cancel,const ExternalConsumer &out,const ExternalConsumer &err){
    return runExternal(command,ExternalCancellation([&cancel]{return cancel.load();}),out,err);
}
