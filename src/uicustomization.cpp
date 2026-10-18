#include "uicustomization.h"
#include <QAction>
#include <QJsonArray>
#include <QKeySequence>
#include <QRegularExpression>
#include <QSet>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <memory>

namespace {
bool hexColor(const QString &s) { static const QRegularExpression re("^#[0-9a-fA-F]{6}$"); return re.match(s).hasMatch(); }
bool safeText(const QString &s, int max) { return s.size() <= max && !s.contains(QChar(0)); }
bool conflicts(const QKeySequence &a, const QKeySequence &b) {
    return !a.isEmpty() && !b.isEmpty() && (a.matches(b) != QKeySequence::NoMatch || b.matches(a) != QKeySequence::NoMatch);
}
struct Colors { QString background, surface, foreground, border, selected, selectedText; };
Colors colors(const UiCustomization &v, bool dark) {
    Colors c;
    c.background = v.background.isEmpty() ? (dark ? "#20252c" : "#f3f5f7") : v.background;
    c.surface = v.surface.isEmpty() ? (dark ? "#181d23" : "#ffffff") : v.surface;
    c.foreground = v.foreground.isEmpty() ? (dark ? "#dce3ed" : "#243043") : v.foreground;
    c.border = dark ? "#465263" : "#ccd6e1";
    c.selected = QColor(v.accent).lighter(dark ? 110 : 180).name();
    c.selectedText = QColor(c.selected).lightness() < 135 ? "#ffffff" : "#162435";
    return c;
}
}
QString validateUiCustomization(const UiCustomization &v) {
    if (v.fontFamily.trimmed().isEmpty() || !safeText(v.fontFamily, 128) || v.fontSize < 8 || v.fontSize > 24
        || v.density < 0 || v.density > 2 || v.iconSize < 16 || v.iconSize > 48 || v.buttonStyle < 0 || v.buttonStyle > 2)
        return "Fonte, espaçamento ou tamanho de ícones inválido.";
    if (!hexColor(v.accent)) return "Escolha uma cor de destaque válida.";
    for (const auto &s : {v.background, v.surface, v.foreground}) if (!s.isEmpty() && !hexColor(s)) return "Cor personalizada inválida.";
    for (const auto &items : {v.toolbar, v.menu}) if (items) {
        if (items->size() > 128) return "A barra ou menu admite até 128 itens.";
        QSet<QString> used;
        for (const auto &s : *items) {
            if (s.isEmpty() || !safeText(s, 80) || (s != "-" && used.contains(s))) return "A barra contém um comando inválido ou repetido.";
            if (s != "-") used.insert(s);
        }
    }
    if (v.shortcuts.size() > 128) return "Há atalhos demais nesta configuração.";
    for (auto i = v.shortcuts.begin(); i != v.shortcuts.end(); ++i) {
        if (i.key().isEmpty() || !safeText(i.key(), 80) || !safeText(i.value(), 128)) return "Atalho inválido.";
        const auto key = QKeySequence::fromString(i.value(), QKeySequence::PortableText);
        if (!i.value().isEmpty() && (key.isEmpty() || key.toString(QKeySequence::PortableText) != i.value())) return "A combinação de teclas não é válida.";
    }
    const auto viewerError=validateViewerSettings(v.viewer);if(!viewerError.isEmpty())return viewerError;
    return validateAdvancedInterface(v.advanced);
}
std::optional<UiCustomization> decodeUiCustomization(const QJsonValue &value) {
    if (!value.isObject()) return {}; UiCustomization v; const auto o = value.toObject(); v.extra = o;
    for (const auto &key : {"fontFamily","fontSize","density","iconSize","buttonStyle","accent","background","surface","foreground","toolbarVisible","statusVisible","toolbarMovable","toolbar","menu","shortcuts","advanced","viewer"}) v.extra.remove(key);
    if(o.contains("viewer")){const auto parsed=decodeViewerSettings(o["viewer"]);if(!parsed)return {};v.viewer=*parsed;}
    if(o.contains("advanced")){const auto parsed=decodeAdvancedInterface(o["advanced"]);if(!parsed)return {};v.advanced=*parsed;}
    for (const auto &key : {"fontFamily", "accent", "background", "surface", "foreground"}) if (o.contains(key) && !o[key].isString()) return {};
    for (const auto &key : {"fontSize", "density", "iconSize", "buttonStyle"})
        if (o.contains(key) && (!o[key].isDouble() || o[key].toDouble() != o[key].toInt(-1))) return {};
    for (const auto &key : {"toolbarVisible", "statusVisible", "toolbarMovable"}) if (o.contains(key) && !o[key].isBool()) return {};
    v.fontFamily = o["fontFamily"].toString(v.fontFamily); v.fontSize = o["fontSize"].toInt(v.fontSize);
    v.density = o["density"].toInt(v.density); v.iconSize = o["iconSize"].toInt(v.iconSize); v.buttonStyle = o["buttonStyle"].toInt(v.buttonStyle);
    v.accent = o["accent"].toString(v.accent); v.background = o["background"].toString(); v.surface = o["surface"].toString(); v.foreground = o["foreground"].toString();
    v.toolbarVisible = o["toolbarVisible"].toBool(true); v.statusVisible = o["statusVisible"].toBool(true); v.toolbarMovable = o["toolbarMovable"].toBool(false);
    for (const auto &name : {"toolbar", "menu"}) if (o.contains(name)) {
        if (!o[name].isArray()) return {}; QVector<QString> items;
        for (const auto &entry : o[name].toArray()) { if (!entry.isString()) return {}; items.append(entry.toString()); }
        if (QString(name) == "toolbar") v.toolbar = items; else v.menu = items;
    }
    if (o.contains("shortcuts")) {
        if (!o["shortcuts"].isObject()) return {}; const auto keys = o["shortcuts"].toObject();
        for (auto i = keys.begin(); i != keys.end(); ++i) { if (!i.value().isString()) return {}; v.shortcuts.insert(i.key(), i.value().toString()); }
    }
    if (!validateUiCustomization(v).isEmpty()) return {}; return v;
}
QJsonObject encodeUiCustomization(const UiCustomization &v) {
    auto o = v.extra;
    o["fontFamily"] = v.fontFamily; o["fontSize"] = v.fontSize; o["density"] = v.density; o["iconSize"] = v.iconSize; o["buttonStyle"] = v.buttonStyle;
    o["accent"] = v.accent; o["background"] = v.background; o["surface"] = v.surface; o["foreground"] = v.foreground;
    o["toolbarVisible"] = v.toolbarVisible; o["statusVisible"] = v.statusVisible; o["toolbarMovable"] = v.toolbarMovable;
    if (v.toolbar) { QJsonArray a; for (const auto &s : *v.toolbar) a.append(s); o["toolbar"] = a; } else o.remove("toolbar");
    if (v.menu) { QJsonArray a; for (const auto &s : *v.menu) a.append(s); o["menu"] = a; } else o.remove("menu");
    QJsonObject keys; for (auto i = v.shortcuts.begin(); i != v.shortcuts.end(); ++i) keys[i.key()] = i.value(); o["shortcuts"] = keys; o["advanced"]=encodeAdvancedInterface(v.advanced);o["viewer"]=encodeViewerSettings(v.viewer);return o;
}
QString commandLabel(const UiCommand &c) {
    auto s = c.action->text(); s.remove('&');
    if (c.id=="copyFiles" || c.id=="moveFiles") s += " para o outro painel";
    return s;
}
QString validateUiShortcuts(const UiCustomization &v, const QVector<UiCommand> &commands) {
    QVector<QPair<QKeySequence, QString>> keys;
    // User definitions in the candidate own their existence, including imports
    // whose actions have not yet been constructed in the window.
    QVector<UiCommand> staged;QVector<std::shared_ptr<QAction>> launchers;
    for (const auto &c : commands) {
        if(!c.id.startsWith("user."))staged.append(c);
    }
    for(const auto &launcher:v.advanced.launchers){auto action=std::make_shared<QAction>(launcher.name);staged.append({"user."+launcher.id,"Comandos próprios",action.get(),{},false});launchers.append(action);}
    for (const auto &c : staged) {
        if (c.toolbarOnly) continue;
        if ((c.id == "preferences" || c.id == "commandCenter" || c.id == "exitApplication") && v.shortcuts.value(c.id,c.defaultKey) != c.defaultKey)
            return "Os atalhos de preferências, central de comandos e fechar janela são fixos.";
        const auto key = QKeySequence::fromString(v.shortcuts.value(c.id, c.defaultKey), QKeySequence::PortableText);
        // Preserve text editing and the recovery routes. Existing clipboard commands keep their keys.
        const QStringList reserved{"Ctrl+,", "Ctrl+Shift+P", "Alt+F4", "Ctrl+A", "Ctrl+Z", "Ctrl+Y", "Ctrl+C", "Ctrl+X", "Ctrl+V"};
        for (const auto &r : reserved) if (conflicts(key, QKeySequence(r)) && c.defaultKey != r)
            return QString("O atalho %1 está reservado para edição ou acesso à interface.").arg(key.toString(QKeySequence::NativeText));
        for (const auto &previous : keys) if (conflicts(key, previous.first)) return QString("Atalho em conflito entre %1 e %2.").arg(commandLabel(c), previous.second);
        if (!key.isEmpty()) keys.append({key, commandLabel(c)});
    }
    return {};
}
QString uiStyleSheet(const UiCustomization &v, bool dark) {
    static const bool resources=[](){Q_INIT_RESOURCE(interface_icons);return true;}();Q_UNUSED(resources);
    const auto c = colors(v,dark);
    const int pad = 2 + v.density * 2;
    auto sheet=QString(R"(
        QWidget { background: %1; color: %3; }
        QMainWindow, QDialog, QMessageBox, QMenuBar, QToolBar, QStatusBar, QWidget#sidebar { background: %1; color: %3; }
        QMenuBar { padding: 3px 6px; border-bottom: 1px solid %4; }
        QMenuBar::item { padding: 5px 10px; } QMenuBar::item:selected { background: %5; color: %8; }
        QMenu { background: %2; color: %3; border: 1px solid %4; padding: 4px; }
        QMenu::item { padding: 6px 22px; } QMenu::item:selected { background: %5; color: %8; }
        QMenu::item:disabled, QToolButton:disabled { color: #85909e; }
        QToolBar { spacing: 3px; padding: %7px; border-bottom: 1px solid %4; }
        QToolBar::separator { background: %4; width: 1px; margin: 4px 7px; }
        QToolButton { border: 1px solid transparent; padding: %7px 6px; border-radius: 4px; }
        QToolButton:hover, QToolButton:checked { background: %5; color: %8; border-color: %4; }
        QPushButton, QComboBox { background: %2; color: %3; border: 1px solid %4; border-radius: 4px; padding: 6px 10px; }
        QPushButton:hover { border-color: %6; } QPushButton:default { border: 2px solid %6; }
        QPushButton:disabled, QComboBox:disabled, QLineEdit:disabled, QKeySequenceEdit:disabled { color: #85909e; }
        QToolBar#panelNavigation[active='true'] { border-bottom: 2px solid %6; }
        QLineEdit, QSpinBox, QKeySequenceEdit { background: %2; color: %3; border: 1px solid %4; border-radius: 3px; padding: 4px 6px; selection-background-color: %5; selection-color: %8; }
        QAbstractItemView, QPlainTextEdit, QTextEdit { background: %2; color: %3; border: 1px solid %4; selection-background-color: %5; selection-color: %8; }
        QAbstractItemView::item:disabled { color: #85909e; }
        QTreeView::item, QListWidget::item { padding: %7px 3px; }
        QHeaderView::section { background: %1; color: %3; border: 0; border-right: 1px solid %4; border-bottom: 1px solid %4; padding: 5px 7px; }
        QLabel#sectionTitle { background: %1; color: %3; border-bottom: 1px solid %4; padding: 5px; font-weight: 600; }
        QSplitter::handle { background: %4; width: 3px; }
        QTabBar::tab { background: %1; color: %3; border: 1px solid %4; padding: 5px 12px; min-width: 55px; }
        QTabBar::tab:selected { background: %5; color: %8; border-bottom: 2px solid %6; }
        QWidget#panelFooter { background: %1; border-top: 1px solid %4; }
        QStatusBar { border-top: 1px solid %4; padding: 3px; }
        QDockWidget::title { background: %1; color: %3; padding: 6px; }
    )").arg(c.background, c.surface, c.foreground, c.border, c.selected, v.accent).arg(pad).arg(c.selectedText);
    const QString tick=QColor(v.accent).lightness()<135?"light":"dark";
    sheet+=QString(R"(
        QCheckBox::indicator, QTreeView::indicator, QListView::indicator { width: 14px; height: 14px; border: 1px solid %1; border-radius: 2px; background: %2; }
        QCheckBox::indicator:checked, QCheckBox::indicator:indeterminate, QTreeView::indicator:checked, QTreeView::indicator:indeterminate, QListView::indicator:checked, QListView::indicator:indeterminate { background: %3; border-color: %3; }
        QCheckBox::indicator:checked, QTreeView::indicator:checked, QListView::indicator:checked { image: url(:/interface/check-%4.svg); }
        QCheckBox::indicator:indeterminate, QTreeView::indicator:indeterminate, QListView::indicator:indeterminate { image: url(:/interface/dash-%4.svg); }
        QCheckBox::indicator:disabled, QTreeView::indicator:disabled, QListView::indicator:disabled { border-color: #85909e; }
        QCheckBox::indicator:checked:disabled, QTreeView::indicator:checked:disabled, QListView::indicator:checked:disabled { background: #85909e; }
    )").arg(dark?"#718299":"#8c98a7",c.surface,v.accent,tick);return sheet;
}
QPalette uiPalette(const UiCustomization &v, bool dark, QPalette palette) {
    const auto c=colors(v,dark);
    palette.setColor(QPalette::Window,QColor(c.background));palette.setColor(QPalette::WindowText,QColor(c.foreground));palette.setColor(QPalette::Text,QColor(c.foreground));
    palette.setColor(QPalette::Base,QColor(c.surface));palette.setColor(QPalette::AlternateBase,QColor(c.background));palette.setColor(QPalette::Button,QColor(c.background));palette.setColor(QPalette::ButtonText,QColor(c.foreground));
    palette.setColor(QPalette::ToolTipBase,QColor(c.surface));palette.setColor(QPalette::ToolTipText,QColor(c.foreground));palette.setColor(QPalette::Highlight,QColor(c.selected));palette.setColor(QPalette::HighlightedText,QColor(c.selectedText));
    for (const auto role : {QPalette::WindowText,QPalette::Text,QPalette::ButtonText}) palette.setColor(QPalette::Disabled,role,QColor("#85909e"));
    return palette;
}
UiProfileResult readUiProfile(const QString &path) {
    UiProfileResult result; QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { result.error=file.errorString(); return result; }
    const auto bytes=file.read(65537);
    if (bytes.size()>65536 || file.error()!=QFileDevice::NoError) { result.error="O perfil não pôde ser lido ou excede 64 KiB."; return result; }
    QJsonParseError parseError; const auto doc=QJsonDocument::fromJson(bytes,&parseError); const auto o=doc.object(); const auto v=decodeUiCustomization(o["interface"]);
    if (parseError.error!=QJsonParseError::NoError || !doc.isObject() || o["format"]!="DirectoryOpen.Interface" || o["version"]!=1 || !o["dark"].isBool() || !v) { result.error="Perfil inválido ou de versão incompatível."; return result; }
    result.value=*v;result.dark=o["dark"].toBool();return result;
}
QString saveUiProfile(const QString &path, const UiCustomization &value, bool dark) {
    const auto validation=validateUiCustomization(value);if(!validation.isEmpty())return validation;
    const auto bytes=QJsonDocument(QJsonObject{{"format","DirectoryOpen.Interface"},{"version",1},{"dark",dark},{"interface",encodeUiCustomization(value)}}).toJson();
    if(bytes.size()>65536)return "O perfil excede 64 KiB.";
    QLockFile lock(path+".lock");if(!lock.tryLock(0))return "Outro processo está gravando este perfil.";
    for(const auto &p:{path,path+".bak"}){const QFileInfo info(p);if(info.isSymLink()||(info.exists()&&!info.isFile()))return "O perfil ou backup não é um arquivo normal.";}
    auto write=[](const QString &p,const QByteArray &b)->QString{QSaveFile file(p);file.setDirectWriteFallback(false);if(!file.open(QIODevice::WriteOnly))return file.errorString();if(file.write(b)!=b.size())return file.errorString();if(!file.commit())return file.errorString();return {};};
    QFile old(path);
    if(old.exists()){
        if(!old.open(QIODevice::ReadOnly)||old.size()>65536)return "O perfil anterior não pôde ser preservado.";
        const auto previous=old.read(65537);if(previous.size()>65536)return "O perfil anterior excede 64 KiB.";
        if(old.error()!=QFileDevice::NoError)return old.errorString();old.close();if(previous==bytes)return {};
        const auto error=write(path+".bak",previous);if(!error.isEmpty())return error;
    }
    return write(path,bytes);
}
