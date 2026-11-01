# GuardianAV — MicroVM Sandbox (code name: **Carcarcaria / MDE — Micro-Detonation Engine**)

Technical design of a dynamic analysis sandbox for Windows that **not** is a traditional VM (without QEMU/VirtualBox/VMware/Hyper-V Manager as a product), but uses a minimum hypervisor (MicroVM)** only as *hardware isolation step*. The bulk of virtualization (files, registration, processes, objects, network) is made by a layer of light compatibility over a minimum guest, with differential VHD disk.

> ** Honest decision number 1 (read first):** In practice, the "minimum hypervisor"
> in Windows = **WHP (Windows Hypervisor Platform)**, the public API on the
> Windows hypervisor (the same as Hyper-V uses underneath). This is how QEMU
> (WHPX accelerator), Android Emulator and VMware 17 run on Windows. You DO NOT use
> Hyper-V Manager nor Windows Sandbox as a product — you write your own
> VMM, its device models and its guest — but the *primitive virtualization of CPU/EPT*
> comes from the platform hypervisor. The only alternative is to write your own driver
> VT-x/AMD-V (large, requires WHQL signature, conflict with any other hypervisor).
> Logo: **the feature "Windows Hypervisor Platform" / virtualization needs to be connected**
> — is exactly what is off on your machine today (`HypervisorPresent=False`).

---

## 1. Components of architecture

Layers, from bottom to top:

```
┌──────────────────────────────────────────────────────────────────────┐
│ L4  UI / Integração GuardianAV (avui "Sandbox" → engine "MicroVM")     │  user-mode
├──────────────────────────────────────────────────────────────────────┤
│ L3  Host Control Plane  (gde-host.exe, serviço/processo user-mode)     │  user-mode
│     HostController · ImageManager · SnapshotManager · VirtualNetwork   │
│     IntrospectionEngine(VMI) · BehaviorRecorder · ReportBuilder        │
├──────────────────────────────────────────────────────────────────────┤
│ L2  MicroVM Monitor (VMM)  — WHP  (sem driver de kernel próprio)       │  user-mode
│     Partition · vCPU loop · SLAT/EPT map · Intercept dispatch          │
│     DeviceModel: PvDisk · PvNet · PvChannel · PvConsole · PvPlatform   │
├──────────────────────────────────────────────────────────────────────┤
│ L1  Hypervisor da plataforma (Windows hypervisor via WHP)  [Microsoft] │  ring -1
│     EPT/SLAT · VT-x/AMD-V · isolamento de memória física do host       │
├──────────────────────────────────────────────────────────────────────┤
│ L0  Guest mínimo (imagem por perfil Win7/10/11) em VHD diferencial     │  guest
│     GuestBootShim · GuestAgent(user) · [opcional] drivers de telemetria│
│     FsFilter(minifilter) · RegMon · NetMon · ProcMon                   │
└──────────────────────────────────────────────────────────────────────┘
```

**Responsibilities:**

| Component | Layer | Liability |
|---|---|---|
| `HostController` | L3 user | Orchestra life cycle, profiles, politics, aggregates report |
| `ImageManager` | L3 user | Bases VHD by profile; creates VHD differential (CoW); monta/discards (API VirtDisk) |
| `SnapshotManager` | L3 user | Differential disk snapshot + RAM/vCPU snapshot; fast restore |
| `VirtualNetwork` | L3 user | Switch virtual host-side; sinkhole DNS / INetSim / catch pcap; without NIC real |
| `IntrospectionEngine` (VMI) | L3 user | Reads guest memory via WHP/EPT; observes without hooks in guest (anti-evasion) |
| `BehaviorRecorder` | L3 user | Funding events (VMI + agent + device layer) one timeline |
| `ReportBuilder` | L3 user | Genera ChangeSet (FS/log diff) + JSON/HTML report |
| `MicroVmMonitor` (VMM) | L2 user | Create partition WHP, map memory (SLAT), vCPUs wheel, dispatch VM-exits |
| `DeviceModel` + Pv* | L2 user | Disco/NIC/canal/console **paravirtual**; denies real hardware |
| `GuestAgent` | L0 guest user | Receives the sample by PvChannel, executes, simulates user, sends telemetry |
| `FsFilter/RegMon/NetMon/ProcMon` | L0 guest kernel | Telemetry semantics fina (optional; replaceable by VMI) |

---

## 2. Execution flow

```
1.  UI: usuário escolhe amostra + perfil (ex.: Win10, rede=sinkhole, timeout=60s)
2.  ImageManager: pega base.vhdx (RO, perfil) → cria diff.vhdx (RW, CoW)         [≈ instantâneo, ~MBs]
3.  SnapshotManager: restaura snapshot "booted" (RAM + vCPU + diff de disco)      [boot sub-segundo]
4.  MicroVmMonitor: WHvCreatePartition → mapeia RAM (SLAT) → cria vCPUs
                    → conecta DeviceModels (PvDisk→diff.vhdx, PvNet→VirtualNetwork, PvChannel)
5.  Guest resume → GuestAgent pronto (handshake pelo PvChannel)
6.  Host → GuestAgent: transfere a amostra (ring buffer do PvChannel) → grava em C:\ (= diff.vhdx)
7.  GuestAgent: lança a amostra; opcional: simula mouse/teclado p/ disparar payloads
8.  Detonação. Telemetria em paralelo:
       (a) device layer (VMM) loga TODO I/O de disco/rede  ← impossível de esconder
       (b) VMI (host) lê lista de processos/regiões de memória do guest
       (c) [opcional] callbacks de kernel + ETW no guest → contexto semântico rico
    MEMZ grava MBR → vai pro PvDisk → setor 0 do diff.vhdx → evento "raw MBR write" (flag vermelha)
9.  Timeout/fim → para vCPUs
10. ReportBuilder: ChangeSet = diff de blocos do VHD + diff de hives de registro + pcap
11. Teardown: descarta diff.vhdx + RAM → ambiente sumiu (descartável). base.vhdx intacta.
```

---

