#pragma once
#include <QByteArray>
#include <QTranslator>
#include <memory>
// Process-wide Qt language selected before constructing any window. No script or
// automatic pack download. The backing bytes outlive QTranslator's memory view.
class UiLanguage final{
public:
    ~UiLanguage();
    QString load(const QString &path);
private:
    QByteArray m_bytes;
    std::unique_ptr<QTranslator> m_translator;
    bool m_installed=false;
};
