// WinLoader.cpp - our own recursive Windows image loader.
#include "WinLoader.h"

#include <windows.h>
#include <cctype>
#include <cstring>
#include <fstream>

namespace av {
namespace microvm {

namespace {
std::string Lower(std::string s) {
    for (auto& ch : s) ch = (char)std::tolower((unsigned char)ch);
    return s;
}
const IMAGE_NT_HEADERS* NtHdr(const std::vector<uint8_t>& f) {
    if (f.size() < sizeof(IMAGE_DOS_HEADER)) return nullptr;
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(f.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    if ((size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS) > f.size()) return nullptr;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(f.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return nullptr;
    return nt;
}
uint32_t RvaToOff(const std::vector<uint8_t>& f, uint32_t rva) {
    auto nt = NtHdr(f); if (!nt) return 0;
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        uint32_t va = sec[i].VirtualAddress;
        uint32_t vs = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
        if (rva >= va && rva < va + vs) {
            uint32_t off = sec[i].PointerToRawData + (rva - va);
            return off < f.size() ? off : 0;
        }
    }
    // RVA inside the headers themselves.
    return rva < nt->OptionalHeader.SizeOfHeaders ? rva : 0;
}
std::string CStrAt(const std::vector<uint8_t>& f, uint32_t off) {
    std::string s;
    for (uint32_t i = off; i && i < f.size() && f[i]; ++i) s.push_back((char)f[i]);
    return s;
}
// API set names (api-ms-win-*, ext-ms-win-*) aren't real files; route them to the
// host DLLs that actually back them. We try the candidates in order.
std::vector<std::string> ApiSetCandidates(const std::string& lower) {
    if (lower.rfind("api-ms-win-", 0) == 0 || lower.rfind("ext-ms-win-", 0) == 0)
        return { "kernelbase.dll", "ntdll.dll", "kernel32.dll" };
    return { lower };
}
} // namespace

void WinLoader::Provide(const std::string& name, std::vector<uint8_t> bytes) {
    provided_[Lower(name)] = std::move(bytes);
}

std::vector<uint8_t> WinLoader::FindFile(const std::string& name) {
    auto it = provided_.find(Lower(name));
    if (it != provided_.end()) return it->second;
    std::wstring path = systemDir_ + L"\\";
    for (char c : name) path.push_back((wchar_t)c);
    std::ifstream fs(path.c_str(), std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(fs)),
                                 std::istreambuf_iterator<char>());
}

uint64_t WinLoader::Place(const std::vector<uint8_t>& file, Module& m) {
    auto nt = NtHdr(file);
    if (!nt) { error_ = "bad PE: " + m.name; return 0; }
    uint64_t sizeOfImage   = nt->OptionalHeader.SizeOfImage;
    uint64_t sizeOfHeaders = nt->OptionalHeader.SizeOfHeaders;
    uint64_t preferred     = nt->OptionalHeader.ImageBase;

    uint64_t base = (regionNext_ + 0xFFFF) & ~0xFFFFull;   // 64 KiB alignment
    if (base + sizeOfImage > regionEnd_) { error_ = "out of load region: " + m.name; return 0; }
    regionNext_ = base + ((sizeOfImage + 0xFFF) & ~0xFFFull);

    auto* g = static_cast<uint8_t*>(vm_.RamData());
    if (base + sizeOfImage > vm_.RamSize()) { error_ = "image past RAM: " + m.name; return 0; }
    ZeroMemory(g + base, (size_t)sizeOfImage);
    CopyMemory(g + base, file.data(), (size_t)(sizeOfHeaders < file.size() ? sizeOfHeaders : file.size()));

    auto sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        uint64_t va = sec[i].VirtualAddress, raw = sec[i].PointerToRawData;
        uint64_t rsz = sec[i].SizeOfRawData;
        uint64_t vsz = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : rsz;
        uint64_t copy = rsz < vsz ? rsz : vsz;
        if (raw + copy > file.size()) copy = raw < file.size() ? file.size() - raw : 0;
        if (copy && base + va + copy <= vm_.RamSize())
            CopyMemory(g + base + va, file.data() + raw, (size_t)copy);
    }
    // DIR64 base relocations for the load delta.
    int64_t delta = (int64_t)base - (int64_t)preferred;
    auto& rel = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (delta && rel.Size && rel.VirtualAddress) {
        uint64_t p = rel.VirtualAddress, end = p + rel.Size;
        while (p + 8 <= end && base + p + 8 <= vm_.RamSize()) {
            uint32_t pageRva, blockSize;
            CopyMemory(&pageRva,   g + base + p + 0, 4);
            CopyMemory(&blockSize, g + base + p + 4, 4);
            if (blockSize < 8 || p + blockSize > end) break;
            uint32_t count = (blockSize - 8) / 2;
            for (uint32_t k = 0; k < count; ++k) {
                uint16_t e; CopyMemory(&e, g + base + p + 8 + (uint64_t)k * 2, 2);
                if ((e >> 12) == IMAGE_REL_BASED_DIR64) {
                    uint64_t loc = base + pageRva + (e & 0xFFF);
                    if (loc + 8 <= vm_.RamSize()) {
                        uint64_t v; CopyMemory(&v, g + loc, 8);
                        v += delta; CopyMemory(g + loc, &v, 8);
                    }
                }
            }
            p += blockSize;
        }
    }
    m.base = base; m.size = (uint32_t)sizeOfImage;
    m.entry = nt->OptionalHeader.AddressOfEntryPoint ? base + nt->OptionalHeader.AddressOfEntryPoint : 0;
    return base;
}

uint64_t WinLoader::ExportAddr(Module& m, const std::string& fn, uint16_t ordinal,
                               bool byOrdinal, int depth) {
    if (depth > 8) return 0;
    auto nt = NtHdr(m.file); if (!nt) return 0;
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress || !dir.Size) return 0;
    uint32_t eoff = RvaToOff(m.file, dir.VirtualAddress);
    if (!eoff) return 0;
    auto exp = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(m.file.data() + eoff);
    auto funcs = reinterpret_cast<const uint32_t*>(m.file.data() + RvaToOff(m.file, exp->AddressOfFunctions));

