#include "mainwindow.h"
#include <QApplication>
#include <QAction>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QTimer>
#include <QWindow>
#include "filepanel.h"

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("Directory Open"); app.setApplicationVersion(QString::fromLatin1(DIROPEN_VERSION));
    app.setStyle("Fusion"); QImageReader::setAllocationLimit(64);
    QCommandLineParser parser; parser.setApplicationDescription("Gerenciador de arquivos Directory Open");
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"left", "Pasta inicial do painel esquerdo.", "path"});
    parser.addOption({"right", "Pasta inicial do painel direito.", "path"});
    parser.addOption({"capture", "Captura da janela para verificação.", "file"});
    parser.addOption({"dark", "Iniciar com tema escuro."});
    parser.addOption({"no-session", "Iniciar sem ler ou guardar a sessão."});
    parser.addOption({"session-file", "Registro de sessão alternativo.", "path"});
    parser.process(app);
    const auto sessionPath = parser.isSet("no-session") || (parser.isSet("capture") && !parser.isSet("session-file")) ? QString{}
        : parser.isSet("session-file") ? QFileInfo(parser.value("session-file")).absoluteFilePath() : SessionStore::defaultPath();
    MainWindow window(parser.value("left"), parser.value("right"), true, sessionPath); window.show();
    if (parser.isSet("dark")) window.findChild<QAction *>("darkTheme")->setChecked(true);
    if (parser.isSet("capture")) {
        QTimer::singleShot(3500, &window, [&] {
            const bool exposed = window.windowHandle() && window.windowHandle()->isExposed();
            const QString output = parser.value("capture");
            const bool captured = window.grab().save(output);
            QJsonObject result{{"windowExposed", exposed}, {"captured", captured},
                {"qtVersion", qVersion()}, {"appVersion", QCoreApplication::applicationVersion()}, {"platform", QApplication::platformName()},
                {"leftPath", window.leftPanel()->path()}, {"rightPath", window.rightPanel()->path()},
                {"leftTabs", window.leftPanel()->tabCount()}, {"rightTabs", window.rightPanel()->tabCount()},
                {"dark", window.session().dark}, {"dualPane", window.session().dualPane}, {"activePanel", window.session().activePanel},
                {"leftItems", window.leftPanel()->visibleCount()}, {"rightItems", window.rightPanel()->visibleCount()}};
            QFile file(output + ".json"); const auto bytes = QJsonDocument(result).toJson();
            const bool written = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
            file.close(); window.close(); app.exit(exposed && captured && written ? 0 : 1);
        });
    }
    return app.exec();
}
