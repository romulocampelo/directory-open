#pragma once
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <QVector>
#include <optional>
struct NamedToolbar { QString id,name;QStringList commands;bool visible=true,movable=true; };
struct ComponentAppearance { QString background,foreground;int fontSize=0; };
struct UserLauncher { QString id,name,program;QStringList arguments; };
struct AdvancedInterface {
    QVector<NamedToolbar> toolbars;
    QMap<QString,QStringList> menus,contexts;
    QMap<QString,QString> icons,globalKeys;
    QMap<QString,ComponentAppearance> components;
    QVector<UserLauncher> launchers;
    QString languageFile;
    bool animations=false;
    QJsonObject extra;
};
QStringList interfaceComponentIds();
QStringList interfaceMenuIds();
QString validateAdvancedInterface(const AdvancedInterface &value);
std::optional<AdvancedInterface> decodeAdvancedInterface(const QJsonValue &value);
QJsonObject encodeAdvancedInterface(const AdvancedInterface &value);
QString componentStyleSheet(const AdvancedInterface &value);
