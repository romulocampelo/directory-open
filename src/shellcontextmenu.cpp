#include "shellcontextmenu.h"
#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QSet>
#include <QWidget>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <vector>
#include <type_traits>

using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT firstShellId = 1, lastShellId = 0x6fff, firstAppId = 0x7000, lastAppId = 0x7fff;
QString failure(const QString &stage, HRESULT hr) {
    return stage + QStringLiteral(" (0x%1)").arg(quint32(hr), 8, 16, QLatin1Char('0'));
}
struct PidlDeleter { void operator()(ITEMIDLIST *value) const { CoTaskMemFree(value); } };
using Pidl = std::unique_ptr<ITEMIDLIST, PidlDeleter>;
struct MenuDeleter { void operator()(std::remove_pointer_t<HMENU> *value) const { DestroyMenu(value); } };
using Menu = std::unique_ptr<std::remove_pointer_t<HMENU>, MenuDeleter>;
struct Apartment {
    HRESULT hr = OleInitialize(nullptr);
    ~Apartment() { if (SUCCEEDED(hr)) OleUninitialize(); }
};
bool parse(const QString &path, Pidl &pidl, QString &error) {
    if (path.contains(QChar::Null) || !QFileInfo(path).isAbsolute() || !QFileInfo::exists(path)) {
        error = QStringLiteral("O item não existe ou não tem um caminho absoluto: %1").arg(path); return false;
    }
    const auto native = QDir::toNativeSeparators(path);
    PIDLIST_ABSOLUTE raw = nullptr;
    const auto hr = SHParseDisplayName(reinterpret_cast<LPCWSTR>(native.utf16()), nullptr, &raw, 0, nullptr);
    // SHParseDisplayName / ILCloneFull allocate aligned, independently owned PIDLs.
    pidl.reset(reinterpret_cast<ITEMIDLIST *>(raw));
    if (FAILED(hr)) { error = failure(QStringLiteral("Não foi possível identificar o item no Windows"), hr); return false; }
    return true;
}
}

struct ShellContextMenu::Data {
    // Reverse destruction order keeps the apartment alive until all COM releases.
    Apartment apartment;
    std::vector<Pidl> items;
    ComPtr<IShellFolder> folder;
    ComPtr<IContextMenu> context;
    ComPtr<IContextMenu2> context2;
    ComPtr<IContextMenu3> context3;
    Menu menu;
    QPointer<QWidget> owner;
    HWND hwnd = nullptr;
    QHash<UINT, QPointer<QAction>> actions;
    QSet<HMENU> applicationMenus;
    UINT nextAppId = firstAppId;
    UINT shellCount = 0;
    POINT invocationPoint{};

    static LRESULT CALLBACK menuProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
        auto *self = reinterpret_cast<Data *>(ref);
        const bool applicationPopup = (msg == WM_INITMENUPOPUP && self->applicationMenus.contains(reinterpret_cast<HMENU>(wp))) ||
                                      (msg == WM_MENUCHAR && self->applicationMenus.contains(reinterpret_cast<HMENU>(lp)));
        if (!applicationPopup && (msg == WM_INITMENUPOPUP || msg == WM_MENUCHAR || ((msg == WM_DRAWITEM || msg == WM_MEASUREITEM) && wp == 0))) {
            LRESULT result = 0;
            if (self->context3 && self->context3->HandleMenuMsg2(msg, wp, lp, &result) == S_OK) return result;
            if (!self->context3 && self->context2 && msg != WM_MENUCHAR && self->context2->HandleMenuMsg(msg, wp, lp) == S_OK)
                return msg == WM_INITMENUPOPUP ? 0 : TRUE;
        }
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
    Menu applicationMenu(const QMenu &source, QString &error) {
        Menu result(CreatePopupMenu());
        if (!result) { error = QStringLiteral("Não foi possível criar o submenu do Directory Open."); return {}; }
        applicationMenus.insert(result.get());
        for (auto *action : source.actions()) {
            if (!action->isVisible()) continue;
            if (action->isSeparator()) { if (!AppendMenuW(result.get(), MF_SEPARATOR, 0, nullptr)) return {}; continue; }
            const auto text = action->text();
            UINT flags = MF_STRING | (action->isEnabled() ? MF_ENABLED : MF_GRAYED);
            if (action->isChecked()) flags |= MF_CHECKED;
            if (auto *sub = action->menu()) {
                auto child = applicationMenu(*sub, error); if (!child) return {};
                if (!AppendMenuW(result.get(), flags | MF_POPUP, reinterpret_cast<UINT_PTR>(child.get()), reinterpret_cast<LPCWSTR>(text.utf16()))) return {};
                child.release(); // parent HMENU owns its children
            } else {
                if (nextAppId > lastAppId) { error = QStringLiteral("O menu atingiu o limite de comandos do Windows."); return {}; }
                const auto id = nextAppId++;
                if (!AppendMenuW(result.get(), flags, id, reinterpret_cast<LPCWSTR>(text.utf16()))) return {};
                actions.insert(id, action);
            }
        }
        return result;
    }
};