## 3. Data structures (core)

```cpp
enum class OsProfile { Win7, Win10, Win11 };
enum class NetMode   { Blackhole, Sinkhole /*INetSim-like*/, Capture, NatLimited };

struct SandboxProfile {
    OsProfile     os            = OsProfile::Win10;
    std::wstring  baseVhdPath;                 // imagem RO do perfil
    uint32_t      vcpus         = 1;
    uint64_t      ramBytes      = 1ull<<30;    // 1 Typical GiB; minimum possible
    NetMode       net           = NetMode::Sinkhole;
    uint32_t      timeoutMs     = 60'000;
    uint8_t       evasionLevel  = 2;           // 0..3 (quanto de anti-fingerprint)
    bool          useGuestDrivers = false;     // false = VMI only (more stealthy)
};

// Differential VHD chain (copy-on-write at block level).
struct DiffDisk { std::wstring parent; std::wstring child; uint64_t virtualSize; };

// Memory of guest exposed to SLAT/EPT.
struct GuestRam { uint64_t gpaBase; uint64_t size; void* hostVa; uint32_t flags; };

// Result of WHvRunVirtualProcessor.
struct VmExit {
    enum Reason { Mmio, PortIo, Cpuid, Msr, Hlt, InterruptWindow, Unrecoverable } reason;
    uint64_t   gpa;          // p/ Mmio
    uint16_t   port;         // p/ PortIo
    bool       isWrite;
    uint64_t   value;        // dado lido/escrito
    WHV_VP_EXIT_CONTEXT raw; // full CPU context
};

// virtio-like queue p/ device models paravirtuais
struct VirtQueue { uint64_t descGpa, availGpa, usedGpa; uint16_t size, lastSeenUsed; };

// Behavior events (single timeline, ordered by ts).
struct Actor { uint32_t pid; uint32_t tid; std::wstring image; };
struct FileEvent     { uint64_t ts; Actor by; std::wstring action, path; uint64_t bytes; };
struct RegistryEvent { uint64_t ts; Actor by; std::wstring action, key, value, data; };
struct ProcessEvent  { uint64_t ts; Actor by; std::wstring action; uint32_t childPid; };
struct NetworkEvent  { uint64_t ts; Actor by; std::wstring proto, remote; uint16_t port; uint64_t bytes; };
struct DeviceEvent   { uint64_t ts; std::wstring device, op; uint64_t lba; uint32_t len; bool denied; }; // ex.: MBR write
struct BehaviorTimeline { std::vector<std::variant<FileEvent,RegistryEvent,ProcessEvent,NetworkEvent,DeviceEvent>> events; };

struct Snapshot { std::wstring vhdDiff; std::wstring ramImage; std::wstring vcpuState; };
struct ChangeSet {                              // the "before vs after"
    std::vector<std::wstring> filesCreated, filesModified, filesDeleted;
    std::vector<RegistryEvent> registryDelta;
    std::vector<NetworkEvent>  networkDelta;
    bool mbrTouched = false; bool bootConfigTouched = false;
};
```

---

## 4. Main classes and interfaces

```cpp
// ---- L2: the heart of MicroVM ----
class IDeviceModel {                       // todos os dispositivos paravirtuais
public:
    virtual ~IDeviceModel() = default;
    virtual bool Handles(uint64_t gpa, uint16_t port) const = 0;
    virtual void OnMmio(VmExit&) = 0;
    virtual void OnPortIo(VmExit&) = 0;
    virtual void Tick() = 0;               // DMA virtual / processa filas virtqueue
};

class PvDisk  : public IDeviceModel { DiffDisk disk_; /* lê/grava blocos no diff.vhdx */ };
class PvNet   : public IDeviceModel { VirtualNetwork* net_; };
class PvChannel: public IDeviceModel { /* ring host<->GuestAgent (vsock-like) */ };
class PvConsole: public IDeviceModel { /* framebuffer p/ screenshots do desktop */ };

class MicroVmMonitor {
public:
    bool Create(const SandboxProfile&);    // WHvCreatePartition/SetupPartition
    bool MapRam(const GuestRam&);          // WHvMapGpaRange  (SLAT: isola host)
    void Attach(IDeviceModel*);
    int  RunVcpu(uint32_t idx);            // loop: WHvRunVirtualProcessor → dispatch(VmExit)
    void Pause(); void Resume(); void Stop();
private:
    void Dispatch(VmExit&);                // roteia MMIO/PIO p/ device model; CPUID/MSR → masking
    WHV_PARTITION_HANDLE part_{};
    std::vector<IDeviceModel*> devices_;
};

// ---- L3: control plane ----
class ImageManager {
public:
    DiffDisk CreateEphemeralDisk(const SandboxProfile&);   // VirtDisk: CreateVirtualDisk c/ ParentPath
    void     Discard(const DiffDisk&);                     // deletes your child → disposable
};

class SnapshotManager {
public:
    Snapshot CaptureBooted(MicroVmMonitor&, const DiffDisk&);  // RAM+vCPU+disk in "ready" state
    void     Restore(MicroVmMonitor&, const Snapshot&);        // Quick boot
};

class IntrospectionEngine {                // VMI — observa o guest "de fora"
public:
    void Bind(MicroVmMonitor&, OsProfile);                  // kernel offset profiles by version
    std::vector<ProcessEvent> PollProcessList();           // is on the list of EPROCESS via EPT
    std::vector<std::pair<uint64_t,std::wstring>> SuspiciousRegions(); // RWX, hollowing, etc.
};

class BehaviorRecorder { public: void Ingest(/*evento*/); BehaviorTimeline Build(); };
class ReportBuilder    { public: ChangeSet Diff(const DiffDisk&, OsProfile); std::wstring ToJson(...); };

class HostController {                      // facade used by UI
public:
    SandboxReportV2 Detonate(const std::wstring& sample, const SandboxProfile&);
};
```

---

## 5. Strategy — virtualization of archives**

