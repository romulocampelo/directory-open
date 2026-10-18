#pragma once
#include "renamerecipe.h"
#include <QDialog>
class QListWidget;class QLineEdit;class QComboBox;class QSpinBox;class QCheckBox;class QLabel;
class RenameRecipeDialog final:public QDialog {
    Q_OBJECT
public:
    RenameRecipeDialog(RenameRecipe recipe,const QString &example,QWidget *parent=nullptr);
    RenameRecipe recipe()const;
private:
    RenameRecipe m_recipe;
    QListWidget *m_steps;QLineEdit *m_name,*m_text,*m_replacement,*m_example;QComboBox *m_kind;QSpinBox *m_start,*m_count;QCheckBox *m_case;QLabel *m_result;
    RenameRule rule()const;void reload(int row);void showRule();void preview();
};
