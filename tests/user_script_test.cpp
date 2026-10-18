#include "userscript.h"
#include "userscriptdialog.h"
#include "mainwindow.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
class UserScriptTest final:public QObject {
    Q_OBJECT
    QString m_python;
    void put(const QString &path,const QByteArray &bytes);
    QString manifest(const QString &root,const QByteArray &source,const QJsonArray &fields={});
    ScriptResult run(const ScriptExecution &execution);
private slots:
    void initTestCase();
    void loadingIsStrictAndNeverRunsCode();
    void normalizedInputsRespectTypesAndUnicode();
    void approvedSourceIsImmutableAndOriginalEditsDoNotReplaceIt();
    void artifactsBesideSnapshotArePreserved();
    void cancellationWaitsAndPreservesAlreadyCreatedOutput();
    void dialogRequiresFreshPermissionAndRunsOnlyAfterConfirmation();
    void mainWindowPersistsOnlyProgramAndManifestPaths();
    void exampleCreatesOnlyNewOutputAndRejectsDeviceNames();
};
void UserScriptTest::put(const QString &path,const QByteArray &bytes){QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write(bytes),bytes.size());}
QString UserScriptTest::manifest(const QString &root,const QByteArray &source,const QJsonArray &fields){put(root+"/script.py",source);put(root+"/form.json",QJsonDocument(QJsonObject{{"format","DirectoryOpen.Script"},{"version",1},{"title","Demonstração de scripts"},{"script","script.py"},{"fields",fields}}).toJson());return root+"/form.json";}
ScriptResult UserScriptTest::run(const ScriptExecution &execution){UserScriptJob job(execution);job.start();if(!job.wait(15000)){job.cancel();job.wait();QTest::qFail("O script de teste não encerrou no prazo.",__FILE__,__LINE__);}return job.result();}
void UserScriptTest::initTestCase(){m_python=qEnvironmentVariable("DIROPEN_TEST_PYTHON");if(m_python.isEmpty()&&QFileInfo("C:/Python314/python.exe").isFile())m_python="C:/Python314/python.exe";if(!QFileInfo(m_python).isFile())m_python.clear();}
void UserScriptTest::loadingIsStrictAndNeverRunsCode(){
    QTemporaryDir root;const auto path=manifest(root.path(),"from pathlib import Path\nPath('unexpected.txt').write_text('executed')\n");const auto loaded=readUserScript(path);QVERIFY2(loaded.script,qPrintable(loaded.error));QVERIFY(!QFileInfo::exists(root.filePath("unexpected.txt")));auto object=QJsonDocument::fromJson([&]{QFile f(path);if(!f.open(QIODevice::ReadOnly))return QByteArray{};return f.readAll();}()).object();object["script"]="../other.py";put(path,QJsonDocument(object).toJson());QVERIFY(!readUserScript(path).script);object["script"]="script.py";object["autorun"]=true;put(path,QJsonDocument(object).toJson());QVERIFY(!readUserScript(path).script);manifest(root.path(),"# coding: latin-1\nprint('ascii')\n");QVERIFY(!readUserScript(path).script);manifest(root.path(),QByteArray::fromHex("fffe"));QVERIFY(!readUserScript(path).script);QVERIFY(!QFileInfo::exists(root.filePath("unexpected.txt")));
}
void UserScriptTest::normalizedInputsRespectTypesAndUnicode(){
    if(m_python.isEmpty())QSKIP("Python real opcional; DIROPEN_TEST_PYTHON pode indicar o executável.");QTemporaryDir root;const QJsonArray fields{QJsonObject{{"id","text"},{"label","Texto"},{"type","text"},{"required",true}},QJsonObject{{"id","number"},{"label","Número"},{"type","integer"},{"minimum",1},{"maximum",9}},QJsonObject{{"id","flag"},{"label","Opção"},{"type","boolean"}},QJsonObject{{"id","choice"},{"label","Escolha"},{"type","choice"},{"choices",QJsonArray{"ação","日本"}}}};const auto path=manifest(root.path(),"import sys,json\nprint(json.dumps(json.load(sys.stdin), ensure_ascii=False))\n",fields);const auto loaded=readUserScript(path);QVERIFY(loaded.script);QJsonObject values{{"text","ação 日本"}};const auto execution=prepareScriptExecution(*loaded.script,m_python,root.path(),{root.filePath("ação 日本.txt")},values);QVERIFY2(execution.error.isEmpty(),qPrintable(execution.error));const auto result=run(execution);QVERIFY2(result.state==ExternalState::Exited,qPrintable(result.message+result.errors));QCOMPARE(result.exitCode,0);const auto input=QJsonDocument::fromJson(result.output.toUtf8()).object();QCOMPARE(input["values"].toObject()["text"].toString(),QString("ação 日本"));QCOMPARE(input["values"].toObject()["number"].toInt(),1);QCOMPARE(input["selection"].toArray()[0].toString(),root.filePath("ação 日本.txt"));values["number"]=1.5;QVERIFY(!prepareScriptExecution(*loaded.script,m_python,root.path(),{},values).error.isEmpty());values.remove("number");values["undeclared"]=true;QVERIFY(!prepareScriptExecution(*loaded.script,m_python,root.path(),{},values).error.isEmpty());
}
void UserScriptTest::approvedSourceIsImmutableAndOriginalEditsDoNotReplaceIt(){
    if(m_python.isEmpty())QSKIP("Python real ausente.");QTemporaryDir root;const auto source=QByteArray("import json\nprotected=False\ntry:\n    open(__file__, 'w').close()\nexcept PermissionError:\n    protected=True\nprint(json.dumps({'protected':protected, 'code':__file__}))\n");const auto path=manifest(root.path(),source);const auto loaded=readUserScript(path);QVERIFY(loaded.script);put(root.filePath("script.py"),"raise RuntimeError('changed original must not run')\n");const auto execution=prepareScriptExecution(*loaded.script,m_python,root.path(),{},{});const auto result=run(execution);QVERIFY2(result.state==ExternalState::Exited,qPrintable(result.message+result.errors));QCOMPARE(result.exitCode,0);const auto facts=QJsonDocument::fromJson(result.output.toUtf8()).object();QVERIFY2(facts["protected"].toBool(),qPrintable(result.output));QVERIFY(facts["code"].toString()!=loaded.script->sourcePath);QVERIFY(!QFileInfo::exists(facts["code"].toString()));QVERIFY(result.preservedDirectory.isEmpty());
}
void UserScriptTest::artifactsBesideSnapshotArePreserved(){
    if(m_python.isEmpty())QSKIP("Python real ausente.");QTemporaryDir root;const auto loaded=readUserScript(manifest(root.path(),"from pathlib import Path\nPath(__file__).with_name('artifact.txt').write_text('preserved')\nprint('done')\n"));QVERIFY(loaded.script);const auto result=run(prepareScriptExecution(*loaded.script,m_python,root.path(),{},{}));QCOMPARE(result.exitCode,0);QCOMPARE(result.state,ExternalState::Incomplete);QVERIFY(!result.preservedDirectory.isEmpty());QVERIFY(QFileInfo::exists(result.preservedDirectory+"/artifact.txt"));QVERIFY(!QFileInfo::exists(result.preservedDirectory+"/script.py"));QVERIFY(QFile::remove(result.preservedDirectory+"/artifact.txt"));QVERIFY(QDir().rmdir(result.preservedDirectory));
}
void UserScriptTest::cancellationWaitsAndPreservesAlreadyCreatedOutput(){
    if(m_python.isEmpty())QSKIP("Python real ausente.");QTemporaryDir root;const auto loaded=readUserScript(manifest(root.path(),"from pathlib import Path\nimport time\nPath('ready.txt').write_text('started')\ntime.sleep(60)\n"));QVERIFY(loaded.script);UserScriptJob job(prepareScriptExecution(*loaded.script,m_python,root.path(),{},{}));job.start();QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(root.filePath("ready.txt")),10000);job.cancel();QVERIFY(job.wait(10000));QCOMPARE(job.result().state,ExternalState::Cancelled);QVERIFY(QFileInfo::exists(root.filePath("ready.txt")));QVERIFY(job.result().preservedDirectory.isEmpty());
}
void UserScriptTest::dialogRequiresFreshPermissionAndRunsOnlyAfterConfirmation(){
    if(m_python.isEmpty())QSKIP("Python real ausente.");QTemporaryDir root;const auto path=manifest(root.path(),"import json,sys\nprint('Olá, Directory Open')\n",{QJsonObject{{"id","text"},{"label","Texto de exemplo"},{"type","text"},{"default","Demonstração"}}});UserScriptDialog dialog(m_python,path,root.path(),{});dialog.show();auto *run=dialog.findChild<QPushButton*>("runUserScript");auto *permission=dialog.findChild<QCheckBox*>("scriptPermission");QVERIFY(!run->isEnabled());QVERIFY(dialog.findChild<QPlainTextEdit*>("scriptCode")->toPlainText().isEmpty());dialog.findChild<QPushButton*>("loadUserScript")->click();QVERIFY(!run->isEnabled());permission->setChecked(true);QVERIFY(run->isEnabled());dialog.findChild<QLineEdit*>("scriptField_text")->setText("novo");QVERIFY(!permission->isChecked());permission->setChecked(true);QTimer::singleShot(0,&dialog,[&]{auto *question=dialog.findChild<QMessageBox*>("confirmUserScript");QVERIFY(question);QCOMPARE(question->defaultButton(),question->button(QMessageBox::No));question->button(QMessageBox::Yes)->click();});run->click();QTRY_VERIFY_WITH_TIMEOUT(dialog.findChild<QLabel*>("scriptStatus")->text().contains("código 0"),10000);QVERIFY(dialog.findChild<QPlainTextEdit*>("scriptOutput")->toPlainText().contains("Olá"));QVERIFY(!permission->isChecked());QVERIFY(!run->isEnabled());QMetaObject::invokeMethod(&dialog,"reject");
}
void UserScriptTest::mainWindowPersistsOnlyProgramAndManifestPaths(){
    QTemporaryDir root;const auto path=manifest(root.path(),"print('not run')\n",{QJsonObject{{"id","text"},{"label","Exemplo"},{"type","text"}}});const auto session=root.filePath("session.json");MainWindow window(root.path(),root.path(),false,session);window.show();QTimer::singleShot(0,&window,[&]{auto *dialog=window.findChild<UserScriptDialog*>("userScriptDialog");QVERIFY(dialog);auto close=qScopeGuard([dialog]{QMetaObject::invokeMethod(dialog,"reject");});dialog->findChild<QLineEdit*>("scriptManifest")->setText(path);dialog->findChild<QLineEdit*>("scriptInterpreter")->setText(m_python);dialog->findChild<QPushButton*>("loadUserScript")->click();auto *field=dialog->findChild<QLineEdit*>("scriptField_text");QVERIFY(field);QTRY_VERIFY(field->isVisible());field->setText("valor-efemero-123");QTest::qWait(50);const auto out=qEnvironmentVariable("DIROPEN_SCRIPT_CAPTURES");if(!out.isEmpty()){QVERIFY(QDir().mkpath(out));QVERIFY(dialog->grab().save(out+"/script-form.png"));}QVERIFY(!dialog->findChild<QPushButton*>("runUserScript")->isEnabled());});window.findChild<QAction*>("userScripts")->trigger();QCOMPARE(window.session().tools.scriptManifest,path);QVERIFY(window.close());QFile saved(session);QVERIFY(saved.open(QIODevice::ReadOnly));QVERIFY(!saved.readAll().contains("valor-efemero-123"));saved.close();MainWindow restored({}, {},false,session);QCOMPARE(restored.session().tools.scriptManifest,path);QVERIFY(restored.close());
}
void UserScriptTest::exampleCreatesOnlyNewOutputAndRejectsDeviceNames(){
    if(m_python.isEmpty())QSKIP("Python real ausente.");const auto manifest=QFINDTESTDATA("../examples/scripts/listagem.json");QVERIFY(!manifest.isEmpty());const auto loaded=readUserScript(manifest);QVERIFY2(loaded.script,qPrintable(loaded.error));QTemporaryDir root;put(root.filePath("origem.txt"),"unchanged");QJsonObject values{{"destination",root.path()},{"name","nova.txt"}};auto result=run(prepareScriptExecution(*loaded.script,m_python,root.path(),{root.filePath("origem.txt")},values));QCOMPARE(result.state,ExternalState::Exited);QCOMPARE(result.exitCode,0);QFile output(root.filePath("nova.txt"));QVERIFY(output.open(QIODevice::ReadOnly));const auto bytes=output.readAll();QVERIFY(bytes.contains("origem.txt"));output.close();result=run(prepareScriptExecution(*loaded.script,m_python,root.path(),{},values));QVERIFY(result.exitCode!=0);QVERIFY(output.open(QIODevice::ReadOnly));QCOMPARE(output.readAll(),bytes);output.close();for(const auto &name:QStringList{"CON","com1.txt","LPT¹.txt",QString("invalid")+QChar(1)+".txt"}){values["name"]=name;result=run(prepareScriptExecution(*loaded.script,m_python,root.path(),{},values));QVERIFY2(result.exitCode!=0,qPrintable(name));QVERIFY(result.errors.contains("Informe somente"));}
    const auto out=qEnvironmentVariable("DIROPEN_SCRIPT_CAPTURES");if(!out.isEmpty()){MainWindow window(root.path(),root.path(),false,{});window.show();UserScriptDialog dialog(m_python,manifest,root.path(),{root.filePath("origem.txt")},&window);dialog.show();dialog.findChild<QPushButton*>("loadUserScript")->click();auto *destination=dialog.findChild<QLineEdit*>("scriptField_destination");QVERIFY(destination);destination->setText(root.path());QTRY_VERIFY(destination->isVisible());QTest::qWait(100);QVERIFY(QDir().mkpath(out));QVERIFY(dialog.grab().save(out+"/script-form.png"));QMetaObject::invokeMethod(&dialog,"reject");QVERIFY(window.close());}
}
int main(int argc,char**argv){QApplication app(argc,argv);app.setStyle("Fusion");UserScriptTest test;return QTest::qExec(&test,argc,argv);}
#include "user_script_test.moc"