- **Required MEMZ-proof basic mechanism**Guest volume is a **VHDX
  differential**. `base.vhdx` (RO, shared by all executions) + `diff.vhdx` (RW, copy-on-write at block level). All writing falls on the child. Raising the child is almost instantaneous and occupies only the changed blocks → **low storage**.
- ** Semantic capture:** two paths, choose by `evasionLevel`:
  - *In-guest minifilter* (`FsFilter`, FltMgr): events `create/write/delete/rename`
    with path + PID. Rico, but detectable (driver present in guest).
  - *VMI + disk diff*: no driver in guest. You rebuild "what has changed" by reading
    `diff.vhdx` blocks and diffusing against base (more stealth, less live).
- ** Policy/redirection (optional):** the minifilter can redirect/!block
  written to baits (honeyfiles) and mark ransomware attempts.
- **Report:**mount `diff.vhdx` to host (read only) and enumerate files
  new/altered/excluded → `ChangeSet.files*`.
- **Delete:**Delete `diff.vhdx`. Base intact. Environment is gone.

## 6. Strategy — virtualisation of the **record*

- **Naturally isolated:** Hives (`SYSTEM`, `SOFTWARE`, `NTUSER.DAT`) are files
  in the guest → volume already captured by `diff.vhdx`. You don't need to "redirect" record as Sandboxie does, because the guest has ** its own kernel and its own hive** (this is the gain to run on a MicroVM instead of a host container).
- ** Semantic capture (optional):** `CmRegisterCallbackEx` in guest → events
  `RegCreateKey/RegSetValue/RegDeleteKey/Value` with actor. Or VMI reading the configuration cache. Or, simpler and more robust: **diff of hives** (offline, comparing base vs diff) for the report of persistent changes.
- ** Persistence detected:** mark classic keys (`Run`, `RunOnce`, `Services`,
  `Image File Execution Options`, `Winlogon`) → flags in the report.

## 7. Strategy — virtualization of processes / kernel objects**

- **Construction insulation:**separate kernel
  objects (`\BaseNamedObjects`, mutexes, events), handle tables — all isolated from the host. Nothing leaks to the real process tree.
- **Monitoring (hybrid):**
  - *In-guest:*`PsSetCreateProcessNotifyRoutineEx` (process tree),
    `PsSetLoadImageNotifyRoutine` (modules/DLLs), `PsSetCreateThreadNotifyRoutine`,
    `ObRegisterCallbacks` (handle openings for injection), + ETW
    `Microsoft-Windows-Kernel-Process`.
  - *Host VMI (anti-tamper):*`IntrospectionEngine` is on the list of `EPROCESS` via
    EPT and compares with what the agent reports → detects hidden/rootkit processes and is **impossible for malware to disable** (it is outside the guest).
- **Technology detection:** process hollowing/injection via RWX and
  discrepancies VMI-vs-agent.

## 8. Strategy — virtualisation of devices** (the vaultty core)

- The VMM exposes **only** a minimum set of *paravirtuals* devices:
  `PvDisk` (disk), `PvNet` (NIC), `PvChannel` (host↔agent channel), `PvConsole` (framebuffer), and the minimum platform (timer/APIC that the WHP already provides).
- **There is no mapping for `PhysicalDrive`, **nor**PCI passthrough, **nor**I/O
  port for real hardware, **n’t even**MMIO for real device memory.
- Any guest access to unmapped physical address / unmodeled I/O port
  → **EPT violation** or **I/O exit** → VMM intercepts and responds *virtually* or denies + loga.
- **DMA:**The "physical memory" of the guest is GPA, remapped by SLAT/EPT for a set
  The guest ** cannot address physical memory of the host**. As there is no real device passthrough, there is no real DMA → the IOMMU is relevant only to passthrough, that this architecture **prohibits by construction**.

## 9. Strategy —**monitoring** (3 sources + fusion)

1. **Device layer (VMM):**log ALL I/O of disco/network at device model level —
   the guest has no way to hide (it is below the guest kernel). This is where the attempt to write the MBR appears as `DeviceEvent{op:"write", lba:0}`.
2. **VMI (host):**cases/memory fundamental truth without hooks in guest.
3. **In-guest (optional):** kernel calls + ETW + agent → semantic context
   ( API names, arguments, URLs).
4. ** Fusion:** `BehaviorRecorder` orders by timestamp in a `BehaviorTimeline`;
   VMI×agent crossing turns evasion/rootkit. signal

## 10. Strategy**anti-fingerprinting**(expective management: is arms race)

- ** Prefer VMI to hooks in guest:** no hooked stubs in ntdll for malware to find.
- **Device plausible models:**real-ish manufacturer strings (without "VBOX"/"VMware"/
  "QEMU"), credible disco/firmware model, common MAC UI.
- **CPUID/MSR masking:**control hypervisor bit and leaf 0x4000000x (hide
  totally can break things; advanced malware uses *timing* anyway).
- ** Consistent STC:** use TSC off: minimize anomalous latency of `RDTSC` (each exit
  is expensive → choose what to intercept). 
- **Inhabited environment:** user with history, documents, browser, uptime,
  installed programs, background processes — defeat checks "virgin machine".
- ** User simulation:** mouse/keyboard/windows to fire waiting payloads
  interaction (and defeat "no human present").
- **Sanear known artifacts:** hostname/username/MAC/services/registry typical of
  sandbox.
- **Honest:** no sandbox wins this absolutely; the goal is to raise the
  Cost of evasion, not eliminating it.

## 11. How **MicroVM protects hardware** without turning traditional VM complete

- **What she doesn't do:** doesn't emulate an entire PC (no legacy chipset without BIOS/UEFI
  complete emulating dozens of devices, no emulated GPU, no full ISA/PCI bus). Do not use Hyper-V management stack (without `vmms`/VM "real" in Hyper-V Manager).
- **What it does (minimum):** CPU via WHP (VT-x/AMD-V + EPT), RAM isolated by SLAT,
  and **only the paravirtual device models. Minimum Guest (dry image/PE/library-OS).
