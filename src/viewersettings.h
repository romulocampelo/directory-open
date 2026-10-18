#pragma once
#include <QJsonObject>
#include <optional>
class QWidget;
struct ViewerSettings{
    QString textFont="Consolas";
    int textSize=11,encoding=0,imageEdge=4096;
    bool wrapText=false,fitImages=true,smoothImages=true;
    QJsonObject extra;
};
QString validateViewerSettings(const ViewerSettings &value);
std::optional<ViewerSettings> decodeViewerSettings(const QJsonValue &value);
QJsonObject encodeViewerSettings(const ViewerSettings &value);
std::optional<ViewerSettings> editViewerSettings(const ViewerSettings &value,QWidget *parent);
