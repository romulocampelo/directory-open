#pragma once
#include "foldersize.h"
class QPagedPaintDevice;
enum class FolderReportFormat { Csv, Text };
// Reads only the supplied terminal value. Never enumerates or refreshes source paths.
bool writeFolderReport(const FolderSizeResult &, const QString &newPath, FolderReportFormat, QString *error);
bool writeFolderReportPdf(const FolderSizeResult &, const QString &newPath, QString *error);
bool paintFolderReport(const FolderSizeResult &, QPagedPaintDevice &, QString *error);
QString folderReportCoverage(const FolderSizeResult &);