- ** Hardware isolation** = EPT (memory) + I/O/MMIO intercept + absence
  The rest (device models rich, monitor, profiles) is your code — hence "MicroVM", not "full VM."
- ** Low cost:** boot per *snapshot restare* (sub-second); low RAM (services
  minimum + immutable shared/dedup pages with base); low storage (shared RO base + small diff).

## 12. How a malware type **MEMZ** affects only the virtual environment

MEMZ: shoots payloads (opens URLs, mouse chaos, processes) and, the final blow, ** overwrites MBR** via raw disk handle (`\\.\PhysicalDrive0`) and reboot force for boot payload.

This one. architecture:
- The guest's "physical disk" handle points to the **PvDisk** → writing goes pro
  **`diff.vhdx` 0 sector**, **never** to the actual host disk.
- O VMM log `DeviceEvent{device:"PvDisk", op:"write", lba:0, ...}` → flag
  Red "Tent to overwrite MBR" in the report.
- The reboot restarts the **guest** (or we just close).
- The actual Disco/MBR/GPT of the host are unalignable** (EPT + without passthrough).
- In teardown, `diff.vhdx` is discarded → MEMZ disappears. Full report, **zero**
  impact no host.

## 13. Real technical limitations (without selling illusion)

1. **Platform hypervisor dependent:** without WHP/virtualization on, there is no
   Hardware isolation. Write your own VMM VT-x = huge, requires signature, conflict with Hyper-V/VMware. (It is the current lock on your machine.)
2. ** MinimalGuest is difficult:** a really lean NT/library-OS was an effort of
   The pragmatic path is a reduced Windows image** (or Windows PE) per profile — larger than ideal, but feasible; maintained light by differential base + dynamic memory + dedup.
3. **VMI is fragile by version:** kernel structures offsets change by build →
   maintenance, and tie to version profiles.
4. ** Anti-evasion is never complete:** timing/side-channels still denounce virtualization.
5. **Network:**give "real internet" to malware increases risk/abuse; normal is
   blackhole/sinkhole/catch — which reduces the fidelity of samples that depend on C2.
6. **Requirements:**"minimum RAM + Instant Boot + Memory Snapshot
   "complete" are counterposed; it is a trade-off calibrating.
7. **VM-exit cost:** the more you intercept (RDTSC, MSR, I/O) by stealth,
   slower. Balance stealth × performance.
8. **Profiles 7/10/11:**Win7 no longer receives updates and has boot peculiarities under
   hypervisor; maintaining 3 bases is storage and curation cost.

## 14. Which parts ** require kernel drivers**

| Part | Kernel? | Remarks |
|---|---|---|
| Hypervisor / EPT / vCPU | ** No** (in your code) | WHP runs the VMM in **user-mode**; the ring-0 is from the platform hypervisor. Great advantage: **host does not need its own driver**. |
| Device models (PvDisk/Net/Channel) | No | User mode, inside the VMM. |
| VMI / introspection | No | Reads guest memory via API WHP (user-mode). |
| ImageManager / VHD | No | API `VirtDisk` (user-mode). |
| Telemetry *inside do guest* (FsFilter/RegMon/NetMon/ProcMon) | **Yes.** | They are drivers, but **no guest** (disposable). As the guest is yours, you can use *test-signing* inside — without touching the host signature. They can be**omitted** if you are 100% VMI (sneakier). |
| Own VT-x Driver (alternative to WHP) | **Yes., WHQL** | Only if you refuse the WHP, not recommended. |

## 15. Which parts only work in user mode**

- **All o host:** VMM (WHP), device models, VirtualNetwork, IntrospectionEngine,
  ImageManager (VirtDisk), SnapshotManager, BehaviorRecorder, ReportBuilder, HostController, UI. → the host side is **fully user-mode** thanks to the WHP.
- **No guest:**`GuestAgent` (receives/spear sample, simulate user, bridge
  telemetry) is user-mode. Only the thin kernel monitors (optional) are ring-0, and yet ** inside the guest.

---

## Module map (proposed, integrates with current GuardianAV)

```
src/microvm/
  host/   HostController.* ImageManager.* SnapshotManager.* VirtualNetwork.*
          IntrospectionEngine.* BehaviorRecorder.* ReportBuilder.*
  vmm/    MicroVmMonitor.* (WHP)  devices/PvDisk.* PvNet.* PvChannel.* PvConsole.*
  guest/  agent/GuestAgent.*  drivers/FsFilter/ RegMon/ NetMon/ ProcMon/   (WDK)
  shared/ Protocol.h (PvChannel) Profiles.h Events.h
profiles/ win7/ win10/ win11/  (base.vhdx + offsets VMI por build)
```

Integration: the `avui`** Sandbox** page wins an engine (`In-OS triage` ↔ `MicroVM`) and profile (Win7/10/11). The `Detonate` calls `HostController::Detonate(...)` instead of the current `LaunchWindowsSandbox`.

---

## Chosen Path: NT personality (Wine backwards), do not boot SO

Project decision: instead of booting a real Windows image inside the MicroVM (UEFI + Firmware) AHCI/virtio + ACPI), we built a ** NT/library-OS personality**. The *user-mode* code of the PE runs **native** within the WHP partition (fast, without emulate instruction), but **all syscall becomes a VM-exit that our "kernel" in the host meets**. Advantage: total freedom over the reported kernel version and about fake devices (GPU, sound card, etc.), because the kernel side is 100% nBone. Security: Partition isolates everything — even a `syscall` direct malware falls on us, never on real Windows.

Honest cost: it is the **largest piece**of the project — in practice a Windows emulator (PE loader + ntdll/kernel32 synthetic + syscalls + PEB/TEB + memory management). It grows incrementally on what already exists (WHP pin, device layer, snapshots).

Components of this layer:
- **Long mode + pagination** no `MicroVm` (`SetupLongMode`) — ✅ done.
- **Doorbell of syscall** (`MicroVm::MapIoTrap`) — the guest signals a syscall and
  the host reads registers/memory and meets. . . done (PoC `vmsyscall.exe`).
