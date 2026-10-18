#include "shellcontextmenu.h"
#include "mainwindow.h"
#include "filepanel.h"
#include "operationdialog.h"
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <QImage>
#include <QScopeGuard>
#include <windows.h>
#include <shlobj.h>

namespace {
UINT findVerb(const ShellContextMenu &shell, HMENU menu, const QString &verb) {
    for (int i = 0; i < GetMenuItemCount(menu); ++i) {
        const UINT id = GetMenuItemID(menu, i);
        if (shell.commandVerb(id) == verb) return id;
        if (auto sub = GetSubMenu(menu, i)) { if (auto found = findVerb(shell, sub, verb)) return found; }
    }
    return 0;
}
QStringList labels(HMENU menu) {
    QStringList result;
    for (int i = 0; i < GetMenuItemCount(menu); ++i) {
        wchar_t text[1024]{}; GetMenuStringW(menu, UINT(i), text, 1024, MF_BYPOSITION);
        result.append(QString::fromWCharArray(text));
    }
    return result;
}
HMENU activeMenu = nullptr;
bool sawPopup = false, sawDynamic = false;
QString capturePath;
bool captureSettled = false;
QString propertiesFixture;
HWND propertiesOwner = nullptr;
bool sawProperties = false;
void CALLBACK closeFixtureProperties(HWND, UINT, UINT_PTR, DWORD) {
    EnumWindows([](HWND window, LPARAM) -> BOOL {
        DWORD processId = 0; GetWindowThreadProcessId(window, &processId);
        // Observe/close only a dialog in this process or owned by the fixture.
        if (processId != GetCurrentProcessId() && GetAncestor(window, GA_ROOTOWNER) != propertiesOwner) return TRUE;
        if (!IsWindowVisible(window)) return TRUE;
        wchar_t name[256]{}, title[512]{}; GetClassNameW(window, name, 256); GetWindowTextW(window, title, 512);
        if (QString::fromWCharArray(name) == "#32770" && QString::fromWCharArray(title).contains(propertiesFixture)) {
            qInfo().noquote() << "Owned fixture properties dialog:" << QString::fromWCharArray(title);
            sawProperties = PostMessageW(window, WM_CLOSE, 0, 0) != FALSE;
        }
        return TRUE;
    }, 0);
}
void CALLBACK cancelMenu(HWND hwnd, UINT, UINT_PTR timer, DWORD) {
    KillTimer(hwnd, timer);
    GUITHREADINFO info{sizeof(info)};
    sawPopup = GetGUIThreadInfo(GetCurrentThreadId(), &info) && info.hwndMenuOwner == hwnd;
    if (!capturePath.isEmpty() && !captureSettled) {
        // A cold Shell query may consume the timer before TrackPopupMenu starts.
        // Give the newly created popup a paint cycle before capturing its pixels.
        captureSettled = true; SetTimer(hwnd, timer, 600, cancelMenu); return;
    }
    if (activeMenu) {
        for (int i = 0; i < GetMenuItemCount(activeMenu); ++i) {
            auto sub = GetSubMenu(activeMenu, i);
            if (!sub) continue;
            // Forward through the real owner subclass, including lazy Shell menus.
            SendMessageW(hwnd, WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(sub), MAKELPARAM(i, FALSE));
            if (i < GetMenuItemCount(activeMenu) - 1 && GetMenuItemCount(sub) > 0) sawDynamic = true;
        }
    }
    if (!capturePath.isEmpty()) EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM) -> BOOL {
        wchar_t name[64]{}; GetClassNameW(window, name, 64);
        if (!IsWindowVisible(window) || QString::fromWCharArray(name) != "#32768") return TRUE;
        // Native menu pixels only. Screen-grab logical coordinates can cross
        // monitor/DPI boundaries and accidentally include unrelated windows.
        RECT rect{}; if (!GetWindowRect(window, &rect)) return TRUE;
        const int width = rect.right - rect.left, height = rect.bottom - rect.top;
        if (width <= 0 || height <= 0 || quint64(width) * quint64(height) > 16000000) return TRUE;
        HDC screen = GetDC(nullptr); if (!screen) return TRUE;
        HDC memory = CreateCompatibleDC(screen);
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        void *pixels = nullptr; HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (memory && bitmap) {
            const auto old = SelectObject(memory, bitmap);
            if (BitBlt(memory, 0, 0, width, height, screen, rect.left, rect.top, SRCCOPY | CAPTUREBLT))
                QImage(static_cast<const uchar *>(pixels), width, height, QImage::Format_RGB32).copy().save(capturePath);
            SelectObject(memory, old);
        }
        if (bitmap) DeleteObject(bitmap); if (memory) DeleteDC(memory); ReleaseDC(nullptr, screen);
        return FALSE;
    }, 0);
    captureSettled = false; EndMenu();
}
}

