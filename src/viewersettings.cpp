#include "viewersettings.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
QString validateViewerSettings(const ViewerSettings &v){
    if(v.textFont.trimmed().isEmpty()||v.textFont.size()>128||v.textFont.contains(QChar(0))||v.textSize<8||v.textSize>32||v.encoding<0||v.encoding>5||!QList<int>{1536,2048,4096}.contains(v.imageEdge))return "Preferências do visualizador inválidas.";return {};
}
QJsonObject encodeViewerSettings(const ViewerSettings &v){auto o=v.extra;o["textFont"]=v.textFont;o["textSize"]=v.textSize;o["encoding"]=v.encoding;o["imageEdge"]=v.imageEdge;o["wrapText"]=v.wrapText;o["fitImages"]=v.fitImages;o["smoothImages"]=v.smoothImages;return o;}
std::optional<ViewerSettings> decodeViewerSettings(const QJsonValue &value){
    if(!value.isObject())return {};const auto o=value.toObject();ViewerSettings v;v.extra=o;
    for(const auto &key:{"textFont","textSize","encoding","imageEdge","wrapText","fitImages","smoothImages"})v.extra.remove(key);
    if(o.contains("textFont")&&!o["textFont"].isString())return {};v.textFont=o["textFont"].toString(v.textFont);
    for(const auto &key:{"textSize","encoding","imageEdge"})if(o.contains(key)&&(!o[key].isDouble()||o[key].toDouble()!=o[key].toInt(-1)))return {};
    v.textSize=o["textSize"].toInt(v.textSize);v.encoding=o["encoding"].toInt(v.encoding);v.imageEdge=o["imageEdge"].toInt(v.imageEdge);
    for(const auto &key:{"wrapText","fitImages","smoothImages"})if(o.contains(key)&&!o[key].isBool())return {};
    v.wrapText=o["wrapText"].toBool(v.wrapText);v.fitImages=o["fitImages"].toBool(v.fitImages);v.smoothImages=o["smoothImages"].toBool(v.smoothImages);
    if(!validateViewerSettings(v).isEmpty())return {};return v;
}
std::optional<ViewerSettings> editViewerSettings(const ViewerSettings &value,QWidget *parent){
    QDialog dialog(parent);dialog.setObjectName("viewerSettingsDialog");dialog.setWindowTitle(QObject::tr("Preferências do visualizador"));dialog.resize(660,460);auto *root=new QVBoxLayout(&dialog);auto *hint=new QLabel(QObject::tr("As opções valem para a prévia e o visualizador ampliado. Zoom, rotação e quebra de linhas só mudam a exibição; os arquivos permanecem intactos. Texto continua limitado à prévia de 1 MiB."),&dialog);hint->setWordWrap(true);root->addWidget(hint);auto *form=new QFormLayout;root->addLayout(form);
    auto *font=new QFontComboBox(&dialog);font->setObjectName("viewerTextFont");font->setCurrentFont(QFont(value.textFont));form->addRow(QObject::tr("Fonte do texto"),font);auto *size=new QSpinBox(&dialog);size->setObjectName("viewerTextSize");size->setRange(8,32);size->setValue(value.textSize);form->addRow(QObject::tr("Tamanho do texto"),size);
    auto *encoding=new QComboBox(&dialog);encoding->setObjectName("viewerDefaultEncoding");encoding->addItems({QObject::tr("Automática"),"UTF-8","UTF-16 LE","UTF-16 BE","Windows-1252","ISO-8859-1"});encoding->setCurrentIndex(value.encoding);form->addRow(QObject::tr("Codificação inicial"),encoding);
    auto *quality=new QComboBox(&dialog);quality->setObjectName("viewerImageQuality");quality->addItem(QObject::tr("Econômica — até 1536 pixels"),1536);quality->addItem(QObject::tr("Intermediária — até 2048 pixels"),2048);quality->addItem(QObject::tr("Maior detalhe — até 4096 pixels (Consome mais CPU)"),4096);quality->setCurrentIndex(quality->findData(value.imageEdge));form->addRow(QObject::tr("Prévia de imagens"),quality);
    auto *wrap=new QCheckBox(QObject::tr("Quebrar linhas longas de texto"),&dialog),*fit=new QCheckBox(QObject::tr("Ajustar imagens ao espaço disponível ao abrir"),&dialog),*smooth=new QCheckBox(QObject::tr("Suavizar imagem ao ampliar/reduzir (Consome mais CPU)"),&dialog);wrap->setObjectName("viewerWrapText");fit->setObjectName("viewerFitImages");smooth->setObjectName("viewerSmoothImages");wrap->setChecked(value.wrapText);fit->setChecked(value.fitImages);smooth->setChecked(value.smoothImages);for(auto *c:{wrap,fit,smooth})root->addWidget(c);root->addStretch();auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);buttons->button(QDialogButtonBox::Cancel)->setText(QObject::tr("Cancelar"));root->addWidget(buttons);QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return {};auto v=value;v.textFont=font->currentFont().family();v.textSize=size->value();v.encoding=encoding->currentIndex();v.imageEdge=quality->currentData().toInt();v.wrapText=wrap->isChecked();v.fitImages=fit->isChecked();v.smoothImages=smooth->isChecked();return v;
}
