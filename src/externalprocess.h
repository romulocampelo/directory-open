#pragma once
#include <QByteArray>
#include <QProcessEnvironment>
#include <QStringList>
#include <atomic>
#include <functional>
enum class ExternalState { Exited, Failed, Cancelled, Incomplete };
struct ExternalCommand {
    QString executable; QStringList arguments; QString workingDirectory;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    QByteArray input;
};
struct ExternalResult {
    ExternalState state = ExternalState::Failed;
    int exitCode = -1;
    QByteArray output, errors;
    QString message;
    qint64 elapsedMs = 0;
};
using ExternalConsumer = std::function<bool(const QByteArray &)>;
using ExternalCancellation = std::function<bool()>;
// Owns process-family lifetime and bounded transport; consumers own protocols.
ExternalResult runExternal(const ExternalCommand &, const std::atomic_bool &,
                          const ExternalConsumer &stdoutConsumer = {}, const ExternalConsumer &stderrConsumer = {});
ExternalResult runExternal(const ExternalCommand &, const ExternalCancellation &,
                          const ExternalConsumer &stdoutConsumer = {}, const ExternalConsumer &stderrConsumer = {});