ShellContextMenu::ShellContextMenu() : m_data(std::make_unique<Data>()) {}
ShellContextMenu::~ShellContextMenu() = default;
std::unique_ptr<ShellContextMenu> ShellContextMenu::create(QWidget *owner, const QStringList &selection,
                                                         const QString &folder, bool extended, QString &error) {
    error.clear();
    auto result = std::unique_ptr<ShellContextMenu>(new ShellContextMenu);
    auto &d = *result->m_data;
    if (FAILED(d.apartment.hr)) { error = failure(QStringLiteral("Não foi possível iniciar o menu do Windows"), d.apartment.hr); return {}; }
    if (!owner) { error = QStringLiteral("A janela do menu não está disponível."); return {}; }
    d.owner = owner->window(); d.hwnd = reinterpret_cast<HWND>(d.owner->winId());
    if (!IsWindow(d.hwnd)) { error = QStringLiteral("A janela não oferece um menu nativo do Windows."); return {}; }
    HRESULT hr;
    if (selection.isEmpty()) {
        Pidl pidl;
        if (!QFileInfo(folder).isDir() || !parse(folder, pidl, error)) {
            if (error.isEmpty()) error = QStringLiteral("A pasta do menu não está disponível."); return {};
        }
        hr = SHBindToObject(nullptr, pidl.get(), nullptr, IID_PPV_ARGS(&d.folder));
        d.items.push_back(std::move(pidl));
        if (SUCCEEDED(hr)) hr = d.folder->CreateViewObject(d.hwnd, IID_PPV_ARGS(&d.context));
    } else {
        std::vector<PCUITEMID_CHILD> children;
        Pidl parentIdentity;
        for (const auto &path : selection) {
            Pidl item;
            if (!parse(path, item, error)) return {};
            Pidl parent(reinterpret_cast<ITEMIDLIST *>(ILCloneFull(item.get())));
            if (!parent || !ILRemoveLastID(parent.get())) { error = QStringLiteral("Não foi possível identificar a pasta do item."); return {}; }
            if (!parentIdentity) parentIdentity = std::move(parent);
            else if (!ILIsEqual(parentIdentity.get(), parent.get())) {
                error = QStringLiteral("O menu do Windows requer itens da mesma pasta. Use os comandos do Directory Open para esta seleção."); return {};
            }
            PCUITEMID_CHILD child = nullptr;
            ComPtr<IShellFolder> parentFolder;
            hr = SHBindToParent(item.get(), IID_PPV_ARGS(&parentFolder), &child);
            if (FAILED(hr)) { error = failure(QStringLiteral("Não foi possível acessar a pasta do item"), hr); return {}; }
            if (!d.folder) d.folder = parentFolder;
            children.push_back(child); d.items.push_back(std::move(item));
        }
        hr = d.folder->GetUIObjectOf(d.hwnd, UINT(children.size()), children.data(), IID_IContextMenu, nullptr, reinterpret_cast<void **>(d.context.GetAddressOf()));
    }
    if (FAILED(hr)) { error = failure(QStringLiteral("O Windows não forneceu o menu desta seleção"), hr); return {}; }
    d.menu.reset(CreatePopupMenu());
    if (!d.menu) { error = QStringLiteral("Não foi possível criar o menu do Windows."); return {}; }
    hr = d.context->QueryContextMenu(d.menu.get(), 0, firstShellId, lastShellId,
                                    CMF_NORMAL | CMF_EXPLORE | CMF_CANRENAME | (extended ? CMF_EXTENDEDVERBS : 0));
    if (FAILED(hr)) { error = failure(QStringLiteral("Não foi possível carregar os comandos do Windows"), hr); return {}; }
    d.shellCount = HRESULT_CODE(hr);
    d.context.As(&d.context3); if (!d.context3) d.context.As(&d.context2);
    return result;
}
quintptr ShellContextMenu::nativeMenu() const { return reinterpret_cast<quintptr>(m_data->menu.get()); }
QString ShellContextMenu::commandVerb(quint32 command) const {
    if (command < firstShellId || command > lastShellId || command - firstShellId >= m_data->shellCount) return {};
    wchar_t wide[512]{};
    if (m_data->context->GetCommandString(command - firstShellId, GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(wide), UINT(std::size(wide))) == S_OK && wide[0]) {
        wide[std::size(wide) - 1] = 0; return QString::fromWCharArray(wide).toLower();
    }
    char ansi[512]{};
    if (m_data->context->GetCommandString(command - firstShellId, GCS_VERBA, nullptr, ansi, UINT(std::size(ansi))) == S_OK) {
        ansi[std::size(ansi) - 1] = 0; return QString::fromLocal8Bit(ansi).toLower();
    }
    return {};
}
bool ShellContextMenu::appendApplicationMenu(const QMenu &menu, QString &error) {
    error.clear();
    auto submenu = m_data->applicationMenu(menu, error);
    if (!submenu || !AppendMenuW(m_data->menu.get(), MF_SEPARATOR, 0, nullptr) ||
        !AppendMenuW(m_data->menu.get(), MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(submenu.get()), L"Directory Open")) {
        if (error.isEmpty()) error = QStringLiteral("Não foi possível acrescentar os comandos do Directory Open."); return false;
    }
    submenu.release(); return true;
}
quint32 ShellContextMenu::track(const QPoint &globalPosition, QString &error) {
    error.clear();
    auto &d = *m_data;
    if (!d.owner || !IsWindow(d.hwnd)) { error = QStringLiteral("A janela do menu foi fechada."); return 0; }
    const auto local = d.owner->mapFromGlobal(globalPosition);
    const auto scale = d.owner->devicePixelRatioF();
    POINT point{qRound(local.x() * scale), qRound(local.y() * scale)};
    if (!ClientToScreen(d.hwnd, &point)) { error = QStringLiteral("Não foi possível posicionar o menu."); return 0; }
    d.invocationPoint = point;
    const auto id = reinterpret_cast<UINT_PTR>(&d);
    if (!SetWindowSubclass(d.hwnd, Data::menuProc, id, reinterpret_cast<DWORD_PTR>(&d))) {
        error = QStringLiteral("Não foi possível conectar os submenus do Windows."); return 0;
    }
    const auto command = TrackPopupMenuEx(d.menu.get(), TPM_RETURNCMD | TPM_RIGHTBUTTON |
        (GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN), point.x, point.y, d.hwnd, nullptr);
    RemoveWindowSubclass(d.hwnd, Data::menuProc, id);
    // Zero means no selection. An extension may leave an unrelated last-error;
    // do not turn Esc into a second (fallback) popup based on that ambient value.
    return UINT(command);
}
ShellMenuResult ShellContextMenu::dispatch(quint32 command, const QHash<QString, QAction *> &routes,
                                         const std::function<void()> &beforeExternal) {
    auto &d = *m_data;
    if (!command) return {};
    if (d.actions.contains(command)) return {d.actions.value(command), false, {}};
    if (command < firstShellId || command > lastShellId || command - firstShellId >= d.shellCount)
        return {{}, false, QStringLiteral("O comando não pertence a este menu.")};
    const auto verb = commandVerb(command);
    if (routes.contains(verb)) return {routes.value(verb), false, {}};
    if (beforeExternal) beforeExternal();
    CMINVOKECOMMANDINFOEX info{}; info.cbSize = sizeof(info);
    info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
    if (GetKeyState(VK_SHIFT) < 0) info.fMask |= CMIC_MASK_SHIFT_DOWN;
    if (GetKeyState(VK_CONTROL) < 0) info.fMask |= CMIC_MASK_CONTROL_DOWN;
    info.hwnd = d.hwnd; info.nShow = SW_SHOWNORMAL; info.ptInvoke = d.invocationPoint;
    info.lpVerb = MAKEINTRESOURCEA(command - firstShellId); info.lpVerbW = MAKEINTRESOURCEW(command - firstShellId);
    const auto hr = d.context->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO *>(&info));
    return {{}, SUCCEEDED(hr), FAILED(hr) ? failure(QStringLiteral("O Windows não conseguiu executar o comando"), hr) : QString{}};
}
