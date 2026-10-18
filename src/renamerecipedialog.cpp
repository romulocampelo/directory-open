#include "renamerecipedialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
namespace {const QStringList labels{"Acrescentar prefixo","Acrescentar sufixo","Substituir texto literal","Recortar caracteres","Minúsculas","MAIÚSCULAS","Retirar espaços nas pontas"};}
RenameRecipeDialog::RenameRecipeDialog(RenameRecipe recipe,const QString &example,QWidget *parent):QDialog(parent),m_recipe(std::move(recipe)){
    setObjectName("renameRecipeDialog");setWindowTitle(tr("Receita de renomeação"));resize(850,700);auto *layout=new QVBoxLayout(this);auto *hint=new QLabel(tr("Até 16 etapas, na ordem exibida. Atuam no nome antes do modelo e depois da substituição literal. A extensão preservada é acrescentada ao final. Importar ou guardar não renomeia arquivos. Confira a prévia completa do lote antes de aplicar."),this);hint->setWordWrap(true);layout->addWidget(hint);auto *form=new QFormLayout;layout->addLayout(form);
    auto edit=[this](const char *name,const QString &value,int length){auto *e=new QLineEdit(value,this);e->setObjectName(name);e->setMaxLength(length);return e;};m_name=edit("recipeName",m_recipe.name,128);form->addRow(tr("Nome da receita:"),m_name);
    m_steps=new QListWidget(this);m_steps->setObjectName("recipeSteps");layout->addWidget(m_steps,1);connect(m_steps,&QListWidget::currentRowChanged,this,&RenameRecipeDialog::showRule);
    auto *editor=new QFormLayout;layout->addLayout(editor);m_kind=new QComboBox(this);m_kind->setObjectName("recipeKind");m_kind->addItems(labels);editor->addRow(tr("Etapa:"),m_kind);m_text=edit("recipeText",{},255);m_replacement=edit("recipeReplacement",{},255);editor->addRow(tr("Texto / procurar:"),m_text);editor->addRow(tr("Substituir por:"),m_replacement);
    auto *slice=new QHBoxLayout;m_start=new QSpinBox(this);m_start->setObjectName("recipeStart");m_start->setRange(1,255);m_count=new QSpinBox(this);m_count->setObjectName("recipeCount");m_count->setRange(0,255);m_count->setSpecialValueText(tr("Até o fim"));slice->addWidget(new QLabel(tr("A partir do caractere"),this));slice->addWidget(m_start);slice->addWidget(new QLabel(tr("Quantidade"),this));slice->addWidget(m_count);editor->addRow(tr("Recorte:"),slice);m_case=new QCheckBox(tr("Distinguir maiúsculas na substituição"),this);m_case->setChecked(true);editor->addRow(m_case);
    auto *commands=new QHBoxLayout;layout->addLayout(commands);auto button=[&](const char *id,const QString &text,const auto &callback){auto *b=new QPushButton(text,this);b->setObjectName(id);b->setAutoDefault(false);commands->addWidget(b);connect(b,&QPushButton::clicked,this,callback);return b;};
    button("addRecipeStep",tr("Acrescentar"),[this]{auto rules=m_recipe.rules;rules.append(rule());const auto error=validateRenameRules(rules);if(!error.isEmpty()){m_result->setText(error);return;}m_recipe.rules=rules;reload(rules.size()-1);});
    button("updateRecipeStep",tr("Atualizar etapa"),[this]{const int row=m_steps->currentRow();if(row<0)return;auto rules=m_recipe.rules;rules[row]=rule();const auto error=validateRenameRules(rules);if(!error.isEmpty()){m_result->setText(error);return;}m_recipe.rules=rules;reload(row);});
    button("removeRecipeStep",tr("Remover"),[this]{const int row=m_steps->currentRow();if(row<0)return;m_recipe.rules.removeAt(row);reload(qMin(row,int(m_recipe.rules.size())-1));});
    button("recipeStepUp",tr("Subir"),[this]{const int row=m_steps->currentRow();if(row>0){m_recipe.rules.move(row,row-1);reload(row-1);}});button("recipeStepDown",tr("Descer"),[this]{const int row=m_steps->currentRow();if(row>=0&&row+1<m_recipe.rules.size()){m_recipe.rules.move(row,row+1);reload(row+1);}});
    auto *sample=new QFormLayout;layout->addLayout(sample);m_example=edit("recipeExample",example,255);sample->addRow(tr("Nome de exemplo (sem extensão):"),m_example);m_result=new QLabel(this);m_result->setObjectName("recipeResult");m_result->setTextFormat(Qt::PlainText);m_result->setWordWrap(true);sample->addRow(tr("Resultado das etapas:"),m_result);connect(m_example,&QLineEdit::textChanged,this,&RenameRecipeDialog::preview);
    auto *bottom=new QHBoxLayout;layout->addLayout(bottom);auto *load=new QPushButton(tr("Importar receita…"),this),*save=new QPushButton(tr("Guardar receita…"),this);bottom->addWidget(load);bottom->addWidget(save);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);bottom->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);connect(buttons,&QDialogButtonBox::accepted,this,[this]{if(this->recipe().name.trimmed().isEmpty()){m_result->setText(tr("Informe o nome da receita."));return;}accept();});
    connect(load,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,tr("Importar receita"),{},"Receita DirOpen (*.json)");if(path.isEmpty())return;const auto value=readRenameRecipe(path);if(!value.error.isEmpty()){m_result->setText(value.error);return;}m_recipe=value.recipe;m_name->setText(m_recipe.name);reload(0);});connect(save,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getSaveFileName(this,tr("Guardar receita"),m_name->text()+".json","Receita DirOpen (*.json)");if(path.isEmpty())return;const auto error=saveRenameRecipe(path,this->recipe());m_result->setText(error.isEmpty()?tr("Receita guardada. Nenhum arquivo renomeado."):error);});
    const auto updateKind=[this](int kind){m_text->setEnabled(kind<=2);m_replacement->setEnabled(kind==2);m_case->setEnabled(kind==2);m_start->setEnabled(kind==3);m_count->setEnabled(kind==3);};connect(m_kind,&QComboBox::currentIndexChanged,this,updateKind);reload(m_recipe.rules.isEmpty()?-1:0);updateKind(m_kind->currentIndex());
}
RenameRecipe RenameRecipeDialog::recipe()const{auto value=m_recipe;value.name=m_name->text();return value;}
RenameRule RenameRecipeDialog::rule()const{return {RenameRuleKind(m_kind->currentIndex()),m_text->text(),m_replacement->text(),m_start->value(),m_count->value(),m_case->isChecked()};}
void RenameRecipeDialog::reload(int row){{const QSignalBlocker blocker(m_steps);m_steps->clear();for(int i=0;i<m_recipe.rules.size();++i){const auto &r=m_recipe.rules[i];m_steps->addItem(QString::number(i+1)+". "+labels.value(int(r.kind))+(r.text.isEmpty()?QString{}:" — "+r.text));}m_steps->setCurrentRow(row);}showRule();preview();}
void RenameRecipeDialog::showRule(){const int row=m_steps->currentRow();if(row<0)return;const auto &r=m_recipe.rules[row];m_kind->setCurrentIndex(int(r.kind));m_text->setText(r.text);m_replacement->setText(r.replacement);m_start->setValue(r.start);m_count->setValue(r.count);m_case->setChecked(r.caseSensitive);}
void RenameRecipeDialog::preview(){const auto result=applyRenameRules(m_example->text(),m_recipe.rules);m_result->setText(result.error.isEmpty()?result.text:result.error);}
