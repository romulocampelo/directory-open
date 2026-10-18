#pragma once
#include "advancedinterface.h"
#include "viewersettings.h"
#include <QColor>
#include <QJsonObject>
#include <QMap>
#include <QPalette>
#include <QVector>
#include <optional>
class QAction;

// Owns the portable, bounded representation. Contains no folders or tool credentials.
struct UiCustomization {
    QString fontFamily = "Segoe UI";
    int fontSize = 9, density = 1, iconSize = 18, buttonStyle = 0;
    QString accent = "#397bb7", background, surface, foreground;
    bool toolbarVisible = true, statusVisible = true, toolbarMovable = false;
    std::optional<QVector<QString>> toolbar;
    std::optional<QVector<QString>> menu;
    QMap<QString, QString> shortcuts;
    AdvancedInterface advanced;
    ViewerSettings viewer;
    QJsonObject extra;
};
struct UiCommand { QString id, category; QAction *action = nullptr; QString defaultKey; bool toolbarOnly = false; };
QString validateUiCustomization(const UiCustomization &value);
std::optional<UiCustomization> decodeUiCustomization(const QJsonValue &value);
QJsonObject encodeUiCustomization(const UiCustomization &value);
QString validateUiShortcuts(const UiCustomization &value, const QVector<UiCommand> &commands);
QString commandLabel(const UiCommand &command);
QString uiStyleSheet(const UiCustomization &value, bool dark);
QPalette uiPalette(const UiCustomization &value, bool dark, QPalette base);
struct UiProfileResult { UiCustomization value; bool dark = false; QString error; };
UiProfileResult readUiProfile(const QString &path);
QString saveUiProfile(const QString &path, const UiCustomization &value, bool dark);
