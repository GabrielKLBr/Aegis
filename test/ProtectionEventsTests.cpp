#include "services/DirectoryChanges.h"
#include "services/Services.h"
#include <cstring>
#include <iostream>
using namespace av::services;
int main() {
    try {
        unsigned tests = 0;
        auto check = [&](bool ok) {
            Check(ok, "Directory notification contract failed");
            ++tests;
        };
        const fs::path root = L"D:\\owned";
        auto wire = [](const std::wstring &name, DWORD action = FILE_ACTION_MODIFIED) {
            std::vector<uint8_t> b(12 + name.size() * 2);
            DWORD n = DWORD(name.size() * 2);
            std::memcpy(b.data() + 4, &action, 4);
            std::memcpy(b.data() + 8, &n, 4);
            std::memcpy(b.data() + 12, name.data(), n);
            return b;
        };
        auto b = wire(L"sub\\ação.txt");
        auto r = DecodeDirectoryChanges(root, b);
        check(!r.rescanRequired && r.changed.size() == 1 && r.changed[0] == root / L"sub\\ação.txt");
        for (DWORD action : {FILE_ACTION_ADDED, FILE_ACTION_RENAMED_NEW_NAME}) {
            auto data = wire(L"x.txt", action);
            check(DecodeDirectoryChanges(root, data).changed.size() == 1);
        }
        for (DWORD action : {FILE_ACTION_REMOVED, FILE_ACTION_RENAMED_OLD_NAME}) {
            auto data = wire(L"x.txt", action);
            auto result = DecodeDirectoryChanges(root, data);
            check(result.changed.empty() && !result.rescanRequired);
        }
        check(DecodeDirectoryChanges(root, {}).rescanRequired);
        for (size_t n : {size_t(1), size_t(11), b.size() - 1})
            check(DecodeDirectoryChanges(root, std::span(b).first(n)).rescanRequired);
        for (const auto *name :
             {L"..\\escape", L"D:\\escape", L"stream:ads", L"sub\\..\\escape", L"trailing.", L"trailing "})
            check(DecodeDirectoryChanges(root, wire(name)).rescanRequired);
        auto bad = b;
        DWORD odd = 3;
        std::memcpy(bad.data() + 8, &odd, 4);
        check(DecodeDirectoryChanges(root, bad).rescanRequired);
        bad = b;
        DWORD shortNext = 4;
        std::memcpy(bad.data(), &shortNext, 4);
        check(DecodeDirectoryChanges(root, bad).rescanRequired);
        auto first = wire(L"first.txt");
        auto second = wire(L"second.txt");
        DWORD offset = DWORD((first.size() + 3) & ~size_t(3));
        first.resize(offset);
        std::memcpy(first.data(), &offset, 4);
        first.insert(first.end(), second.begin(), second.end());
        r = DecodeDirectoryChanges(root, first);
        check(!r.rescanRequired && r.changed.size() == 2);
        Request request;
        request.pathChars = 10;
        request.milliseconds = 5000;
        for (auto op : {Operation::ProtectionStatus, Operation::ProtectionConfigure,
                        Operation::ProtectionSubmit, Operation::ProtectionCancel, Operation::ProtectionEvents,
                        Operation::ProtectionFile, Operation::ProtectionManualFile}) {
            request.operation = op;
            Validate(request);
            request.kind = Kind::Sandbox;
            bool refused = false;
            try {
                Validate(request);
            } catch (...) {
                refused = true;
            }
            check(refused);
            request.kind = Kind::Antivirus;
        }
        std::cout << Json({{"phase", "16"}, {"status", "PASS"}, {"tests", tests}}).dump() << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
