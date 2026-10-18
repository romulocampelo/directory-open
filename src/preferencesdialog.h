#pragma once
#include "uicustomization.h"
#include <QDialog>
class QCheckBox;
class QComboBox;
class QFontComboBox;
class QSpinBox;
class QLineEdit;
class QListWidget;
class QTableWidget;
class QLabel;
struct InterfaceOptions {
    bool dark = false, dual = true, tree = true, preview = false, previewBottom = false;
    bool nativeMenu = true, teraCopy = false;
    bool independentTrees = false;
};
class PreferencesDialog final : public QDialog {
    Q_OBJECT
public:
    PreferencesDialog(const UiCustomization &value, const InterfaceOptions &options,
                      const QVector<UiCommand> &commands, const QVector<QString> &defaultToolbar, QWidget *parent);
    UiCustomization customization() const;
    InterfaceOptions options() const;
signals:
    void applyRequested();
private:
    UiCustomization m_value;
    QVector<UiCommand> m_commands;
    QVector<QString> m_defaultToolbar;
    QFontComboBox *m_font;
    QSpinBox *m_fontSize, *m_iconSize;
    QComboBox *m_theme, *m_density, *m_buttonStyle;
    QLineEdit *m_accent, *m_background, *m_surface, *m_foreground;
    QCheckBox *m_dual, *m_tree, *m_preview, *m_bottom, *m_toolbarVisible, *m_statusVisible, *m_movable, *m_native, *m_tera;
    QCheckBox *m_independentTrees;
    QListWidget *m_toolbar, *m_menu;
    QTableWidget *m_keys;
    QLabel *m_error = nullptr, *m_sample = nullptr;
    void populate(const UiCustomization &value, const InterfaceOptions &options);
    void acceptChanges(bool close);
    QWidget *commandEditor(QListWidget *&list, bool toolbar);
    void updateSample();
    void importProfile();
    void exportProfile();
};
class CommandsDialog final : public QDialog {
    Q_OBJECT
public:
    CommandsDialog(const QVector<UiCommand> &commands, QWidget *parent);
    QAction *selectedCommand() const { return m_selected; }
private:
    QAction *m_selected = nullptr;
};
