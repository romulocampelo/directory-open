#include "interfacecommands.h"
#include <QAction>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMenu>
#include <QPixmap>
#include <QRegularExpression>
EditableMenu contextMenuDefinition(const QString &id){
    if(id=="tree")return {id,QObject::tr("Contexto: árvore"),{"treeOpen","treeTab","treeExpand","-","treeCopy","treeCut","treePaste","treeRename","treeTrash","treeCreate","treePath"},
        {{"treeOpen",QObject::tr("Abrir no painel")},{"treeTab",QObject::tr("Abrir em nova aba")},{"treeExpand",QObject::tr("Expandir/recolher")},{"treeCopy",QObject::tr("Copiar")},{"treeCut",QObject::tr("Recortar")},{"treePaste",QObject::tr("Colar nesta pasta")},{"treeRename",QObject::tr("Renomear")},{"treeTrash",QObject::tr("Lixeira")},{"treeCreate",QObject::tr("Nova pasta")},{"treePath",QObject::tr("Copiar caminho")}},true};
    if(id=="tabs")return {id,QObject::tr("Contexto: abas"),{"tabDuplicate","tabLock","tabClose"},{{"tabDuplicate",QObject::tr("Duplicar")},{"tabLock",QObject::tr("Bloquear/desbloquear")},{"tabClose",QObject::tr("Fechar")}},true};
    return {"files",QObject::tr("Contexto: arquivos"),{"open","openFolderInTab","-","clipboardCopy","clipboardCut","pasteFiles","copyFiles","moveFiles","-","renameFile","batchRename","trashFiles","-","createArchive","browseArchive","-","createFolder","copyPath","refresh"},{},true};
}
void arrangeContextMenu(QMenu &menu,const QStringList &order,const QHash<QString,QAction*> &commands){
    for(auto *a:menu.actions()){menu.removeAction(a);if(a->isSeparator())delete a;}
    for(const auto &id:order){if(id=="-")menu.addSeparator();else if(auto *a=commands.value(id))menu.addAction(a);}
}
LauncherPlan planLauncher(const UserLauncher &launcher,const QString &folder,const QStringList &selection){
    LauncherPlan plan;AdvancedInterface config;config.launchers={launcher};plan.error=validateAdvancedInterface(config);if(!plan.error.isEmpty())return plan;
    const QFileInfo exe(launcher.program),cwd(folder);
    if(!exe.isFile()||exe.suffix().compare("exe",Qt::CaseInsensitive)!=0||!cwd.isDir()){plan.error=QObject::tr("O programa ou a pasta de trabalho não está disponível.");return plan;}
    if(selection.size()>256){plan.error=QObject::tr("Um lançador admite até 256 itens por chamada.");return plan;}
    plan.program=exe.absoluteFilePath();plan.workingDirectory=cwd.absoluteFilePath();
    for(const auto &argument:launcher.arguments){
        if(argument=="{folder}")plan.arguments.append(QDir::toNativeSeparators(plan.workingDirectory));
        else if(argument=="{selection}"){if(selection.isEmpty()){plan.error=QObject::tr("Selecione os arquivos antes de executar este comando.");return plan;}for(const auto &path:selection)plan.arguments.append(QDir::toNativeSeparators(path));}
        else if(argument=="{file}"){if(selection.size()!=1){plan.error=QObject::tr("Este comando exige exatamente um item selecionado.");return plan;}plan.arguments.append(QDir::toNativeSeparators(selection.first()));}
        else {static const QRegularExpression token("\\{(?:file|folder|selection)\\}");if(token.match(argument).hasMatch()){plan.error=QObject::tr("Use os campos de substituição como argumentos inteiros.");return plan;}plan.arguments.append(argument);}
    }
    // Conservative allowance for Windows quoting expansion (backslashes/quotes).
    qsizetype units=plan.program.size()*2+4;for(const auto &a:plan.arguments)units+=a.size()*2+4;
    if(units>30000)plan.error=QObject::tr("Os argumentos excedem o limite desta chamada.");return plan;
}
QIcon readCommandIcon(const QString &path,QString *error){
    auto fail=[&](const QString &message){if(error)*error=message;return QIcon{};};
    const QFileInfo info(path);const auto suffix=info.suffix().toLower();
    if(!QStringList{"png","ico","jpg","jpeg"}.contains(suffix)||!info.isFile()||info.size()>8*1024*1024)return fail(QObject::tr("Ícone inválido ou maior que 8 MiB."));
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return fail(file.errorString());auto bytes=file.read(8*1024*1024+1);if(bytes.size()>8*1024*1024||file.error()!=QFileDevice::NoError)return fail(QObject::tr("Não foi possível ler o ícone."));
    QBuffer buffer(&bytes);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer);reader.setDecideFormatFromContent(true);const auto format=reader.format().toLower();
    if(!QList<QByteArray>{"png","ico","jpeg"}.contains(format))return fail(QObject::tr("Formato de ícone não permitido."));
    const auto size=reader.size();if(!size.isValid()||size.width()>2048||size.height()>2048)return fail(QObject::tr("Use ícones de até 2048 × 2048 pixels."));
    reader.setScaledSize(size.scaled(64,64,Qt::KeepAspectRatio));const auto image=reader.read();if(image.isNull())return fail(reader.errorString());return QIcon(QPixmap::fromImage(image));
}
