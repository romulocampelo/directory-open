#include "folderpresentation.h"
#include <QCheckBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileSystemModel>
#include <QFormLayout>
#include <QIntValidator>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

QString validateFolderRules(const FolderRules &r) {
    if(r.patterns.size()>1024 || r.patterns.contains(QChar(0)) || r.patterns.split(';',Qt::SkipEmptyParts).size()>32)
        return "Use até 32 padrões, separados por ponto e vírgula, com até 1.024 caracteres no total.";
    if(!r.files&&!r.folders)return "Selecione arquivos ou pastas.";
    if(r.minimumBytes < -1 || r.maximumBytes < -1 || (r.minimumBytes>=0&&r.maximumBytes>=0&&r.minimumBytes>r.maximumBytes))return "O intervalo de tamanhos é inválido.";
    if(r.modifiedFrom.isValid()&&r.modifiedTo.isValid()&&r.modifiedFrom>r.modifiedTo)return "O intervalo de datas é inválido.";
    return {};
}
std::optional<FolderRules> decodeFolderRules(const QJsonValue &value) {
    if(!value.isObject())return {};const auto o=value.toObject();FolderRules r;
    for(const auto &key:{"patterns","minimumBytes","maximumBytes","modifiedFrom","modifiedTo"})if(o.contains(key)&&!o[key].isString())return {};
    for(const auto &key:{"files","folders","hidden"})if(o.contains(key)&&!o[key].isBool())return {};
    r.patterns=o["patterns"].toString();r.files=o["files"].toBool(true);r.folders=o["folders"].toBool(true);r.hidden=o["hidden"].toBool(true);
    for(const auto &key:{"minimumBytes","maximumBytes"})if(o.contains(key)){bool ok=false;const auto n=o[key].toString().toLongLong(&ok);if(!ok||QString::number(n)!=o[key].toString())return {};if(QString(key)=="minimumBytes")r.minimumBytes=n;else r.maximumBytes=n;}
    for(const auto &key:{"modifiedFrom","modifiedTo"})if(!o[key].toString().isEmpty()){const auto date=QDate::fromString(o[key].toString(),Qt::ISODate);if(!date.isValid())return {};if(QString(key)=="modifiedFrom")r.modifiedFrom=date;else r.modifiedTo=date;}
    if(!validateFolderRules(r).isEmpty())return {};return r;
}
QJsonObject encodeFolderRules(const FolderRules &r) {
    return {{"patterns",r.patterns},{"minimumBytes",QString::number(r.minimumBytes)},{"maximumBytes",QString::number(r.maximumBytes)},
        {"modifiedFrom",r.modifiedFrom.toString(Qt::ISODate)},{"modifiedTo",r.modifiedTo.toString(Qt::ISODate)},{"files",r.files},{"folders",r.folders},{"hidden",r.hidden}};
}
void FolderRuleMatcher::setRules(const FolderRules&r){
    if(m_rules==r)return;m_rules=r;m_patterns.clear();
    for(const auto &p:r.patterns.split(';',Qt::SkipEmptyParts))if(!p.trimmed().isEmpty())m_patterns.append(QRegularExpression(QRegularExpression::wildcardToRegularExpression(p.trimmed()),QRegularExpression::CaseInsensitiveOption));
}
bool FolderRuleMatcher::matches(const QFileInfo &f)const {
    return matches(FolderRuleFacts{f.fileName(), f.isDir(), f.isHidden(), f.size(), f.lastModified().date()});
}
bool FolderRuleMatcher::matches(const FolderRuleFacts &f)const {
    const auto &r=m_rules;
    if((f.directory&&!r.folders)||(!f.directory&&!r.files)||(!r.hidden&&f.hidden))return false;
    // Folder sizes are not file sizes. Size/pattern constraints apply to files.
    if(f.directory)return true;
    if(!m_patterns.isEmpty()){
        bool match=false;for(const auto &p:m_patterns)if(p.match(f.name).hasMatch()){match=true;break;}if(!match)return false;
    }
    if((r.minimumBytes>=0&&f.bytes<r.minimumBytes)||(r.maximumBytes>=0&&f.bytes>r.maximumBytes))return false;
    const auto date=f.modified;
    if((r.modifiedFrom.isValid()&&date<r.modifiedFrom)||(r.modifiedTo.isValid()&&date>r.modifiedTo))return false;
    return true;
}
bool matchesFolderRules(const QFileInfo &f,const FolderRules&r){FolderRuleMatcher matcher;matcher.setRules(r);return matcher.matches(f);}
FileGroup fileGroup(const QFileInfo &f,int mode) {
    if(!mode)return {};if(f.isDir())return {"0","Pastas"};
    if(mode==1){const auto suffix=f.suffix().toCaseFolded();return {"1/"+suffix,suffix.isEmpty()?"Sem extensão":suffix.toUpper()};}
    if(mode==2){const auto date=f.lastModified().date();return {"1/"+date.toString("yyyy-MM"),date.toString("MM/yyyy")};}
    const int bucket=f.size()<1024*1024?0:f.size()<100*1024*1024?1:2;
    return {QString("1/%1").arg(bucket),QStringList{"Menos de 1 MiB","De 1 a 100 MiB","100 MiB ou mais"}[bucket]};
}
FolderFilter::FolderFilter(QObject *p):QSortFilterProxyModel(p){setSortCaseSensitivity(Qt::CaseInsensitive);}
void FolderFilter::update(const QString &root,const QString &text,const FolderRules &rules){
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();m_root=root;m_text=text;m_matcher.setRules(rules);endFilterChange(Direction::Rows);
#else
    m_root=root;m_text=text;m_matcher.setRules(rules);invalidateFilter();
#endif
}
void FolderFilter::setGrouping(int mode){if(m_grouping==mode)return;m_grouping=mode;invalidate();}
bool FolderFilter::filterAcceptsRow(int row,const QModelIndex &parent)const{
    const auto *m=qobject_cast<QFileSystemModel *>(sourceModel());if(m->filePath(parent)!=m_root)return true;
    const auto f=m->fileInfo(m->index(row,0,parent));return f.fileName().contains(m_text,Qt::CaseInsensitive)&&m_matcher.matches(f);
}
bool FolderFilter::lessThan(const QModelIndex&a,const QModelIndex&b)const{
    const auto *m=qobject_cast<QFileSystemModel *>(sourceModel());const auto l=m->fileInfo(a),r=m->fileInfo(b);
    if(m_grouping){const auto lg=fileGroup(l,m_grouping),rg=fileGroup(r,m_grouping);if(lg.key!=rg.key)return lg.key<rg.key;}
    if(l.isDir()!=r.isDir())return l.isDir();
    if(a.column()==1&&l.size()!=r.size())return l.size()<r.size();
    if(a.column()==3&&l.lastModified()!=r.lastModified())return l.lastModified()<r.lastModified();
    if(a.column()==2&&m->type(a)!=m->type(b))return QString::localeAwareCompare(m->type(a),m->type(b))<0;
    return QString::localeAwareCompare(l.fileName().toCaseFolded(),r.fileName().toCaseFolded())<0;
}
QVariant FolderFilter::data(const QModelIndex &index,int role)const{
    if(role==GroupHeaderRole){
        if(!m_grouping||!index.isValid())return {};const auto *m=qobject_cast<QFileSystemModel *>(sourceModel());
        if(m->filePath(mapToSource(index.parent()))!=m_root)return {};
        const auto group=fileGroup(m->fileInfo(mapToSource(index)),m_grouping);
        if(index.row()>0&&fileGroup(m->fileInfo(mapToSource(this->index(index.row()-1,0,index.parent()))),m_grouping).key==group.key)return {};
        return group.label;
    }return QSortFilterProxyModel::data(index,role);
}
void GroupedFileDelegate::paint(QPainter *p,const QStyleOptionViewItem &o,const QModelIndex&i)const{
    auto item=o;const auto label=i.data(GroupHeaderRole).toString();
    if(!label.isEmpty()){
        const int h=headerHeight(o.fontMetrics);QRect band=o.rect;band.setHeight(h);p->save();p->fillRect(band,o.palette.window());
        if(i.column()==0){auto font=o.font;font.setBold(true);p->setFont(font);p->setPen(o.palette.windowText().color());p->drawText(band.adjusted(6,0,-4,0),Qt::AlignVCenter,label);}
        p->restore();item.rect.setTop(item.rect.top()+h);
    }QStyledItemDelegate::paint(p,item,i);
}
QSize GroupedFileDelegate::sizeHint(const QStyleOptionViewItem&o,const QModelIndex&i)const{
    auto size=QStyledItemDelegate::sizeHint(o,i);if(!i.data(GroupHeaderRole).toString().isEmpty())size.rheight()+=headerHeight(o.fontMetrics);return size;
}
std::optional<FolderRules> editFolderRules(const FolderRules &rules,QWidget *parent,const QString &title){
    QDialog dialog(parent);dialog.setObjectName("folderRulesDialog");dialog.setWindowTitle(title.isEmpty()?"Filtro combinado da aba":title);dialog.resize(560,460);
    auto *root=new QVBoxLayout(&dialog);auto *info=new QLabel("Combina com o filtro por nome. Tamanho, padrões e datas valem para arquivos; pastas seguem visíveis se habilitadas.",&dialog);info->setWordWrap(true);root->addWidget(info);
    auto *form=new QFormLayout;root->addLayout(form);auto *patterns=new QLineEdit(rules.patterns,&dialog);patterns->setObjectName("rulePatterns");patterns->setMaxLength(1024);patterns->setPlaceholderText("*.jpg;*.png");form->addRow("Padrões de arquivo",patterns);
    auto number=[&](const QString &label,qint64 value,const QString&id){auto *e=new QLineEdit(&dialog);e->setObjectName(id);e->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]{0,19}"),e));e->setPlaceholderText("Sem limite — 1 MiB = 1048576 bytes");if(value>=0)e->setText(QString::number(value));form->addRow(label,e);return e;};
    auto *minimum=number("Mínimo (bytes)",rules.minimumBytes,"ruleMinimum"),*maximum=number("Máximo (bytes)",rules.maximumBytes,"ruleMaximum");
    auto date=[&](const QString &label,const QDate &value){auto *row=new QWidget(&dialog);auto *layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);auto *on=new QCheckBox(label,row);on->setChecked(value.isValid());auto *edit=new QDateEdit(value.isValid()?value:QDate::currentDate(),row);edit->setCalendarPopup(true);edit->setDisplayFormat("dd/MM/yyyy");edit->setEnabled(on->isChecked());QObject::connect(on,&QCheckBox::toggled,edit,&QWidget::setEnabled);layout->addWidget(on);layout->addWidget(edit);form->addRow(row);return QPair{on,edit};};
    auto from=date("Modificado desde",rules.modifiedFrom),to=date("Modificado até",rules.modifiedTo);
    auto *files=new QCheckBox("Mostrar arquivos",&dialog),*folders=new QCheckBox("Mostrar pastas",&dialog),*hidden=new QCheckBox("Mostrar ocultos",&dialog);files->setChecked(rules.files);folders->setChecked(rules.folders);hidden->setChecked(rules.hidden);for(auto *c:{files,folders,hidden})root->addWidget(c);
    auto *error=new QLabel(&dialog);error->setWordWrap(true);root->addWidget(error);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel|QDialogButtonBox::Reset,&dialog);buttons->button(QDialogButtonBox::Cancel)->setText("Cancelar");buttons->button(QDialogButtonBox::Reset)->setText("Limpar filtro");root->addWidget(buttons);
    std::optional<FolderRules> result;
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QObject::connect(buttons->button(QDialogButtonBox::Reset),&QPushButton::clicked,&dialog,[&]{result=FolderRules{};dialog.accept();});
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        bool minOk=true,maxOk=true;FolderRules r;r.patterns=patterns->text();r.minimumBytes=minimum->text().isEmpty()?-1:minimum->text().toLongLong(&minOk);r.maximumBytes=maximum->text().isEmpty()?-1:maximum->text().toLongLong(&maxOk);
        if(!minOk||!maxOk){error->setText("O tamanho excede o intervalo permitido.");return;}
        r.modifiedFrom=from.first->isChecked()?from.second->date():QDate{};r.modifiedTo=to.first->isChecked()?to.second->date():QDate{};r.files=files->isChecked();r.folders=folders->isChecked();r.hidden=hidden->isChecked();const auto message=validateFolderRules(r);if(!message.isEmpty()){error->setText(message);return;}result=r;dialog.accept();});
    dialog.exec();return result;
}
