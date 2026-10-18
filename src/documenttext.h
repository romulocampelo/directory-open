#pragma once
#include <QByteArray>
#include <QString>
#include <functional>
struct DocumentText{QString text,message;bool complete=false,cancelled=false;};
bool supportsDocumentText(const QString &suffix);
// Reads only bounded XML parts from a private snapshot via 7z stdout. No macros,
// linked resources, office automation, destination extraction or file execution.
DocumentText extractDocumentText(const QByteArray &snapshot,const QString &suffix,const std::function<bool()> &cancelled);
