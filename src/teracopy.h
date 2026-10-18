#pragma once
#include "fileoperation.h"

// Public CLI adapter. TeraCopy owns progress, verification, cancellation and results.
// It may forward to an existing instance; neither a PID nor exit status is a receipt.
QString teraCopyExecutable();
QString teraCopyRecordsPath();
OperationResult dispatchTeraCopy(const OperationRequest &request, const std::atomic_bool &cancel,
                                const OperationProgress &progress = {}, const QString &recordsPath = {});