    uint32_t funcRva = 0;
    if (byOrdinal) {
        uint32_t idx = ordinal - exp->Base;
        if (idx < exp->NumberOfFunctions) funcRva = funcs[idx];
    } else {
        auto names = reinterpret_cast<const uint32_t*>(m.file.data() + RvaToOff(m.file, exp->AddressOfNames));
        auto ords  = reinterpret_cast<const uint16_t*>(m.file.data() + RvaToOff(m.file, exp->AddressOfNameOrdinals));
        for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
            if (CStrAt(m.file, RvaToOff(m.file, names[i])) == fn) { funcRva = funcs[ords[i]]; break; }
        }
    }
    if (!funcRva) return 0;
    // Forwarder: funcRva points inside the export directory -> "OTHERDLL.Func".
    if (funcRva >= dir.VirtualAddress && funcRva < dir.VirtualAddress + dir.Size) {
        std::string fwd = CStrAt(m.file, RvaToOff(m.file, funcRva));
        auto dot = fwd.find('.');
        if (dot == std::string::npos) return 0;
        std::string dll = fwd.substr(0, dot) + ".dll";
        std::string sym = fwd.substr(dot + 1);
        if (!sym.empty() && sym[0] == '#')                      // forward by ordinal
            return ResolveImport(dll, "", (uint16_t)std::atoi(sym.c_str() + 1), true);
        return ResolveImport(dll, sym, 0, false);
    }
    return m.base + funcRva;
}

uint64_t WinLoader::ResolveImport(const std::string& dll, const std::string& fn,
                                  uint16_t ordinal, bool byOrdinal) {
    for (const auto& cand : ApiSetCandidates(Lower(dll))) {
        uint64_t base = Load(cand);
        if (!base) continue;
        uint64_t a = ExportAddr(mods_[Lower(cand)], fn, ordinal, byOrdinal, 0);
        if (a) return a;
    }
    return 0;
}

bool WinLoader::ProcessImports(Module& m) {
    auto nt = NtHdr(m.file); if (!nt) return false;
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress || !dir.Size) return true;   // no imports
    auto* g = static_cast<uint8_t*>(vm_.RamData());

    for (uint32_t d = 0; ; d += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        uint32_t doff = RvaToOff(m.file, dir.VirtualAddress + d);
        if (!doff || doff + sizeof(IMAGE_IMPORT_DESCRIPTOR) > m.file.size()) break;
        auto desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(m.file.data() + doff);
        if (desc->Name == 0 && desc->FirstThunk == 0) break;
        std::string dll = CStrAt(m.file, RvaToOff(m.file, desc->Name));
        uint32_t intRva = desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk;
        uint32_t iatRva = desc->FirstThunk;

        for (uint32_t i = 0; ; ++i) {
            uint32_t toff = RvaToOff(m.file, intRva + i * 8);
            if (!toff || toff + 8 > m.file.size()) break;
            uint64_t thunk; CopyMemory(&thunk, m.file.data() + toff, 8);
            if (!thunk) break;

            uint64_t addr = 0;
            if (thunk & 0x8000000000000000ull) {              // import by ordinal
                addr = ResolveImport(dll, "", (uint16_t)(thunk & 0xFFFF), true);
            } else {
                std::string fn = CStrAt(m.file, RvaToOff(m.file, (uint32_t)thunk) + 2); // skip hint
                addr = ResolveImport(dll, fn, 0, false);
            }
            uint64_t slot = m.base + iatRva + (uint64_t)i * 8;
            if (slot + 8 <= vm_.RamSize()) CopyMemory(g + slot, &addr, 8);
            if (addr) ++resolved_;
            else {
                ++missing_;
                std::string fn = (thunk & 0x8000000000000000ull)
                    ? ("#" + std::to_string(thunk & 0xFFFF))
                    : CStrAt(m.file, RvaToOff(m.file, (uint32_t)thunk) + 2);
                if (missingNames_.size() < 40) missingNames_.push_back(dll + "!" + fn);
            }
        }
    }
    return true;
}