- **Table of NT syscalls** (synthetic kernel) — grow case by case: memory
  (`NtAllocateVirtualMemory`...), files (`NtCreateFile`→FS virtual in VHD), registration (`NtOpenKey`→hive virtual), cases/threads, devices fake.
- **Loader of PE** (host) — map sections in guest RAM, resolve imports
  against our synthetic DLLs, apply relocations, mount PEB/TEB and stack.
- ** Real `SYSCALL` trap** via LSTAR→unmapped page + #PF intercept
  (replaces doorbell by port, taking up to direct syscalls) + CPL3 (user) / CPL0 separation (our kernel).

NT personality sub-roadmap (from the current PoC):
1. . Long mode + syscall doorbell served by host (`vmsyscall.exe`).
2. . Shielding — `vmguard.exe`: guest in **CPL3 (ring-3)** with `EFER.SCE=0` then
   the actual `SYSCALL` instruction (our OR a direct syscall of malware bypassing our DLLs) generates **#UD**, intercepted via `ExceptionExitBitmap` + `ExtendedVmExits.ExceptionExit` and served by the synthetic kernel. Privileged instructions on CPL3 generate **#GP** (vector 13), also intercepted and reported as blocked action. (Simpler and robust than LSTAR+#PF.)
3. . Minimum EP Loader — `src/microvm/host/PeLoader.*` + `vmpe.exe`: generates a
   PE32+ without CRT, map sections, apply DIR64 relocations, rewrites IAT for stubs and wheel. **Unified with shielding (step 2):** stubs use the actual `SYSCALL` instruction and `.exe` runs in ring-3, so loader and evasion-resistant path are the same. (Fault: PEB/TEB + CRT for common binaries.)
4. 🟡 PEB/TEB + Ldr + heap — foundation made: `src/microvm/host/ProcessEnv.*`
   (TEB/PEB/ProcessParameters + `BuildLoaderData` = `PEB_LDR_DATA` with circular list of modules) + `MicroVm::SetGsBase` + heap bump no `NtKernel` (`GetProcessHeap`/`HeapAlloc`/`VirtualAlloc`). Demos `vmpeb.exe` (GSÆTEB→PEB →ImageBase→CommandLine) and `vmheap.exe` (schedules) `Ldr`, aloca/write ? **startup + TLS** — `vmstartup.exe` + `NtKernel`: `GetCurrentProcessId`/ `ThreadId` (read `TEB->ClientId`), `GetCommandLineW` (read `PEB->params`), `GetModuleHandleW` (NULL=self, or walks in the `Ldr` comparing `BaseDllName`), `GetSystemTimeAsFileTime`/`QueryPerformanceCounter`/`GetTickCount` (cookie) `GetStartupInfoW`, and TLS (`TlsAlloc`/`TlsSetValue`/`TlsGetValue` in the slots of the TEB). `NtKernel::StackArg` read the args 5+ (`[RSP+0x28+...]`) at the time of syscal. **`test/nt_gcc.c` compiled with `gcc -nostdlib` (importation of `KERNEL32.dll`) wheel by `microsandbox`**: does `GetStdHandle`+`WriteFile` (console), `CreateFileA` with `dwCreationDisposition` ** on stack** read correctly (`CREATE_ALWAYS`), `WriteFile`/`CloseHandle` in the virtual FS, `ExitProcess` — with a report and a real record untouched. (Film: `NtAllocateVirtualMemory` real and the full surface of `ucrtbase`/ `vcruntime` for a `.exe` with standard CRT reach in `main`.)
5. . virtual FS + report — `src/microvm/host/VirtualFs.*`, `NtKernel.*`,
   `ReportBuilder.*` + `vmfs.exe`: a PE makes `CreateFileA`/`WriteFile`/ `CloseHandle`; the synthetic kernel serves against a memory FS and records a behavioural timeline** (created/written/bytes). The "loot" is only on the virtual FS — the real disk is never played. (Falt: connect to the VHD diff of the `PvDisk`, `CreateFileW`/realpaths.)
6. ✅ Registro virtual + perfil de sistema fake — `src/microvm/host/VirtualRegistry.*`
   + `SystemProfile` (Win7/10/11) + `vmreg.exe`: `RtlGetVersion` and registration
   (`RegQueryValueA`/`RegSetValueA`) respond with the **version/ProductName/GPU we invented**. The same PE sees Win11 or Win7 as per profile; persistence writing (`HKCU\...\Run`) is captured and the actual record is never played. (Fault: sound/network as devices, ABI Win32 with stack args.)
7. 🟡 Consolidation — `src/microvm/MicroSandbox.*` (facade `Detonate()`) +
   `microsandbox.exe`: a call joins MicroVm (ring-3, exceptions) + PeLoader + `NtKernel` (FS/log/profile) + `ReportBuilder` in a unified report**. Also exposed as `av detonate-micro`. (Falt: grow the surface up to real samples — PEB/TEB+CRT, ABI with stack args, virtual memory.)
8. . GUI by **proxy pro host** (Wine model), do not reimplement `win32k`
   (ReactOS/Wine scale, decades).The execution is isolated in the sandbox; only the *design* native wheel. . . **First brick** — `test/nt_gui.c` (gcc, import `USER32.dll`) runs by `microsandbox`: `MessageBoxW` is routed by the `NtKernel` pro `user32` real and a native dialogue appears** (self-dismiss) with the event in the report.
   - **Next bricks:**`RegisterClass`/`CreateWindow`/`ShowWindow` + pumping
     of messages with the **`WndProc` called back DENTRO of the guest**(re-entry into vCPU) + GDI (`BeginPaint`/`TextOut`/`BitBlt`) with mapping of `HDC`/`HWND`/`HBITMAP` and structs/pixels marshaling + threads in the guest.
   - **Critical security:**Proxy CAN NOT draw on the real desktop (channel
     escape — MEMZ would paint your screen).The safe version renders it into an area that we control** (window-daughter/off-screen/`CreateDesktop`) displayed in the sandbox viewer — as RemoteApp/Windows Sandbox does.

