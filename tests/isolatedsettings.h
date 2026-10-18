#pragma once
// Test-only: lifetime encloses QApplication, including initialization and teardown.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTest>
#include <QUuid>
#include <cstdio>
#include <windows.h>

class IsolatedSettings final {
    HKEY m_user = nullptr, m_key = nullptr;
    bool m_owned = false, m_redirected = false;
    QString m_path = "Software\\DirectoryOpen.Tests\\" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString m_nonce = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString m_receipt;
    bool m_failUnmapOnce = false, m_failDeleteOnce = false;
    bool matches(HKEY key, const wchar_t *subkey = nullptr, const wchar_t *name = L"DirectoryOpenTestOwner") const {
        wchar_t value[40]{}; DWORD type = 0, bytes = sizeof(value);
        return RegGetValueW(key, subkey, name, RRF_RT_REG_SZ | (subkey ? RRF_SUBKEY_WOW6464KEY : 0), &type,
            reinterpret_cast<BYTE *>(value), &bytes) == ERROR_SUCCESS && type == REG_SZ
            && bytes == (m_nonce.size()+1)*sizeof(wchar_t) && QString::fromWCharArray(value) == m_nonce;
    }
public:
    IsolatedSettings() = default;
    IsolatedSettings(const IsolatedSettings &) = delete;
    IsolatedSettings &operator=(const IsolatedSettings &) = delete;
    ~IsolatedSettings() { if (!close()) std::fprintf(stderr, "Settings cleanup pending: key=%s receipt=%s\n", qPrintable(m_path), qPrintable(m_receipt)); }
    QString receipt() const { return m_receipt; }
    // Explicit recovery of ONE recorded fixture. Never enumerate/delete by prefix.
    static bool recover(const QString &receiptPath) {
        QFile file(receiptPath);
        if (!file.open(QIODevice::ReadOnly) || file.size()>4096) return false;
        const auto object=QJsonDocument::fromJson(file.readAll()).object(); file.close();
        const auto path=object["key"].toString(), nonce=object["owner"].toString();
        const auto id=path.section('\\',-1);
        if (QUuid(id).isNull() || QUuid(nonce).isNull() || path!="Software\\DirectoryOpen.Tests\\"+id) return false;
        IsolatedSettings owner; owner.m_path=path; owner.m_nonce=nonce;
        if (RegOpenCurrentUser(KEY_ALL_ACCESS,&owner.m_user)!=ERROR_SUCCESS) return false;
        if (RegOpenKeyExW(owner.m_user,path.toStdWString().c_str(),0,KEY_ALL_ACCESS|KEY_WOW64_64KEY,&owner.m_key)!=ERROR_SUCCESS) return false;
        if (!owner.matches(owner.m_key)) return false;
        owner.m_receipt=receiptPath; owner.m_owned=true;
        return owner.close();
    }
    bool open(const QString &fault = {}) {
        if (RegOpenCurrentUser(KEY_ALL_ACCESS, &m_user) != ERROR_SUCCESS) return false;
        if (fault == "open") return false;
        DWORD disposition = 0;
        if (RegCreateKeyExW(m_user, m_path.toStdWString().c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
            KEY_ALL_ACCESS | KEY_WOW64_64KEY, nullptr, &m_key, &disposition) != ERROR_SUCCESS) return false;
        m_owned = disposition == REG_CREATED_NEW_KEY;
        if (!m_owned) return false; // Never adopt or delete an existing key.
        const auto nonce = m_nonce.toStdWString();
        if (RegSetValueExW(m_key, L"DirectoryOpenTestOwner", 0, REG_SZ,
            reinterpret_cast<const BYTE *>(nonce.c_str()), DWORD((nonce.size()+1)*sizeof(wchar_t))) != ERROR_SUCCESS) return false;
        const auto dir = QDir::currentPath()+"/.cache/test-settings";
        if (!QDir().mkpath(dir)) return false;
        m_receipt = dir+'/'+m_nonce+".json";
        QFile receipt(m_receipt);
        const auto data = QJsonDocument(QJsonObject{{"key",m_path},{"owner",m_nonce}}).toJson();
        if (!receipt.open(QIODevice::WriteOnly|QIODevice::NewOnly) || receipt.write(data) != data.size() || !receipt.flush()) return false;
        receipt.close();
        if (fault == "create") return false;
        if (RegOverridePredefKey(HKEY_CURRENT_USER,m_key) != ERROR_SUCCESS) return false;
        m_redirected = true;
        m_failUnmapOnce = fault == "cleanup";
        m_failDeleteOnce = fault == "delete";
        if (fault == "redirect") return false;
        // QFileDialog uses this explicit NativeFormat constructor, regardless of default format.
        QSettings probe(QSettings::UserScope,"QtProject");
        probe.setValue("_isolationProbe",m_nonce); probe.sync();
        const bool routed = matches(m_key,L"Software\\QtProject\\OrganizationDefaults",L"_isolationProbe");
        const bool ok = routed && probe.status() == QSettings::NoError;
        probe.remove("_isolationProbe"); probe.sync();
        return ok && probe.status() == QSettings::NoError && fault != "probe";
    }
    bool close() {
        // If unmapping fails, retain all handles and ownership for retry; never delete the active hive.
        if (m_redirected) {
            if (m_failUnmapOnce) { m_failUnmapOnce=false; return false; }
            if (RegOverridePredefKey(HKEY_CURRENT_USER,nullptr) != ERROR_SUCCESS) return false;
            m_redirected = false;
        }
        if (m_owned) {
            const bool owned = matches(m_key) && matches(m_user,m_path.toStdWString().c_str());
            if (!owned) return false;
            const auto removed = RegDeleteTreeW(m_key,L"Software");
            if (removed != ERROR_SUCCESS && removed != ERROR_FILE_NOT_FOUND) return false;
            if (m_failDeleteOnce) { m_failDeleteOnce=false; return false; }
            if (RegDeleteKeyExW(m_user,m_path.toStdWString().c_str(),KEY_WOW64_64KEY,0) != ERROR_SUCCESS) return false;
            m_owned = false;
        }
        if (m_key) { if (RegCloseKey(m_key) != ERROR_SUCCESS) return false; m_key=nullptr; }
        if (m_user) { if (RegCloseKey(m_user) != ERROR_SUCCESS) return false; m_user=nullptr; }
        return m_receipt.isEmpty() || !QFile::exists(m_receipt) || QFile::remove(m_receipt);
    }
};

template<class Test, class Application = QApplication>
int isolatedGuiTest(int argc, char **argv) {
    QString fault;
    for (int i=1;i<argc;++i) if (QByteArray(argv[i]).startsWith("--settings-fault=")) fault=QString::fromLocal8Bit(argv[i]).section('=',1);
    IsolatedSettings settings;
    if (!settings.open(fault)) return settings.close() ? 73 : 74;
    int result = 0;
    {
        Application app(argc,argv);
        app.setAttribute(Qt::AA_Use96Dpi,true); // Preserve QTEST_MAIN setup in the replaced entry point.
        if (fault == "app") return 75; // Both application and settings destructors run.
        if (fault == "abrupt") {
            std::fprintf(stdout,"%s\n",qPrintable(settings.receipt())); std::fflush(stdout);
            ExitProcess(76); // Intentional fixture: receipt survives, no destructor can run.
        }
        if (fault != "cleanup" && fault != "delete") { Test test; result=QTest::qExec(&test,argc,argv); }
    }
    return settings.close() ? result : 74;
}
