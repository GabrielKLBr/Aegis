// ntsyscalls.cpp - dump the REAL ntdll Nt* syscall table (name -> SSN).
//
// ntdll is mapped into every process, so we just read its export directory in
// memory and parse each Nt* stub. The stub is (optionally an endbr64 for CET,
// then) `mov r10,rcx ; mov eax,<SSN> ; ...` -- the SSN is the imm32 after 0xB8.
// This is the concrete starting point for the "load the real DLLs" path.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

int main() {
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    auto base = reinterpret_cast<uint8_t*>(nt);
    auto dos  = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nth  = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto dir  = nth->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    auto exp  = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
    auto names = reinterpret_cast<uint32_t*>(base + exp->AddressOfNames);
    auto ords  = reinterpret_cast<uint16_t*>(base + exp->AddressOfNameOrdinals);
    auto funcs = reinterpret_cast<uint32_t*>(base + exp->AddressOfFunctions);

    struct Entry { std::string name; uint32_t ssn; };
    std::vector<Entry> entries;
    for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
        const char* name = reinterpret_cast<const char*>(base + names[i]);
        if (!(name[0] == 'N' && name[1] == 't')) continue;
        uint8_t* fn = base + funcs[ords[i]];
        int off = 0;
        if (fn[0] == 0xF3 && fn[1] == 0x0F && fn[2] == 0x1E && fn[3] == 0xFA) off = 4; // endbr64
        if (fn[off] == 0x4C && fn[off+1] == 0x8B && fn[off+2] == 0xD1 && fn[off+3] == 0xB8) {
            uint32_t ssn; std::memcpy(&ssn, fn + off + 4, 4);
            entries.push_back({name, ssn});
        }
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b){ return a.ssn < b.ssn; });

    std::printf("ntdll.dll: %lu exports, %zu Nt* syscall stubs decoded\n",
                (unsigned long)exp->NumberOfNames, entries.size());
    std::printf("first 12 by SSN:\n");
    for (size_t i = 0; i < 12 && i < entries.size(); ++i)
        std::printf("  0x%03X  %s\n", entries[i].ssn, entries[i].name.c_str());

    if (FILE* f = fopen("build\\ntdll_syscalls.txt", "w")) {
        for (auto& e : entries) std::fprintf(f, "0x%03X  %s\n", e.ssn, e.name.c_str());
        std::fclose(f);
        std::printf("wrote full %zu-entry table to build\\ntdll_syscalls.txt\n", entries.size());
    }
    return 0;
}