### Model B: load real DLLs (mini-NT by real SSNs)

Instead of synthesizing each Win32 function, **loads the real `ntdll`/`kernel32`** and intercepts only **~488 syscalls `Nt*`** (`Rtl*`/`Ldr*` come for free because they are `ntdll`'s own user-mode code). DLLs base: viable. `user32`/`gdi32`: no (need `win32k`) — so GUI uses the proxy above.
- `tools/ntsyscalls.cpp` extracts the real `nome→SSN` table from `ntdll` alive (488
  entries in this build; `NtCreateFile=0x55`, `NtAllocateVirtualMemory=0x18`...).
- .**Provided** — `src/microvm/host/MiniNt.*` dispatches by the actual SSNs (read from
  `ntdll` alive); `MicroVm::MapPage2MB` maps `KUSER_SHARED_DATA` in
  `0x7FFE0000`. `vmntdll.exe`: o `PeLoader` map a **`ntdll.dll` real** (0x266000
  bytes, relocated), guest **CHAMA real stubs**`NtAllocateVirtualMemory`/
  `NtTerminateProcess`, and `MiniNt` meets — real allocation (`STATUS_SUCCESS`,
  written and read back).
  . **Real NT file subsystem** — `vmntfile.exe`: guest calls stubs
  real `NtCreateFile`/`NtWriteFile`/`NtClose` with `OBJECT_ATTRIBUTES`/
  `UNICODE_STRING`/`IO_STATUS_BLOCK` real (path `\??\C:\...`, `Buffer`/`Length`
  `NtWriteFile` read from the stack), and `MiniNt` serves against `VirtualFs` —
  virtually captured file, real disk untouched.
  ? ** Sections (the core of the loader)** — `vmntsect.exe`: the guest calls the real stubs
  `NtCreateFile`→`NtCreateSection`→`NtMapViewOfSection`; o `MiniNt` cria uma section
  **backed by file**(from `VirtualFs`) and maps the content in a view, which the
  guest reads. It's how Windows maps data.
  . ** Image section (`SEC_IMAGE`) — real DLL mapping** —
  `vmntimage.exe`: the guest creates a section with `SEC_IMAGE` about a real **DLL
  Relocable** (`test/imgtest.dll`) in `VirtualFs` and map. `MiniNt` does not copy
  bytes crus: makes PE layout complete** (`LayoutPeImage` in `MiniNt.cpp`) —
  headers + each section in your RVA by `SizeOfImage`, plus the DIR64 links**
  for the cargo delta. The guest then ** CALLS the exported `Probe()`** from the image
  mapped, which returns `*g_ptr` (an absolute pointer that only solves within the
  view if relocation has been applied): returned `0xC0FFEE5`
  headers/sections in the right RVA, executable code running from the image, and fixups from
  relocation.
  . **Loader + sync consultations** — `vmntquery.exe`: what `LdrpInitialize`
  ask before mapping any DLL. The guest calls the real stubs
  `NtQueryInformationProcess(ProcessBasicInformation)` (`MiniNt` reads GS base →
  `TEB+0x60` → devolve o `PebBaseAddress`), `NtQuerySystemInformation(SystemBasicInformation)`
  (1 CPU, 4 KiB pages, 64 KiB granularity) and the event trio
  `NtCreateEvent`/`NtSetEvent`/`NtWaitForSingleObject` (template single-thread: object
  Signed satisfies on time). Guest read the PEB that mounted, the CPU number and a wait
  with `STATUS_SUCCESS` ✅.
  . ** NT record** — `vmntreg.exe`: the guest calls the real stubs
  `NtOpenKey`/`NtQueryValueKey`/`NtSetValueKey` against o namespace `\Registry`
  in memory of `MiniNt` (`\Registry\Machine\...`, case-insensitive keys).
  **Seed with the system customity** with the right types
  (`SafeDllSearchMode` = `REG_DWORD 1`, `ProductName` = `REG_SZ "Windows 11 Pro"`,
  via `KEY_VALUE_PARTIAL_INFORMATION`), and **captures a persistence writing**
  (`Run\Updater = "...evil.exe"`) with reading round-trip — Real HKLM/HKCU
  intocado.
  == sync, corrected by elderman == `LdrInitializeThunk`** — `vmldr.exe`: instead of
  call `Nt*` by hand, points the vCPU to **`LdrInitializeThunk` real `ntdll`** (the
  entry that the Windows kernel itself uses in a new thread), with desktop environment
  truth process (TEB/PEB/ProcessParameters + `CONTEXT`, `RCX=&CONTEXT`,
  `RDX=ntdll base`) and lets the real loader drive**. `MiniNt` meets and **trace
  Each syscall**; #UD/#GP/#PF intercept to report the exact point.
  - **SSE needs to be connected**: `ntdll` uses `xorps`/`movaps`; without `CR4.OSFXSR`
    Gives #UD. Connected `CR4 = PAE|OSFXSR|OSXMMEXCPT (0x620)` no `SetupLongMode`.
  - **ABI alignment**: `RSP ≡ 8 (mod 16)` at the input (as if a `CALL`
    had stacked the return), otherwise the `movaps [rsp+k]` stacks give #GP.
  - With this the real loader runs **60+ syscalls background** on `LdrpInitializeProcess`:
    Security cookie, `NtProtectVirtualMemory`, heat via **`NtAllocateVirtualMemoryEx`**, NLS codepage, the entire scan of **Image File Execution Options / GlobalFlag** (~25 values), Session Manager, ProductOptions, `ProcessCookie`, system info. All served by `MiniNt`.
  - . **The real wall**: after `NtWaitForAlertByThreadId`, the loader enters its
    **lock-free work queue /thread-alert synchronization**(`LdrpDrainWorkQueue`) and spins on a list walk (`ntdll!0xBBD0`) expecting progress from worker threads that do not exist in our single-thread model.`MicroVm::Cancel` via `WHvCancelRunVirtualProcessor`) cuts the spin without-exit and reveals the RIP. Strategic conclusion: run the `LdrpInitialize` ed `ntdll` to the end it bumps into its coupling with internal multi-thread state — that's exactly why **Wine and ReactOS reimplement the loader** instead of running the Microsoft. Our probe has proven that you can run the real loader bottom in the ring-up and precisely locate where the coupling locks. `kernel32` real** — `src/microvm/host/WinLoader.*`: instead of turning the `LdrpInitialize` ed `ntdll`, we carry the DLLs ourselves — just like Wine/ReactOS. O `WinLoader` is a recursive host-side loader that: maps each DLL as `SEC_IMAGE` (headers + sections in RVA + DIR64 relocations), load the dependencies recursively, resolve imports between modules (including **forwarders** and the heuristic of **API sets** `api-ms-win-*`/`ext-ms-win-*` → `kernelbase`/`ntdll`/`kernel32`) and patches each IAT. Tests, in three stages:
  - **`vmwinload.exe`** (controlled): two of our DLLs — `libb.dll` imports `AddOne`
    from `liba.dll`. The loader maps the two, solves the crossed import, patches the IAT, and the guest wheel `libb!Compute(20)` (which calls `liba!AddOne` crossing the DLL limit) → **50** ., and `liba!Magic()` → `0x7E57` .
  - **`vmwinsys.exe`** (real): points to `C:\Windows\System32\kernel32.dll` and links
    the whole graph — **kernel32 + kernelbase + ntdll**, **2024 imports solved,
    0 missing**. `GetProcAddress`/`LoadLibraryA` resolve for real addresses in
    guest.
  - **`vmwincall.exe`** (capstone): o guest **CHAMA a export real `kernel32!lstrlenA`**
    in a string and gets the right length (`24`)
    running on our hardware sandbox, linked 100% for our loader, **without the
    `LdrpInitialize` from `ntdll`**. The Wine path works end to end.
  . ** Automatic coverage — call ALL `kernel32` export** — `vmwinall.exe`:
  After linking the graph, it drives each of the 1693 exports** automatically —
  resets the vCPU, points the RIP to function with null args and a `HLT` trampoline
  return on stack, wheel (with `MiniNt` serving real syscalls + TEB/PEB/KUSER),
  and a watchdog by call** cuts spins. Sort each result without
  know no signature. Result: **1317 / 1693 (77.8%) run and return
  limpo** (`GetCurrentProcessId`→PID certo, `GetVersion`→actual version, etc.); ~17%
  turn on locks/waits without initialized status, ~5% fhighm/#UD in functions that
  need real heap/handles (CreateProcess, FindFirstFile, threadpool).
  empirical map exact of what is missing — automatically generated.
  ✅ **Heap + locks via hot-patch (technique Wine) — `vmwininit.exe`** — discovery
  key: the real `HeapCreate` rotates in **ntdll+0xBBDB**, the boss's *same* loop. or
  either, `0xBBD0` is not from the loader: it is a central primer of `ntdll`** (list of
  process heaps under lock, with thread-alert) that both loader and heat
  use, and that assumes initialized multi-thread state — the common wall. The solution is the
  Wine: *code*hot-patch of functions (cover forwarders and calls)
  internal). `ApplyWinePatches` (`WinLoader.cpp`) reescreve as `Rtl*Heap` ed `ntdll`
  for `mov eax,<op>; syscall; ret` — `MiniNt::DispatchHeap` serves them with a
  simple buffer — and SRW/critical-section locks for no-ops (safe
  single-thread). Result: Real `HeapCreate`/`HeapAlloc`/`HeapSize`/`HeapFree`
  make a complete round-trip (write+read verified) .
  . **Head on cover** — re-turning `vmwinall.exe` with patches: o
  "returned" rises from **1317 (77.8%) to 1589 (93.9%)** of the 1693 exports, and
  Spins fall from **289 for 24** (the no-ops of lock).6% rbookcases are
  functions that *must* terminate (`ExitProcess`/`FatalAppExit`/`DebugBreak`) or that
  need valid args/threadpool (`CreateProcess`, `GetDriveType` with buffer
  (nil). **93,9% dthe actual surface of `kernel32` executes and returns clean in our
  sandbox.**
  (Next: detonate a real `.exe` by linking it against the `kernel32` we climbed — the
  full analysis loop; and model threadpool/fibers for last %.)
  ✅ **Anti-VM / personality "bare-metal" — `vmspoof.exe` + `vmspoofreg.exe`** — one
  sample sandbox-aware doesn't detonate if you smell VM. We cover the two tracks it uses:
  - **CPUID (`vmspoof.exe`):** `MicroVm::SpoofCpuid(vendor, brand, nCpus)` gun o
    `WHvPartitionPropertyCodeCpuidExitList`**together with the master enable `X64CpuidExit`**(`ExtendedVmExits` bit 0 — without it the `CpuidExitList` is accepted but *no* generates exits; found validated on this machine). `HandleCpuid` then **cleans the hypervisor bit**(leaf 1 ECX[31]), **zero the leaf `0x40000000`**(hypervisor dispenser), and injects vendor/brand/count of colors. The real host is a **AMD Ryzen 5 5500**; the guest sees a **Intel i7-10700K, 8 colors, without hypervisor**—bare metal.
  - **Register (`vmspoofreg.exe`):**`MiniNt::SeedPersonality` sows a
    **Consistent machine-victimidentity**that malware scans: BIOS/plate **ASUS**(not "VMware"/"innotek"), Intel CPU, **NVIDIA RTX 3070 + Fake driver**(`nvlddmkm` registered, no real kernel module), `MachineGuid`, `ComputerName`, `InstallDate`, owner, and a list of **installed programs**(Chrome, Firefox, Office, Steam, Discord...).Guest reads all this **by the real `ntdll` stubs**(`NtOpenKey`/`NtQueryValueKey`) and none of the probes returns a VM *tell* — the machine reads as used metal.
  - **CPU/RAM (`NtQuerySystemInformation`):**the default "1 CPU, 1 GiB" is a
    sandbox; now `SystemBasicInformation` reports **8 processors and 16 GiB** (`0xFF` affinity), and we add `SystemProcessorInformation` (AMD64, family 6 model 165) — consistent with i7-10700K.
  - **Disco (`MiniNt::SeedFilesystem` + `VirtualFs`):**`VirtualFs` won a
    ** VM artifact blocklist** (`vboxguest`, `vmci.sys`, `vmware`, `qemu`, `viostor`, `xenpci`, `prl_`, `sbiedll`...) which open with `STATUS_OBJECT_NAME_NOT_FOUND`, and a set of ordinary sown files** (kernel32, explorer, chrome, user files) that exist. `Open` It now respects the `CreateDisposition`: `FILE_OPEN` of a path absent/blocked real failure (before *everything* was created on-open, which made VM drivers "exist"). `vmspoofreg.exe` proves the two ways: `vmci.sys`/`VBoxGuest.sys` → NOT_FOUND, `kernel32.dll`/`notas.txt` → Gifts.
  - **Register:**Real Intel adapter in the `{4d36e972-…}` class (DriverDesc
    "Intel(R) Ethernet Connection (7) I219-V", `ven_8086`), without `NetworkAddress` override — then no VM UI (00:05:69, 08:00:27...). . . . **Anti-VM timing/implementation — `vmspoofsys.exe`** — two more checks that sample sandbox-aware does, read the way malware reads:
  - **Uptime (`MiniNt::SeedSharedData`):** `GetTickCount64` read o `KUSER_SHARED_DATA`
    direct; a newly booted VM (uptime seconds) is sandbox tell. We sow `TickCountQuad`/`InterruptTime`/`TickCountMultiplier` (0x0FA00000) for **~3 days uptime** (79h 21m). The guest reads `[0x7FFE0320]` and the host confirms the high uptime.
  - ** List of processes (`NtQuerySystemInformation` class 5):** few wheel sandbox
    processes and malware scan the list by analysis tools (`vmtoolsd`, `vboxservice`, `wireshark`, `procmon`, `x64dbg`...). We've issued a chain `SYSTEM_PROCESS_INFORMATION` believable of **46 processes** desktop (svchost, explorer, chrome, steam, Defender...) and **none** tool. Host walks the list (count ≥ 30, scan of names) — all clear. `vmspoofnet.exe`** — closes the last path. Any API that reads MAC (`GetAdaptersInfo`/`GetAdaptersAddresses`/NDIS) Bottom-out, in the kernel, in an OID NDIS file**. `NtDeviceIoControlFile`: upon receiving `IOCTL_NDIS_QUERY_GLOBAL_STATS` (0x00170002) with `OID_802_3_PERMANENT_ADDRESS` /`CURRENT_ADDRESS`, returns a MAC with genuine Intel oui** (`8C:16:45:…`) — never a VM UI (VMware) 00:05:69/00:0C:29/00:50:56, VirtualBox 08:00:27, Hyper-V 00:15:5D, Xen 00:16:3E, QEMU 52:54:00). `\Device\Tcp`/`\Device\Nsi` The guest makes exactly that way (`NtCreateFile("\Device\Tcp")` → IOCTL) and the host validates that the UIU is not in any VM list. **The network path is 100% copen** (identity in record + live MAC via the real path of the NDIS).

## Roadmap of realistic implementation (lower order→higher risk)

1. **PoC WHP:**up a partition,map RAM,run a 16-bit/UEFI "hello world",
   see VM-exits. (valid that WHP/virtualization is connected to the machine.) . **IMPLEMENTED** — `src/microvm/vmm/WhpPoc.cpp` (except `vmpoc.exe`): creates partition, maps 4 KiB in GPA 0x1000, real-mode guest wheel of 13 bytes and intercepts `OUT 0xE9` as VM-exits. The readiness gate is `src/microvm/host/Preflight.cpp` (command `av vmcheck`), which detects via `WHvGetCapability` if the hypervisor is running and, if not, how to turn it on.
2. **PvDisk + differential VHD + Windows PE boot** →minimum boot on your VMM.
   . **PARTIAL** — *layer of devices* is implemented and demonstrated: `src/microvm/vmm/MicroVmMonitor.*` (class `MicroVm` + interface `IoDevice`), `devices/Devices.h` (`PvConsole`) and `devices/PvDisk.cpp` (disco differential copy-on-write).The target `vmdemo.exe` runs a guest that makes a boot-style sector-MEMZ (512×0xCC in LBA 0): falls on `sandbox-disk.diff`, with `host disk opened: no`. **Missing** the big part: booting a real Windows PE (firmware UEFI + AHCI/virtio + ACPI) — effort of weeks, is the real wall to run real PE malware.
3. **PvChannel + GuestAgent** → transfer and execute the sample; capture exit code.
4. **Device-layer logging** (disco/network) → already gives the report "MBR write" of MEMZ.
5. **VirtualNetwork** (blackhole→sinkhole→capture).
6. **SnapshotManager**(boot snapshot → restore fast) → disposable + fast.
   **IMPLEMENTED** — `src/microvm/host/SnapshotManager.*` catch/restores the guest RAM + 29 vCPU registers. Demo `vmsnap.exe`: detonates a guest that changes memory, restores snapshot in ~100 μs (image of 1 MiB, without reboot) and re-wheels from the restored state. It is the "disposable" indeed: a `Restore` erases all change (RAM + CPU) in microseconds.
7. **VMI** (offset profiles) → stealth monitoring of cases/memory.
8. **Optional in-guest telemetry** (minifilter etc.) → rich semantic context.
9. **Anti-fingerprinting** (incremental) + **profiles 7/10/11**.
10. **ReportBuilder** complete (ChangeSet + timeline + screenshots).

> Where GuardianAV already helps today: the current `Sandbox` in-OS (job + low-IL token +
> hidden desktop + FS/cases monitor) is a "L0 version without hypervisor" — good
> for quick screening, not for MEMZ. The MicroVM above is the upgrade that closes the
> borehole of hardware.
