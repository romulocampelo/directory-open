#include "documenttext.h"
#include "archive.h"
#include "externalprocess.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <algorithm>
namespace{
constexpr qsizetype xmlBudget=8*1024*1024,textBudget=512*1024;
bool append(QString &target,const QString &text){if(text.size()>textBudget-target.size())return false;target+=text;return true;}
bool validXml(QXmlStreamReader &reader,QString &error){if(reader.tokenType()==QXmlStreamReader::DTD){error="Documentos com DTD não são interpretados.";return false;}if(reader.hasError()){error="XML do documento inválido: "+reader.errorString();return false;}return true;}
QString paragraphs(const QByteArray &bytes,bool odf,QString &error){
    QXmlStreamReader reader(bytes);reader.setEntityExpansionLimit(1024);QString text;bool inText=false;int paragraph=0;
    while(!reader.atEnd()){
        reader.readNext();if(!validXml(reader,error))return {};const auto name=reader.name();
        if(reader.isStartElement()){
            if(odf&&(name=="p"||name=="h"))++paragraph;
            if(!odf&&name=="t")inText=true;
            if(name=="tab"){if(!append(text,"\t")){error="Texto acima de 512 mil caracteres.";return {};}}
            if(name=="br"||name=="line-break"){if(!append(text,"\n")){error="Texto acima de 512 mil caracteres.";return {};}}
            if(odf&&name=="s"&&paragraph){int count=1;for(const auto &a:reader.attributes())if(a.name()=="c"){bool ok=false;count=a.value().toInt(&ok);if(!ok||count<1||count>1024){error="Espaçamento ODF fora dos limites.";return {};}}if(!append(text,QString(count,' '))){error="Texto acima de 512 mil caracteres.";return {};}}
        }else if(reader.isCharacters()&&((odf&&paragraph)||(!odf&&inText))){if(!append(text,reader.text().toString())){error="Texto acima de 512 mil caracteres.";return {};}}
        else if(reader.isEndElement()){
            if(name=="t")inText=false;
            if(name=="p"||name=="h"){if(odf&&paragraph)--paragraph;if(!append(text,"\n")){error="Texto acima de 512 mil caracteres.";return {};}}
        }
    }if(!validXml(reader,error))return {};return text;
}
QStringList sharedStrings(const QByteArray &bytes,QString &error){
    QXmlStreamReader reader(bytes);reader.setEntityExpansionLimit(1024);QStringList result;QString current;bool inText=false;qsizetype retained=0;
    while(!reader.atEnd()){reader.readNext();if(!validXml(reader,error))return {};if(reader.isStartElement()){if(reader.name()=="si")current.clear();if(reader.name()=="t")inText=true;}else if(reader.isCharacters()&&inText){if(!append(current,reader.text().toString())){error="String da planilha acima dos limites.";return {};}}else if(reader.isEndElement()){if(reader.name()=="t")inText=false;if(reader.name()=="si"){retained+=current.size();if(result.size()>=50000||retained>xmlBudget){error="Tabela de strings acima dos limites.";return {};}result.append(current);}}}if(!validXml(reader,error))return {};return result;
}
QString worksheet(const QByteArray &bytes,const QStringList &strings,QString &error){
    QXmlStreamReader reader(bytes);reader.setEntityExpansionLimit(1024);QString text,reference,type,value,formula;int cells=0;
    while(!reader.atEnd()){
        reader.readNext();if(!validXml(reader,error))return {};
        if(reader.isStartElement()){
            if(reader.name()=="c"){if(++cells>10000){error="Planilha acima de 10.000 células.";return {};}reference=reader.attributes().value("r").toString();type=reader.attributes().value("t").toString();value.clear();formula.clear();}
            else if(reader.name()=="v"||reader.name()=="t"){value+=reader.readElementText();if(value.size()>textBudget){error="Célula acima dos limites.";return {};}}
            else if(reader.name()=="f")formula=reader.readElementText();
        }else if(reader.isEndElement()&&reader.name()=="c"){
            if(type=="s"){bool ok=false;const auto index=value.toUInt(&ok);if(!ok||index>=quint32(strings.size())){error="Referência inválida à tabela de strings da planilha.";return {};}value=strings[int(index)];}
            if(!formula.isEmpty())value="="+formula+" [cache: "+value+"]";
            if(!append(text,reference+"\t"+value+"\n")){error="Texto acima de 512 mil caracteres.";return {};}
        }
    }if(!validXml(reader,error))return {};return text;
}
struct Sheet{QString name,relationship;bool hidden=false;};
QVector<Sheet> workbook(const QByteArray &bytes,QString &error){QXmlStreamReader reader(bytes);reader.setEntityExpansionLimit(1024);QVector<Sheet>sheets;while(!reader.atEnd()){reader.readNext();if(!validXml(reader,error))return {};if(reader.isStartElement()&&reader.name()=="sheet"){if(sheets.size()>=100){error="O documento excede 100 planilhas.";return {};}Sheet sheet;sheet.name=reader.attributes().value("name").toString();sheet.hidden=reader.attributes().value("state")!="visible"&&!reader.attributes().value("state").isEmpty();for(const auto &a:reader.attributes())if(a.name()=="id")sheet.relationship=a.value().toString();if(sheet.name.size()>128||sheet.relationship.isEmpty()){error="Identidade da planilha inválida.";return {};}sheets.append(sheet);}}if(!validXml(reader,error))return {};return sheets;}
QMap<QString,QString> relationships(const QByteArray &bytes,QString &error){QXmlStreamReader reader(bytes);reader.setEntityExpansionLimit(1024);QMap<QString,QString>parts;while(!reader.atEnd()){reader.readNext();if(!validXml(reader,error))return {};if(reader.isStartElement()&&reader.name()=="Relationship"){const auto a=reader.attributes();if(a.value("TargetMode")=="External")continue;const auto id=a.value("Id").toString(),target=a.value("Target").toString();if(id.isEmpty()||target.size()>1024||parts.contains(id)||parts.size()>1000){error="Relações de planilhas inválidas ou acima dos limites.";return {};}auto path=QDir::cleanPath(target.startsWith('/')?target.mid(1):"xl/"+target);static QRegularExpression allowed("^xl/worksheets/[^/]+\\.xml$");if(allowed.match(path).hasMatch())parts[id]=path;}}if(!validXml(reader,error))return {};return parts;}
}
bool supportsDocumentText(const QString &suffix){return QStringList{"docx","xlsx","pptx","odt","ods","odp"}.contains(suffix.toLower());}
DocumentText extractDocumentText(const QByteArray &snapshot,const QString &suffix,const std::function<bool()> &cancelled){
    DocumentText result;const auto type=suffix.toLower();if(!supportsDocumentText(type)||snapshot.size()>64*1024*1024){result.message="Documento fora dos formatos ou do limite de 64 MiB.";return result;}
    if(cancelled()){result.cancelled=true;return result;}const auto exe=sevenZipExecutable();if(exe.isEmpty()){result.message="7-Zip não encontrado para ler o conteúdo textual deste documento.";return result;}
    QTemporaryDir work(QDir::tempPath()+"/.diropen-document-XXXXXX");if(!work.isValid()){result.message="Não foi possível preparar a leitura do documento.";return result;}work.setAutoRemove(false);
    result=[&]() -> DocumentText {
    const auto input=work.filePath("document.zip");QFile file(input);if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)||file.write(snapshot)!=snapshot.size()){result.message="Não foi possível preparar o snapshot do documento.";return result;}file.close();
    ExternalCommand listing;listing.executable=exe;listing.arguments={"l","-tzip","-slt","-ba","-sccUTF-8","--",input};const auto listed=runExternal(listing,cancelled);
    if(listed.state!=ExternalState::Exited||listed.exitCode!=0){result.cancelled=listed.state==ExternalState::Cancelled;result.message="A leitura do documento não foi concluída. "+listed.message;return result;}
    const auto catalog=parseArchiveListing(listed.output);if(catalog.state!=ArchiveState::Completed){result.message="Documento não suportado: "+catalog.message;return result;}QSet<QString>members;for(const auto &entry:catalog.entries){if(entry.encrypted){result.message="Documento protegido por senha: use o aplicativo associado.";return result;}if(!entry.directory)members.insert(entry.path);}
    qsizetype readBytes=0;auto part=[&](const QString &name)->QByteArray{
        if(!members.contains(name)){result.message="Parte necessária ausente no documento.";return {};}
        ExternalCommand command;command.executable=exe;command.arguments={"e","-tzip","-so","-spd","-sccUTF-8","--",input,name};QByteArray bytes;const auto extracted=runExternal(command,cancelled,[&](const QByteArray &chunk){if(chunk.size()>xmlBudget-readBytes)return false;readBytes+=chunk.size();bytes+=chunk;return true;});
        if(extracted.state!=ExternalState::Exited||extracted.exitCode!=0){result.cancelled=extracted.state==ExternalState::Cancelled;result.message=extracted.state==ExternalState::Incomplete?"O texto XML excede o orçamento de 8 MiB.":"Não foi possível ler uma parte textual do documento.";return {};}return bytes;
    };
    QString error;
    if(type=="docx"){const auto bytes=part("word/document.xml");if(!result.message.isEmpty())return result;result.text=paragraphs(bytes,false,error);}
    else if(type=="odt"||type=="ods"||type=="odp"){const auto bytes=part("content.xml");if(!result.message.isEmpty())return result;result.text=paragraphs(bytes,true,error);}
    else if(type=="pptx"){
        QVector<QPair<int,QString>>slides;static QRegularExpression pattern("^ppt/slides/slide([1-9][0-9]{0,5})\\.xml$");for(const auto &name:members){const auto match=pattern.match(name);if(match.hasMatch())slides.append({match.captured(1).toInt(),name});}if(slides.size()>200){result.message="O documento excede 200 slides.";return result;}std::sort(slides.begin(),slides.end());
        for(const auto &slide:slides){const auto bytes=part(slide.second);if(!result.message.isEmpty())return result;const auto text=paragraphs(bytes,false,error);if(!error.isEmpty())break;if(!append(result.text,QString("Slide %1\n").arg(slide.first)+text+"\n")){error="Texto acima de 512 mil caracteres.";break;}}
    }else{
        QStringList strings;if(members.contains("xl/sharedStrings.xml")){const auto bytes=part("xl/sharedStrings.xml");if(!result.message.isEmpty())return result;strings=sharedStrings(bytes,error);if(!error.isEmpty()){result.message=error;return result;}}
        auto bytes=part("xl/workbook.xml");if(!result.message.isEmpty())return result;const auto sheets=workbook(bytes,error);if(!error.isEmpty()){result.message=error;return result;}bytes=part("xl/_rels/workbook.xml.rels");if(!result.message.isEmpty())return result;const auto relations=relationships(bytes,error);if(!error.isEmpty()){result.message=error;return result;}
        for(const auto &sheet:sheets){if(!relations.contains(sheet.relationship)){error="Uma planilha não tem uma relação interna suportada.";break;}const auto content=part(relations[sheet.relationship]);if(!result.message.isEmpty())return result;const auto text=worksheet(content,strings,error);if(!error.isEmpty())break;if(!append(result.text,sheet.name+(sheet.hidden?" (oculta)":"")+"\n"+text+"\n")){error="Texto acima de 512 mil caracteres.";break;}}
    }
    if(!error.isEmpty()){result.message=error;return result;}if(cancelled()){result.cancelled=true;result.message="Leitura do documento cancelada.";return result;}
    result.complete=true;result.message="Texto extraído do documento; sem aparência de página, imagens, macros, comentários ou recursos externos. Planilhas exibem valores brutos/em cache e fórmulas sem executá-las.";return result;
    }();
    if(!work.remove()){result.complete=false;result.message+="\nPrévia temporária própria preservada por falha de limpeza: "+work.path();}
    if(!result.complete&&!result.text.isEmpty())result.message="Prévia parcial: "+result.message;
    return result;
}
