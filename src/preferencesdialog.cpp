#include "preferencesdialog.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
QLabel *description(const QString &s, QWidget *parent) { auto *l = new QLabel(s, parent); l->setWordWrap(true); return l; }
QCheckBox *check(const QString &s, const QString &id, QWidget *p) { auto *c = new QCheckBox(s, p); c->setObjectName(id); return c; }
void fillList(QListWidget *list, const QVector<QString> &ids, const QVector<UiCommand> &commands) {
    list->clear();
    for (const auto &id : ids) {
        QString label = id == "-" ? QStringLiteral("──────── Separador") : id;
        for (const auto &c : commands) if (c.id == id) label = commandLabel(c);
        auto *item = new QListWidgetItem(label, list); item->setData(Qt::UserRole, id);
    }
}
QVector<QString> listIds(QListWidget *list) { QVector<QString> r; for (int i = 0; i < list->count(); ++i) r.append(list->item(i)->data(Qt::UserRole).toString()); return r; }

}
PreferencesDialog::PreferencesDialog(const UiCustomization &value, const InterfaceOptions &options, const QVector<UiCommand> &commands,
                                   const QVector<QString> &defaultToolbar, QWidget *parent)
    : QDialog(parent), m_value(value), m_commands(commands), m_defaultToolbar(defaultToolbar) {
    setObjectName("preferencesDialog"); setWindowTitle(tr("Directory Open — Preferências")); resize(980, 700);
    auto *root = new QVBoxLayout(this); auto *body = new QHBoxLayout; root->addLayout(body, 1);
    auto *navigation = new QListWidget(this); navigation->setObjectName("preferencesPages"); navigation->setMaximumWidth(220);
    navigation->addItems({tr("Aparência"), tr("Painéis e visualizador"), tr("Barras e menus"), tr("Atalhos de teclado"), tr("Integrações"), tr("Perfis e restauração")}); body->addWidget(navigation);
    auto *pages = new QStackedWidget(this); body->addWidget(pages, 1);
    auto page = [&](const QString &title, const QString &text) {
        auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        auto *w = new QWidget(scroll); auto *layout = new QVBoxLayout(w);
        auto *heading = new QLabel(title, w); QFont f = heading->font(); f.setPointSize(f.pointSize() + 3); f.setBold(true); heading->setFont(f);
        layout->addWidget(heading); layout->addWidget(description(text, w)); scroll->setWidget(w); pages->addWidget(scroll); return layout;
    };
    auto *appearance = page(tr("Aparência"), tr("Escolha as cores, a fonte e o espaçamento. A amostra acompanha suas escolhas; a janela principal muda ao aplicar."));
    auto *form = new QFormLayout; appearance->addLayout(form);
    m_theme = new QComboBox(this); m_theme->setObjectName("uiTheme"); m_theme->addItems({tr("Claro"), tr("Escuro")}); form->addRow(tr("Tema"), m_theme);
    m_font = new QFontComboBox(this); m_font->setObjectName("uiFont"); form->addRow(tr("Fonte"), m_font);
    m_fontSize = new QSpinBox(this); m_fontSize->setObjectName("uiFontSize"); m_fontSize->setRange(8, 24); m_fontSize->setSuffix(tr(" pt")); form->addRow(tr("Tamanho do texto"), m_fontSize);
    m_density = new QComboBox(this); m_density->setObjectName("uiDensity"); m_density->addItems({tr("Compacto"), tr("Confortável"), tr("Espaçoso")}); form->addRow(tr("Espaçamento"), m_density);
    auto color = [&](const QString &title, const QString &id, bool optional) {
        auto *row = new QWidget(this); auto *line = new QLineEdit(row); line->setObjectName(id); line->setMaxLength(7); line->setPlaceholderText(optional ? tr("Cor do tema") : "#397bb7");
        auto *l = new QHBoxLayout(row); l->setContentsMargins(0,0,0,0); l->addWidget(line);
        auto *choose = new QPushButton(tr("Escolher…"), row); l->addWidget(choose);
        connect(choose, &QPushButton::clicked, this, [this, line] { const auto c = QColorDialog::getColor(QColor(line->text()), this, tr("Escolher cor")); if (c.isValid()) line->setText(c.name()); });
        if (optional) { auto *clear = new QPushButton(tr("Usar tema"), row); l->addWidget(clear); connect(clear, &QPushButton::clicked, line, &QLineEdit::clear); }
        form->addRow(title, row); connect(line, &QLineEdit::textChanged, this, &PreferencesDialog::updateSample); return line;
    };
    m_accent = color(tr("Destaque"), "uiAccent", false); m_background = color(tr("Fundo da janela"), "uiBackground", true);
    m_surface = color(tr("Fundo das listas"), "uiSurface", true); m_foreground = color(tr("Texto"), "uiForeground", true);
    m_sample = new QLabel(tr("Directory Open\nPastas • Arquivos • Seleção"), this); m_sample->setObjectName("appearanceSample"); m_sample->setMinimumHeight(90); appearance->addWidget(m_sample); appearance->addStretch();
    connect(m_theme, &QComboBox::currentIndexChanged, this, &PreferencesDialog::updateSample);
    connect(m_font, &QFontComboBox::currentFontChanged, this, &PreferencesDialog::updateSample); connect(m_fontSize, &QSpinBox::valueChanged, this, &PreferencesDialog::updateSample);

    auto *panels = page(tr("Painéis e visualizador"), tr("Organize a área de trabalho. As abas, colunas, filtros e miniaturas continuam disponíveis no menu Exibição de cada painel."));
    m_dual = check(tr("Dois painéis de arquivos"), "uiDual", this); m_tree = check(tr("Mostrar árvore e locais"), "uiTree", this);
    m_preview = check(tr("Mostrar pré-visualização"), "uiPreview", this); m_bottom = check(tr("Pré-visualização abaixo dos arquivos"), "uiPreviewBottom", this);
    m_statusVisible = check(tr("Mostrar barra de status"), "uiStatus", this);
    m_independentTrees=check(tr("Uma árvore por painel (Consome mais CPU)"),"uiIndependentTrees",this);panels->addWidget(m_independentTrees);
    for (auto *c : {m_dual, m_tree, m_preview, m_bottom, m_statusVisible}) panels->addWidget(c);
    auto *presets = new QHBoxLayout; panels->addLayout(presets);
    for (int i = 0; i < 3; ++i) {
        auto *b = new QPushButton(QStringList{tr("Navegação"), tr("Dois painéis"), tr("Revisão de arquivos")}[i], this); presets->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, i] { m_dual->setChecked(i == 1); m_tree->setChecked(true); m_preview->setChecked(i == 2); m_bottom->setChecked(false); });
    } panels->addStretch();

    auto *bars = page(tr("Barras e menus"), tr("Adicione, remova e ordene comandos. Os menus principais e o acesso às preferências permanecem disponíveis."));
    m_toolbarVisible = check(tr("Mostrar barra de comandos"), "uiToolbarVisible", this); m_movable = check(tr("Permitir mover e destacar a barra de comandos"), "uiToolbarMovable", this);
    bars->addWidget(m_toolbarVisible); bars->addWidget(m_movable); auto *barForm = new QFormLayout; bars->addLayout(barForm);
    m_iconSize = new QSpinBox(this); m_iconSize->setObjectName("uiIconSize"); m_iconSize->setRange(16,48); m_iconSize->setSuffix(tr(" px")); barForm->addRow(tr("Tamanho dos ícones"), m_iconSize);
    m_buttonStyle = new QComboBox(this); m_buttonStyle->setObjectName("uiButtonStyle"); m_buttonStyle->addItems({tr("Ícones e texto ao lado"), tr("Somente ícones"), tr("Texto abaixo dos ícones")}); barForm->addRow(tr("Botões"), m_buttonStyle);
    auto *editors = new QTabWidget(this); editors->addTab(commandEditor(m_toolbar, true), tr("Barra de comandos")); editors->addTab(commandEditor(m_menu, false), tr("Menu Meus comandos")); bars->addWidget(editors, 1);

    auto *keys = page(tr("Atalhos de teclado"), tr("Atalhos locais do DirOpen. Conflitos e combinações reservadas são verificados antes de aplicar. Ctrl+, abre preferências; Ctrl+Shift+P abre a central de comandos."));
    auto *keyFilter = new QLineEdit(this); keyFilter->setPlaceholderText(tr("Procurar comando…")); keys->addWidget(keyFilter);
    m_keys = new QTableWidget(this); m_keys->setObjectName("uiShortcuts"); m_keys->setColumnCount(2); m_keys->setHorizontalHeaderLabels({tr("Comando"), tr("Atalho")}); m_keys->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch); m_keys->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents); keys->addWidget(m_keys, 1);
    connect(keyFilter, &QLineEdit::textChanged, this, [this](const QString &s) { for (int r=0;r<m_keys->rowCount();++r) m_keys->setRowHidden(r, !m_keys->item(r,0)->text().contains(s,Qt::CaseInsensitive)); });
    auto *resetKeys = new QPushButton(tr("Restaurar atalhos padrão"), this); keys->addWidget(resetKeys);
    connect(resetKeys, &QPushButton::clicked, this, [this] { for (int r=0;r<m_keys->rowCount();++r) static_cast<QKeySequenceEdit *>(m_keys->cellWidget(r,1))->setKeySequence(QKeySequence(m_keys->item(r,0)->data(Qt::UserRole+1).toString())); });

    auto *integrations = page(tr("Integrações"), tr("A central de comandos reúne as ferramentas disponíveis. Caminhos dos programas e opções de cada tarefa são ajustados nas respectivas telas."));
    m_native = check(tr("Menu de contexto do Windows (Consome mais CPU)"), "uiNativeMenu", this); m_tera = check(tr("Preferir TeraCopy nas cópias"), "uiTeraCopy", this);
    integrations->addWidget(m_native); integrations->addWidget(m_tera);
    integrations->addWidget(description(tr("Everything: busca por nome\n7-Zip: compactados\nrestic: backups com versões\nrclone: armazenamento remoto\nripgrep: busca de conteúdo\nExifTool: metadados\nImageMagick: imagens\nFFmpeg / ffprobe: áudio e vídeo\n\nA escolha de TeraCopy encaminha cópias à janela dele; o resultado continua sendo acompanhado externamente."), this)); integrations->addStretch();
    auto *profiles = page(tr("Perfis e restauração"), tr("Guarde aparência, barra, menu e atalhos em um perfil. O arquivo inclui lançadores e caminhos de ícones configurados, mas não suas pastas abertas, credenciais ou a configuração das ferramentas. Importar nunca executa comandos. Importar preenche as preferências; revise e aplique."));
    auto *import = new QPushButton(tr("Importar perfil…"), this); auto *exportButton = new QPushButton(tr("Exportar perfil…"), this); auto *reset = new QPushButton(tr("Restaurar personalização padrão"), this);
    profiles->addWidget(import); profiles->addWidget(exportButton); profiles->addWidget(reset); profiles->addStretch();
    connect(import, &QPushButton::clicked, this, &PreferencesDialog::importProfile); connect(exportButton, &QPushButton::clicked, this, &PreferencesDialog::exportProfile);
    connect(reset, &QPushButton::clicked, this, [this] { auto o = this->options(); o.dark = false; populate(UiCustomization{}, o); });
    m_error = description({}, this); m_error->setObjectName("preferencesError"); root->addWidget(m_error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this); root->addWidget(buttons);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar")); buttons->button(QDialogButtonBox::Apply)->setText(tr("Aplicar"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { acceptChanges(true); }); connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] { acceptChanges(false); });
    connect(navigation, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex); navigation->setCurrentRow(0); populate(value, options);
}
QWidget *PreferencesDialog::commandEditor(QListWidget *&list, bool toolbar) {
    auto *w = new QWidget(this); auto *l = new QVBoxLayout(w); list = new QListWidget(w); list->setObjectName(toolbar ? "uiToolbarCommands" : "uiMenuCommands"); l->addWidget(list, 1);
    auto *row = new QHBoxLayout; l->addLayout(row); auto *choices = new QComboBox(w); choices->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); choices->setMinimumContentsLength(14);
    choices->addItem(tr("Separador"), "-"); for (const auto &c : m_commands) if (toolbar || !c.toolbarOnly) choices->addItem(c.category+" / "+commandLabel(c), c.id); row->addWidget(choices, 1);
    auto *add = new QPushButton(tr("Adicionar"), w); row->addWidget(add);
    connect(add, &QPushButton::clicked, this, [this, list, choices] {
        if (list->count() >= 128) return; const auto id = choices->currentData().toString(); const auto ids = listIds(list); if (id != "-" && ids.contains(id)) return;
        auto *item = new QListWidgetItem(choices->currentText(), list); item->setData(Qt::UserRole,id); list->setCurrentItem(item);
    });
    auto *controls = new QHBoxLayout; l->addLayout(controls);
    for (int d : {-1,1,0}) { auto *b = new QPushButton(d == -1 ? tr("Subir") : d == 1 ? tr("Descer") : tr("Remover"), w); controls->addWidget(b);
        connect(b, &QPushButton::clicked, this, [list, d] { int r = list->currentRow(); if (r < 0) return; if (!d) { delete list->takeItem(r); return; } if (r+d < 0 || r+d >= list->count()) return; auto *i=list->takeItem(r); list->insertItem(r+d,i); list->setCurrentRow(r+d); }); }
    return w;
}
void PreferencesDialog::populate(const UiCustomization &v, const InterfaceOptions &o) {
    m_value = v; m_theme->setCurrentIndex(o.dark ? 1 : 0); m_font->setCurrentFont(QFont(v.fontFamily)); m_fontSize->setValue(v.fontSize); m_density->setCurrentIndex(v.density);
    m_accent->setText(v.accent); m_background->setText(v.background); m_surface->setText(v.surface); m_foreground->setText(v.foreground);
    m_iconSize->setValue(v.iconSize); m_buttonStyle->setCurrentIndex(v.buttonStyle); m_toolbarVisible->setChecked(v.toolbarVisible); m_statusVisible->setChecked(v.statusVisible); m_movable->setChecked(v.toolbarMovable);
    m_dual->setChecked(o.dual); m_tree->setChecked(o.tree); m_preview->setChecked(o.preview); m_bottom->setChecked(o.previewBottom); m_native->setChecked(o.nativeMenu); m_tera->setChecked(o.teraCopy);
    m_independentTrees->setChecked(o.independentTrees);
    fillList(m_toolbar, v.toolbar.value_or(m_defaultToolbar), m_commands); fillList(m_menu, v.menu.value_or(QVector<QString>{}), m_commands);
    m_keys->setRowCount(0);
    for (const auto &c : m_commands) if (!c.toolbarOnly) {
        int r = m_keys->rowCount(); m_keys->insertRow(r); auto *item = new QTableWidgetItem(commandLabel(c)); item->setFlags(item->flags() & ~Qt::ItemIsEditable); item->setData(Qt::UserRole,c.id); item->setData(Qt::UserRole+1,c.defaultKey); m_keys->setItem(r,0,item);
        auto *edit = new QKeySequenceEdit(QKeySequence(v.shortcuts.value(c.id,c.defaultKey)), m_keys); m_keys->setCellWidget(r,1,edit);
        edit->setClearButtonEnabled(true);
        if(auto *line=edit->findChild<QLineEdit *>())line->setPlaceholderText(tr("Pressione as teclas"));
        if(c.id=="preferences"||c.id=="commandCenter"||c.id=="exitApplication")edit->setEnabled(false);
    } updateSample();
}
UiCustomization PreferencesDialog::customization() const {
    auto v=m_value; v.fontFamily=m_font->currentFont().family(); v.fontSize=m_fontSize->value(); v.density=m_density->currentIndex(); v.accent=m_accent->text(); v.background=m_background->text(); v.surface=m_surface->text(); v.foreground=m_foreground->text();
    v.iconSize=m_iconSize->value(); v.buttonStyle=m_buttonStyle->currentIndex(); v.toolbarVisible=m_toolbarVisible->isChecked(); v.statusVisible=m_statusVisible->isChecked(); v.toolbarMovable=m_movable->isChecked();
    const auto toolbar = listIds(m_toolbar); if (toolbar == m_defaultToolbar) v.toolbar.reset(); else v.toolbar=toolbar;
    v.menu=listIds(m_menu);
    for (int r=0;r<m_keys->rowCount();++r) {
        auto *item=m_keys->item(r,0); const auto key=static_cast<QKeySequenceEdit *>(m_keys->cellWidget(r,1))->keySequence().toString(QKeySequence::PortableText);
        if (key==item->data(Qt::UserRole+1).toString()) v.shortcuts.remove(item->data(Qt::UserRole).toString()); else v.shortcuts[item->data(Qt::UserRole).toString()]=key;
    } return v;
}
InterfaceOptions PreferencesDialog::options() const { return {m_theme->currentIndex()==1,m_dual->isChecked(),m_tree->isChecked(),m_preview->isChecked(),m_bottom->isChecked(),m_native->isChecked(),m_tera->isChecked(),m_independentTrees->isChecked()}; }
void PreferencesDialog::updateSample() {
    // Called during population; controls after the color section may not yet be populated.
    if (!m_sample) return;
    UiCustomization v; v.accent=m_accent->text(); v.background=m_background->text(); v.surface=m_surface->text(); v.foreground=m_foreground->text();
    if (!validateUiCustomization(v).isEmpty()) return;
    const auto palette=uiPalette(v,m_theme->currentIndex()==1,QPalette{});
    m_sample->setStyleSheet(QString("background:%1;color:%2;border:3px solid %3;padding:12px;").arg(palette.color(QPalette::Base).name(),palette.color(QPalette::Text).name(),v.accent)); m_sample->setFont(QFont(m_font->currentFont().family(),m_fontSize->value()));
}
void PreferencesDialog::acceptChanges(bool close) {
    const auto v=customization(); auto error=validateUiCustomization(v); if (error.isEmpty()) error=validateUiShortcuts(v,m_commands);
    m_error->setText(error); if (!error.isEmpty()) return; emit applyRequested(); if (close) accept();
}
void PreferencesDialog::importProfile() {
    const auto path=QFileDialog::getOpenFileName(this,tr("Importar perfil"),{},tr("Perfil DirOpen (*.json)")); if(path.isEmpty())return;
    const auto profile=readUiProfile(path);auto error=profile.error;
    if (error.isEmpty()) error=validateUiShortcuts(profile.value,m_commands);
    if (!error.isEmpty()) { m_error->setText(error); return; }
    auto opts=options();opts.dark=profile.dark;populate(profile.value,opts);m_error->setText(tr("Perfil importado. Revise e aplique para usar."));
}
void PreferencesDialog::exportProfile() {
    const auto v=customization(); auto error=validateUiCustomization(v); if(error.isEmpty())error=validateUiShortcuts(v,m_commands); if(!error.isEmpty()){m_error->setText(error);return;}
    const auto path=QFileDialog::getSaveFileName(this,tr("Exportar perfil"),"DirOpen-interface.json",tr("Perfil DirOpen (*.json)"));if(path.isEmpty())return;
    error=saveUiProfile(path,v,options().dark);m_error->setText(error.isEmpty()?tr("Perfil exportado. Se houve substituição, o anterior foi preservado em .bak."):error);
}
CommandsDialog::CommandsDialog(const QVector<UiCommand> &commands, QWidget *parent) : QDialog(parent) {
    setObjectName("commandsDialog"); setWindowTitle(tr("Directory Open — Central de comandos")); resize(780,620); auto *layout=new QVBoxLayout(this);
    layout->addWidget(description(tr("Encontre uma tela ou ação. Comandos indisponíveis precisam de seleção ou do término da tarefa atual."),this));
    auto *search=new QLineEdit(this); search->setObjectName("commandSearch"); search->setPlaceholderText(tr("Procurar: imagens, backup, buscar, favoritos…")); layout->addWidget(search);
    auto *list=new QListWidget(this); list->setObjectName("commandList"); layout->addWidget(list,1);
    for(const auto &c:commands) if(!c.toolbarOnly){
        auto *i=new QListWidgetItem(c.action->icon(),c.category+"  /  "+commandLabel(c)+(c.action->shortcut().isEmpty()?QString{}:"    "+c.action->shortcut().toString(QKeySequence::NativeText)),list);
        i->setData(Qt::UserRole,c.id);
        auto update=[i,a=c.action]{auto flags=i->flags();if(a->isEnabled())flags|=Qt::ItemIsEnabled;else flags&=~Qt::ItemIsEnabled;i->setFlags(flags);};
        update();connect(c.action,&QAction::changed,this,update);
    }
    connect(search,&QLineEdit::textChanged,this,[list](const QString&s){for(int r=0;r<list->count();++r)list->item(r)->setHidden(!list->item(r)->text().contains(s,Qt::CaseInsensitive));});
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Open|QDialogButtonBox::Cancel,this); layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Open)->setText(tr("Abrir"));buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    auto choose=[this,list,commands]{auto *i=list->currentItem();if(!i||i->isHidden()||!(i->flags()&Qt::ItemIsEnabled))return;for(const auto &c:commands)if(c.id==i->data(Qt::UserRole).toString()&&c.action->isEnabled()){m_selected=c.action;accept();return;}};
    connect(buttons,&QDialogButtonBox::accepted,this,choose);connect(list,&QListWidget::itemDoubleClicked,this,[choose](QListWidgetItem*){choose();});connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);search->setFocus();
}
