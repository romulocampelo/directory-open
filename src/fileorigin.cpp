#include "fileorigin.h"
#include <QDir>
#include <windows.h>

QString readFileOrigin(const QString &archive, std::optional<QByteArray> &origin) {
    const auto native = QDir::toNativeSeparators(archive + ":Zone.Identifier");
    const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return {};
        return "Não foi possível conferir a marca de origem do arquivo (Zone.Identifier).";
    }
    LARGE_INTEGER size{}; QString error;
    if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0 || size.QuadPart > 65536) error = "Marca de origem indisponível ou maior que 64 KiB; operação recusada.";
    else {
        QByteArray bytes(qsizetype(size.QuadPart), Qt::Uninitialized); DWORD count = 0;
        if (!ReadFile(handle, bytes.data(), DWORD(bytes.size()), &count, nullptr) || count != DWORD(bytes.size())) error = "Não foi possível ler a marca de origem completa.";
        else origin = bytes;
    }
    CloseHandle(handle); return error;
}
QString writeFileOrigin(const QString &path, const std::optional<QByteArray> &origin) {
    if (!origin) return {};
    const auto native = QDir::toNativeSeparators(path + ":Zone.Identifier");
    const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return "O destino não permitiu preservar a marca de origem. O arquivo não foi publicado.";
    DWORD written = 0; const bool ok = WriteFile(handle, origin->constData(), DWORD(origin->size()), &written, nullptr) && written == DWORD(origin->size());
    CloseHandle(handle); return ok ? QString{} : QStringLiteral("Falha ao preservar a marca de origem; arquivo não publicado.");
}