class ShellContextTest : public QObject {
    Q_OBJECT
    void write(const QString &path) { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("fixture"), qint64(7)); }
private slots:
    void realSelectionAndCanonicalRouting_data() {
        QTest::addColumn<bool>("multiple");
        QTest::newRow("single-unicode") << false; QTest::newRow("multiple") << true;
    }
    void realSelectionAndCanonicalRouting() {
        QFETCH(bool, multiple);
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const auto a = fixture.path() + "/ação 中文.txt", b = fixture.path() + "/second.txt";
        write(a); write(b); QWidget owner; owner.show(); QVERIFY(QTest::qWaitForWindowExposed(&owner));
        QString error; const QStringList selection = multiple ? QStringList{a, b} : QStringList{a};
        auto shell = ShellContextMenu::create(&owner, selection, fixture.path(), false, error);
        QVERIFY2(shell, qPrintable(error)); const auto menu = reinterpret_cast<HMENU>(shell->nativeMenu());
        QVERIFY(GetMenuItemCount(menu) > 0);
        for (const auto &verb : {"delete", "copy", "cut", "properties"}) QVERIFY2(findVerb(*shell, menu, verb), verb);
        if (!multiple) QVERIFY(findVerb(*shell, menu, "rename"));
        QAction trash("Lixeira", &owner); bool triggered = false;
        connect(&trash, &QAction::triggered, &owner, [&] { triggered = true; });
        bool external = false;
        const auto result = shell->dispatch(findVerb(*shell, menu, "delete"), {{"delete", &trash}}, [&] { external = true; });
        QCOMPARE(result.action.data(), &trash); QVERIFY(result.error.isEmpty()); QVERIFY(!result.invoked); QVERIFY(!external); QVERIFY(!triggered);
        shell.reset(); result.action->trigger(); QVERIFY(triggered); QVERIFY(QFileInfo::exists(a)); QVERIFY(QFileInfo::exists(b));
    }
    void folderAndBackgroundMenus() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); QVERIFY(QDir(fixture.path()).mkdir("pasta ação"));
        QWidget owner; QString error;
        auto folder = ShellContextMenu::create(&owner, {fixture.path() + "/pasta ação"}, fixture.path(), true, error);
        QVERIFY2(folder, qPrintable(error));
        auto menu = reinterpret_cast<HMENU>(folder->nativeMenu());
        QVERIFY(findVerb(*folder, menu, "open")); QVERIFY(findVerb(*folder, menu, "properties"));
        qInfo().noquote() << "Folder Shell entries:" << labels(menu).join(" | ");
        auto background = ShellContextMenu::create(&owner, {}, fixture.path(), false, error);
        QVERIFY2(background, qPrintable(error)); QVERIFY(GetMenuItemCount(reinterpret_cast<HMENU>(background->nativeMenu())) > 0);
    }
    void applicationSubmenusAndDisabledActions() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); write(fixture.path() + "/a.txt");
        QWidget owner; QString error;
        auto shell = ShellContextMenu::create(&owner, {fixture.path() + "/a.txt"}, fixture.path(), false, error); QVERIFY2(shell, qPrintable(error));
        QMenu menu; auto *first = menu.addAction("Primeiro"); menu.addSeparator(); auto *sub = menu.addMenu("Submenu"); auto *disabled = sub->addAction("Desabilitado"); disabled->setEnabled(false);
        QVERIFY2(shell->appendApplicationMenu(menu, error), qPrintable(error));
        auto native = reinterpret_cast<HMENU>(shell->nativeMenu()); auto application = GetSubMenu(native, GetMenuItemCount(native) - 1); QVERIFY(application);
        QCOMPARE(shell->dispatch(GetMenuItemID(application, 0), {}).action.data(), first);
        auto nested = GetSubMenu(application, 2); QVERIFY(nested);
        QVERIFY(GetMenuState(nested, 0, MF_BYPOSITION) & MF_GRAYED);
        QCOMPARE(shell->dispatch(GetMenuItemID(nested, 0), {}).action.data(), disabled);
        QVERIFY(shell->dispatch(0, {}).error.isEmpty()); QVERIFY(!shell->dispatch(0xffffffff, {}).error.isEmpty());
        first->deleteLater(); QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!shell->dispatch(GetMenuItemID(application, 0), {}).action);
    }
    void invalidOrMixedSelectionHasNoPartialMenu() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); write(fixture.path() + "/a.txt"); QVERIFY(QDir(fixture.path()).mkdir("other")); write(fixture.path() + "/other/b.txt");
        QWidget owner; QString error;
        for (const auto &selection : {QStringList{fixture.path() + "/a.txt", fixture.path() + "/missing"},
                                     QStringList{fixture.path() + "/a.txt", fixture.path() + "/other/b.txt"}, QStringList{"relative.txt"}}) {
            QVERIFY(!ShellContextMenu::create(&owner, selection, fixture.path(), false, error)); QVERIFY(!error.isEmpty());
        }
        QVERIFY(!ShellContextMenu::create(&owner, {}, fixture.path() + "/missing", false, error)); QVERIFY(!error.isEmpty());
    }
    void invokesRealPropertiesForFixtureOnly() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        // Extension visibility is a Shell preference, not part of the command contract.
        // An extensionless nonce keeps the fixture identity stable in the dialog title.
        propertiesFixture = "diropen-properties-" + QFileInfo(fixture.path()).fileName();
        const auto path = fixture.path() + '/' + propertiesFixture; write(path);
        QWidget owner; owner.show(); QVERIFY(QTest::qWaitForWindowExposed(&owner));
        QString error; auto shell = ShellContextMenu::create(&owner, {path}, fixture.path(), false, error); QVERIFY2(shell, qPrintable(error));
        const auto command = findVerb(*shell, reinterpret_cast<HMENU>(shell->nativeMenu()), "properties"); QVERIFY(command);
        const auto hwnd = reinterpret_cast<HWND>(owner.winId()); propertiesOwner = hwnd; sawProperties = false; bool released = false;
        QVERIFY(SetTimer(hwnd, 7820, 100, closeFixtureProperties));
        const auto stopObserver = qScopeGuard([hwnd] { KillTimer(hwnd, 7820); propertiesOwner = nullptr; });
        const auto result = shell->dispatch(command, {}, [&] { released = true; });
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error)); QVERIFY(result.invoked); QVERIFY(released); QVERIFY(!result.action);
        QTRY_VERIFY_WITH_TIMEOUT(sawProperties, 5000); shell.reset();
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("fixture"));
    }
    void realPopupLazySubmenusAndCancellationReleaseResources() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); QVERIFY(QDir(fixture.path()).mkdir("Directory Open — demonstração"));
        QWidget owner; owner.setWindowTitle("Directory Open — menu nativo / dados de demonstração"); owner.resize(1200, 850); owner.show();
        QVERIFY(QTest::qWaitForWindowExposed(&owner));
        const auto hwnd = reinterpret_cast<HWND>(owner.winId());
        for (int i = 0; i < 4; ++i) {
            QString error; auto shell = ShellContextMenu::create(&owner, {fixture.path() + "/Directory Open — demonstração"}, fixture.path(), false, error); QVERIFY2(shell, qPrintable(error));
            QMenu own; own.addAction("Abrir em nova aba"); QVERIFY(shell->appendApplicationMenu(own, error));
            activeMenu = reinterpret_cast<HMENU>(shell->nativeMenu()); sawPopup = false; sawDynamic = false;
            capturePath = i == 0 ? qEnvironmentVariable("DIROPEN_CAPTURE_SHELL_MENU") : QString{};
            QVERIFY(SetTimer(hwnd, 7819, 600, cancelMenu));
            QCOMPARE(shell->track(owner.mapToGlobal(QPoint(30, 30)), error), quint32(0)); QVERIFY2(error.isEmpty(), qPrintable(error));
            QVERIFY(sawPopup); QVERIFY(sawDynamic);
            const auto ownedMenu = activeMenu;
            const auto ownedApplicationMenu = GetSubMenu(ownedMenu, GetMenuItemCount(ownedMenu) - 1);
            activeMenu = nullptr; shell.reset();
            QVERIFY(!IsMenu(ownedMenu)); QVERIFY(!IsMenu(ownedApplicationMenu));
        }
    }
    void applicationPopupCancellationControl() {
        // Same completed task (four popup/cancel cycles), without loading Shell
        // extensions. Retained as a measured desktop control for the native host.
        QWidget owner; owner.setWindowTitle("Directory Open — demonstração"); owner.resize(1200, 850); owner.show();
        QVERIFY(QTest::qWaitForWindowExposed(&owner));
        for (int i = 0; i < 4; ++i) {
            QMenu menu(&owner);
            menu.addAction("Abrir"); menu.addAction("Abrir em nova aba"); menu.addSeparator();
            menu.addAction("Copiar"); menu.addAction("Recortar"); menu.addAction("Colar");
            auto *between = menu.addMenu("Para o outro painel"); between->addAction("Copiar"); between->addAction("Mover");
            menu.addSeparator(); menu.addAction("Renomear"); menu.addAction("Renomear em lote…"); menu.addAction("Lixeira");
            menu.addSeparator(); menu.addAction("Nova pasta"); menu.addAction("Copiar caminho"); menu.addAction("Atualizar");
            bool shown = false;
            QTimer::singleShot(600, &menu, [&] { shown = menu.isVisible(); menu.close(); });
            QVERIFY(!menu.exec(owner.mapToGlobal(QPoint(30, 30)))); QVERIFY(shown);
        }
    }
    void mainWindowNativeContextPreservesRightPanelBatch_data() {
        QTest::addColumn<bool>("native");
        QTest::newRow("native") << true; QTest::newRow("simple") << false;
    }
    void mainWindowNativeContextPreservesRightPanelBatch() {
        QFETCH(bool, native);
        QTemporaryDir fixture; QVERIFY(fixture.isValid()); QVERIFY(QDir(fixture.path()).mkdir("left")); QVERIFY(QDir(fixture.path()).mkdir("right"));
        write(fixture.path() + "/right/relatório.txt"); write(fixture.path() + "/right/leitura.txt");
        MainWindow window(fixture.path() + "/left", fixture.path() + "/right", false); window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window)); QTRY_COMPARE(window.rightPanel()->visibleCount(), 2);
        auto *setting = window.findChild<QAction *>("nativeContextMenu"); QVERIFY(setting);
        QCOMPARE(setting->text(), QString("Menu de contexto do Windows (Consome mais CPU)"));
        QVERIFY(window.findChild<QMenu *>("settingsMenu")->actions().contains(setting)); setting->setChecked(native);
        auto *view = window.rightPanel()->findChild<QTreeView *>("files"); view->selectAll();
        const auto hwnd = reinterpret_cast<HWND>(window.winId()); activeMenu = nullptr; sawPopup = false;
        capturePath = native ? qEnvironmentVariable("DIROPEN_CAPTURE_MAIN_CONTEXT") : QString{};
        QTimer watchdog; watchdog.setSingleShot(true);
        connect(&watchdog, &QTimer::timeout, &window, [&] { for (auto *menu : window.findChildren<QMenu *>()) menu->close(); EndMenu(); });
        bool simpleShown = false;
        watchdog.start(5000);
        if (native) { QVERIFY(SetTimer(hwnd, 7819, 600, cancelMenu)); }
        else QTimer::singleShot(0, &window, [&] {
            auto *menu = window.findChild<QMenu *>("fileContextMenu"); if (!menu) return;
            simpleShown = menu->isVisible(); menu->close();
        });
        const auto point = view->visualRect(view->model()->index(0, 0, view->rootIndex())).center();
        QContextMenuEvent event(QContextMenuEvent::Mouse, point, view->viewport()->mapToGlobal(point));
        QApplication::sendEvent(view->viewport(), &event); watchdog.stop(); KillTimer(hwnd, 7819);
        QCOMPARE(sawPopup, native); QCOMPARE(simpleShown, !native);
        QCOMPARE(window.session().activePanel, 1); QCOMPARE(window.rightPanel()->selectedPaths().size(), 2);
        QVERIFY(QFileInfo::exists(fixture.path() + "/right/relatório.txt"));
    }
};
QTEST_MAIN(ShellContextTest)
#include "shell_context_test.moc"
