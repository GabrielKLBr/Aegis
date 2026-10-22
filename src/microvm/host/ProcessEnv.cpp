// ProcessEnv.cpp - implementation of the TEB/PEB/parameters builder.
#include "ProcessEnv.h"

#include <windows.h>

namespace av {
namespace microvm {

ProcessEnv BuildProcessEnv(MicroVm& vm, uint64_t gpaBase, uint64_t imageBase,
                           uint64_t stackBase, uint64_t stackLimit,
                           uint32_t pid, uint32_t tid, const std::wstring& commandLine) {
    uint8_t* g = static_cast<uint8_t*>(vm.RamData());
    auto wr8  = [&](uint64_t a, uint8_t v)  { g[a] = v; };
    auto wr16 = [&](uint64_t a, uint16_t v) { CopyMemory(g + a, &v, 2); };
    auto wr64 = [&](uint64_t a, uint64_t v) { CopyMemory(g + a, &v, 8); };

    ProcessEnv e;
    e.teb    = gpaBase + 0x0000;
    e.peb    = gpaBase + 0x2000;
    e.params = gpaBase + 0x3000;
    uint64_t cmdBuf = gpaBase + 0x4000;
    ZeroMemory(g + gpaBase, 0x5000);

    // ---- TEB (NT_TIB + thread fields), x64 offsets ----
    wr64(e.teb + 0x08, stackBase);     // NT_TIB.StackBase
    wr64(e.teb + 0x10, stackLimit);    // NT_TIB.StackLimit
    wr64(e.teb + 0x30, e.teb);         // NT_TIB.Self  (GS:[0x30])
    wr64(e.teb + 0x40, pid);           // ClientId.UniqueProcess
    wr64(e.teb + 0x48, tid);           // ClientId.UniqueThread
    wr64(e.teb + 0x60, e.peb);         // ProcessEnvironmentBlock (GS:[0x60])

    // ---- PEB, x64 offsets ----
    wr8 (e.peb + 0x02, 0);             // BeingDebugged = FALSE (anti-debug reads this)
    wr64(e.peb + 0x10, imageBase);     // ImageBaseAddress
    wr64(e.peb + 0x18, 0);             // Ldr (module list) - not yet
    wr64(e.peb + 0x20, e.params);      // ProcessParameters

    // ---- RTL_USER_PROCESS_PARAMETERS: CommandLine UNICODE_STRING @ 0x70 ----
    uint16_t byteLen = static_cast<uint16_t>(commandLine.size() * 2);
    for (size_t i = 0; i < commandLine.size(); ++i)
        wr16(cmdBuf + i * 2, static_cast<uint16_t>(commandLine[i]));
    wr16(cmdBuf + commandLine.size() * 2, 0);
    wr16(e.params + 0x70, byteLen);            // Length
    wr16(e.params + 0x72, (uint16_t)(byteLen + 2));  // MaximumLength
    wr64(e.params + 0x78, cmdBuf);             // Buffer

    return e;
}

void BuildLoaderData(MicroVm& vm, const ProcessEnv& env, uint64_t gpaBase,
                     const std::vector<ModuleSpec>& modules) {
    uint8_t* g = static_cast<uint8_t*>(vm.RamData());
    auto wr8  = [&](uint64_t a, uint8_t v)  { g[a] = v; };
    auto wr16 = [&](uint64_t a, uint16_t v) { CopyMemory(g + a, &v, 2); };
    auto wr32 = [&](uint64_t a, uint32_t v) { CopyMemory(g + a, &v, 4); };
    auto wr64 = [&](uint64_t a, uint64_t v) { CopyMemory(g + a, &v, 8); };

    const uint64_t ldr = gpaBase;
    const size_t   n   = modules.size();
    ZeroMemory(g + gpaBase, 0x200 + n * 0x200);

    // PEB_LDR_DATA
    wr32(ldr + 0x00, 0x58);   // Length
    wr8 (ldr + 0x04, 1);      // Initialized
    const uint64_t inLoadHead = ldr + 0x10;   // InLoadOrderModuleList
    const uint64_t inMemHead  = ldr + 0x20;   // InMemoryOrderModuleList
    const uint64_t inInitHead = ldr + 0x30;   // InInitializationOrderModuleList

    std::vector<uint64_t> ent(n);
    for (size_t i = 0; i < n; ++i) ent[i] = gpaBase + 0x200 + i * 0x200;

    for (size_t i = 0; i < n; ++i) {
        uint64_t e = ent[i];
        uint64_t nameBuf = e + 0x100;
        wr64(e + 0x30, modules[i].base);   // DllBase
        wr64(e + 0x38, 0);                 // EntryPoint
        wr32(e + 0x40, modules[i].size);   // SizeOfImage
        uint16_t bl = (uint16_t)(modules[i].name.size() * 2);
        for (size_t c = 0; c < modules[i].name.size(); ++c)
            wr16(nameBuf + c * 2, (uint16_t)modules[i].name[c]);
        wr16(e + 0x48, bl); wr16(e + 0x4A, (uint16_t)(bl + 2)); wr64(e + 0x50, nameBuf); // FullDllName
        wr16(e + 0x58, bl); wr16(e + 0x5A, (uint16_t)(bl + 2)); wr64(e + 0x60, nameBuf); // BaseDllName
    }

    // Link a circular LIST_ENTRY chain whose node link field is at ent[i]+off
    // (Flink at +0x00, Blink at +0x08).
    auto link = [&](uint64_t head, uint64_t off) {
        if (n == 0) { wr64(head, head); wr64(head + 8, head); return; }
        wr64(head, ent[0] + off);                                       // head.Flink
        for (size_t i = 0; i < n; ++i)
            wr64(ent[i] + off, (i + 1 < n) ? ent[i + 1] + off : head);  // node.Flink
        wr64(head + 8, ent[n - 1] + off);                              // head.Blink
        wr64(ent[0] + off + 8, head);                                  // node0.Blink
        for (size_t i = 1; i < n; ++i)
            wr64(ent[i] + off + 8, ent[i - 1] + off);                  // node.Blink
    };
    link(inLoadHead, 0x00);   // InLoadOrderLinks  @ entry+0x00
    link(inMemHead,  0x10);   // InMemoryOrderLinks @ entry+0x10
    wr64(inInitHead, inInitHead); wr64(inInitHead + 8, inInitHead);  // empty

    wr64(env.peb + 0x18, ldr);   // PEB->Ldr
}

} // namespace microvm
} // namespace av