uint64_t WinLoader::Load(const std::string& name) {
    std::string key = Lower(name);
    if (key.find(".dll") == std::string::npos && key.find('.') == std::string::npos) key += ".dll";
    auto it = mods_.find(key);
    if (it != mods_.end()) return it->second.base;        // already loaded

    std::vector<uint8_t> file = FindFile(key);
    if (file.size() < 0x40) { error_ = "not found: " + key; return 0; }

    // Insert the module BEFORE recursing so cyclic imports terminate.
    Module& m = mods_[key];
    m.name = key; m.file = file;
    if (!Place(file, m)) { mods_.erase(key); return 0; }

    ProcessImports(m);
    m.linked = true;
    return m.base;
}

uint64_t WinLoader::Resolve(const std::string& dll, const std::string& fn) {
    uint64_t base = Load(dll);
    if (!base) return 0;
    return ExportAddr(mods_[Lower(dll)], fn, 0, false, 0);
}

int ApplyWinePatches(MicroVm& vm, WinLoader& ld) {
    auto* g = static_cast<uint8_t*>(vm.RamData());
    int n = 0;
    auto write = [&](const char* fn, const uint8_t* stub, size_t len) {
        uint64_t a = ld.Resolve("ntdll.dll", fn);
        if (a && a + len <= vm.RamSize()) { CopyMemory(g + a, stub, len); ++n; }
    };
    // Heap -> `mov eax,<op>; syscall; ret` (MiniNt's GuestHeap services it).
    auto heap = [&](const char* fn, uint32_t op) {
        uint8_t s[8] = { 0xB8, (uint8_t)op,(uint8_t)(op>>8),(uint8_t)(op>>16),(uint8_t)(op>>24), 0x0F,0x05, 0xC3 };
        write(fn, s, sizeof s);
    };
    heap("RtlAllocateHeap",   0xF0000001);
    heap("RtlFreeHeap",       0xF0000002);
    heap("RtlCreateHeap",     0xF0000003);
    heap("RtlReAllocateHeap", 0xF0000004);
    heap("RtlSizeHeap",       0xF0000005);
    // Locks -> no-op. `xor eax,eax; ret` (NTSTATUS/void = 0); Try* = TRUE.
    const uint8_t ret0[3] = { 0x31, 0xC0, 0xC3 };          // xor eax,eax; ret
    const uint8_t retTrue[3] = { 0xB0, 0x01, 0xC3 };       // mov al,1; ret
    for (const char* fn : { "RtlEnterCriticalSection", "RtlLeaveCriticalSection",
            "RtlInitializeCriticalSection", "RtlInitializeCriticalSectionEx",
            "RtlDeleteCriticalSection", "RtlAcquireSRWLockExclusive",
            "RtlAcquireSRWLockShared", "RtlReleaseSRWLockExclusive",
            "RtlReleaseSRWLockShared", "RtlAcquirePebLock", "RtlReleasePebLock" })
        write(fn, ret0, sizeof ret0);
    for (const char* fn : { "RtlTryAcquireSRWLockExclusive", "RtlTryAcquireSRWLockShared" })
        write(fn, retTrue, sizeof retTrue);
    return n;
}

std::vector<std::string> WinLoader::Exports(const std::string& dll) {
    std::vector<std::string> out;
    if (!Load(dll)) return out;
    Module& m = mods_[Lower(dll)];
    auto nt = NtHdr(m.file); if (!nt) return out;
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress) return out;
    auto exp = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(m.file.data() + RvaToOff(m.file, dir.VirtualAddress));
    auto names = reinterpret_cast<const uint32_t*>(m.file.data() + RvaToOff(m.file, exp->AddressOfNames));
    for (DWORD i = 0; i < exp->NumberOfNames; ++i)
        out.push_back(CStrAt(m.file, RvaToOff(m.file, names[i])));
    return out;
}

} // namespace microvm
} // namespace av
