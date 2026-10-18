#pragma once
#include "folderview.h"
#include "folderpresentation.h"
#include <QByteArray>
#include <QJsonObject>
#include <optional>
struct TabSession { QString path; QString filter; bool locked=false; FolderRules rules; };
struct PanelSession {
    QVector<TabSession> tabs;
    int currentTab=0,sortColumn=0;
    bool descending=false;
    std::optional<FolderView> view;
    QByteArray treeSplitter;
};
std::optional<PanelSession> decodePanelSession(const QJsonValue &value);
QJsonObject encodePanelSession(const PanelSession &panel);
