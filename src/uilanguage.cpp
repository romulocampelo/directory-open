#include "uilanguage.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
UiLanguage::~UiLanguage(){if(m_installed)QCoreApplication::removeTranslator(m_translator.get());}
QString UiLanguage::load(const QString &path){
    if(m_installed){QCoreApplication::removeTranslator(m_translator.get());m_installed=false;}
    m_translator.reset();m_bytes.clear();
    if(path.isEmpty())return {};
    if(QFileInfo(path).suffix().compare("qm",Qt::CaseInsensitive)!=0)return "Escolha um pacote de tradução Qt (.qm).";
    QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>1024*1024)return "O pacote de idioma não está disponível ou excede 1 MiB. O português foi mantido.";
    auto bytes=file.read(1024*1024+1);if(bytes.size()>1024*1024||file.error()!=QFileDevice::NoError)return "Não foi possível ler o pacote de idioma. O português foi mantido.";
    m_bytes=std::move(bytes);m_translator=std::make_unique<QTranslator>();
    if(!m_translator->load(reinterpret_cast<const uchar*>(m_bytes.constData()),int(m_bytes.size()))||m_translator->isEmpty())return "Pacote de tradução inválido ou vazio. O português foi mantido.";
    m_installed=QCoreApplication::installTranslator(m_translator.get());return m_installed?QString{}:QString("Não foi possível ativar o idioma. O português foi mantido.");
}
