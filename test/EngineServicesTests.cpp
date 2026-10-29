#include "services/Services.h"
#include <iostream>
#include <thread>
#include <vector>
#include <aclapi.h>
#include <sddl.h>

using namespace av::services;
namespace {
template<class F> void Rejected(F action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    Check(rejected, "Malformed protocol value accepted");
}
void Wire() {
    const auto name = L"\\\\.\\pipe\\Aegis.Engine.Test." + std::to_wstring(GetCurrentProcessId());
    Handle server(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | WRITE_DAC | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 4096, 1000, nullptr));
    Check(server.value != INVALID_HANDLE_VALUE, "Test pipe creation");
    const auto sddl = L"D:P(A;;GA;;;SY)(A;;0x12008b;;;" + UserSid() + L")";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    Check(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr) != FALSE, "Test pipe descriptor");
    PACL acl = nullptr; BOOL present = FALSE, inherited = FALSE;
    GetSecurityDescriptorDacl(descriptor, &present, &acl, &inherited);
    const auto security = SetSecurityInfo(server.value, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr, nullptr, acl, nullptr);
    LocalFree(descriptor); Check(security == ERROR_SUCCESS, "Test pipe client-only ACL");
    static_assert((PipeClientAccess & FILE_CREATE_PIPE_INSTANCE) == 0);
    Handle denied(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 4096, 1000, nullptr));
    const auto deniedError = GetLastError();
    Check(denied.value == INVALID_HANDLE_VALUE && (deniedError == ERROR_ACCESS_DENIED || deniedError == ERROR_PIPE_BUSY), "Client was granted pipe-instance creation rights");
    Handle client(CreateFileW(name.c_str(), PipeClientAccess, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
    Check(client.value != INVALID_HANDLE_VALUE, "Test pipe connection");
    ULONG pid = 0;
    Check(GetNamedPipeServerProcessId(client.value, &pid) && pid == GetCurrentProcessId(), "Pipe server identity unavailable with client-only rights");
    Connect(server.value, GetTickCount64() + 2000);
    const Json expected = {{"document", std::string(131072, 'a')}, {"complete", true}};
    std::exception_ptr failure;
    std::jthread writer([&] { try { Send(server.value, FrameType::Result, expected, GetTickCount64() + 3000); } catch (...) { failure = std::current_exception(); } });
    Frame frame;
    Transfer(client.value, false, &frame, sizeof(frame), GetTickCount64() + 3000);
    std::string text(frame.bytes, '\0');
    Transfer(client.value, false, text.data(), frame.bytes, GetTickCount64() + 3000);
    writer.join(); if (failure) std::rethrow_exception(failure);
    Check(frame.type == FrameType::Result && Json::parse(text) == expected, "Partial pipe transfers corrupted the result");
    Handle stop(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    const auto start = GetTickCount64();
    std::jthread cancel([&] { Sleep(50); SetEvent(stop.value); });
    Rejected([&] { unsigned char byte = 0; Transfer(client.value, false, &byte, 1, GetTickCount64() + 10000, stop.value); });
    cancel.join(); Check(GetTickCount64() - start < 2000, "Pending pipe read did not cancel promptly");
    Rejected([&] { Send(server.value, FrameType::Result, {{"oversize", std::string(MaxFrame, 'x')}}, GetTickCount64() + 1000); });
    const Json terminal = {{"error", "The caller must receive this before disconnect"}};
    std::jthread completion([&] {
        try {
            Send(server.value, FrameType::Error, terminal, GetTickCount64() + 2000);
            FinishConnection(server.value);
            DisconnectNamedPipe(server.value);
        } catch (...) { failure = std::current_exception(); }
    });
    Sleep(50); // A small reply fits in the pipe before the reader is scheduled.
    Transfer(client.value, false, &frame, sizeof(frame), GetTickCount64() + 2000);
    text.assign(frame.bytes, '\0');
    Transfer(client.value, false, text.data(), frame.bytes, GetTickCount64() + 2000);
    Check(frame.type == FrameType::Error && Json::parse(text) == terminal, "Closing the connection discarded the terminal reply");
    CloseHandle(client.release());
    completion.join(); if (failure) std::rethrow_exception(failure);
}
}
int main() {
    try {
        Request request; request.pathChars = 1; Validate(request);
        for (unsigned i = 0; i < 7; ++i) {
            auto bad = request;
            switch (i) {
            case 0: bad.magic = 0; break;
            case 1: bad.version = 2; break;
            case 2: bad.pathChars = 0; break;
            case 3: bad.pathChars = 32768; break;
            case 4: bad.milliseconds = 99; break;
            case 5: bad.milliseconds = 3600001; break;
            case 6: bad.operation = Operation::Detonate; break;
            }
            Rejected([&] { Validate(bad); });
        }
        request.kind = Kind::Sandbox; request.operation = Operation::Detonate;
        Rejected([&] { Validate(request); });
        request.milliseconds = 60000; Validate(request);
        request.operation = Operation::DesktopBundle; Validate(request);
        request.operation = Operation::Scan; Rejected([&] { Validate(request); });
        const std::wstring unicode = L"C:\\Análises\\teste-\U0001F9EA.exe";
        Check(Wide(Text(unicode)) == unicode, "UTF-16/UTF-8 path roundtrip");
        Rejected([] { Text(std::wstring(1, wchar_t(0xD800))); });
        Rejected([] { Wide(std::string("\xff")); });
        Wire();
        std::cout << "Engine services protocol PASS\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
