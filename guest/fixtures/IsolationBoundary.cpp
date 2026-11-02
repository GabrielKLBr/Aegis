// Inert SDK-linked guest acceptance fixture. The host owns a sentinel at the
// same path and a loopback listener; neither must be reached through MiniNT.
#include <winsock2.h>
#include <Windows.h>
extern "C" const int AegisIsolationAnchor = 1901;
const void* volatile AegisIsolationRelocation = &AegisIsolationAnchor;

namespace {
void Print(const char* text) {
    DWORD length = 0, written = 0;
    while (text[length]) ++length;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, length, &written, nullptr);
}
[[noreturn]] void Fail(const char* text) { Print(text); ExitProcess(19); }
}
extern "C" void IsolationBoundaryEntry() {
    if (!AegisIsolationRelocation) Fail("ISOLATION_RELOCATION_FAILED\n");
#ifndef ISOLATION_NETWORK_ONLY
    constexpr wchar_t path[] = L"C:\\ProgramData\\AegisIsolation19A\\host-sentinel.txt";
    auto file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
        Fail("ISOLATION_HOST_FILE_VISIBLE\n");
    }
    constexpr const wchar_t* directories[]{L"C:\\ProgramData", L"C:\\ProgramData\\AegisIsolation19A"};
    for (const auto* directory : directories)
        if (!CreateDirectoryW(directory, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            Fail("ISOLATION_VIRTUAL_DIRECTORY_CREATE_FAILED\n");
    file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) Fail("ISOLATION_VIRTUAL_FILE_CREATE_FAILED\n");
    constexpr char data[] = "AEGIS_INERT_GUEST_ONLY\n";
    DWORD written = 0;
    const bool saved = WriteFile(file, data, sizeof(data) - 1, &written, nullptr) &&
                       written == sizeof(data) - 1;
    CloseHandle(file);
    if (!saved) Fail("ISOLATION_VIRTUAL_FILE_WRITE_FAILED\n");
    Print("ISOLATION_GUEST_FILES_PASS\n");
#else
    WSADATA startup{};
    if (WSAStartup(MAKEWORD(2, 2), &startup)) Fail("ISOLATION_WINSOCK_START_FAILED\n");
    const auto socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == INVALID_SOCKET) Fail("ISOLATION_SOCKET_FAILED\n");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = 0x6fb4; // htons(46191), without an extra imported API.
    address.sin_addr.s_addr = 0x0100007f;
    const auto connected = connect(socketHandle, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    const auto error = WSAGetLastError();
    closesocket(socketHandle);
    WSACleanup();
    if (connected != SOCKET_ERROR || error != WSAEACCES) Fail("ISOLATION_NETWORK_NOT_DENIED\n");
    Print("ISOLATION_GUEST_NETWORK_PASS\n");
#endif
    ExitProcess(0);
}
