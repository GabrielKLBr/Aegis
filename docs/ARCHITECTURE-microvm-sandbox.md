# GuardianAV — MicroVM Sandbox (codinome: **Carcará / MDE — Micro-Detonation Engine**)

Projeto técnico de um sandbox de análise dinâmica para Windows que **não** é uma VM
tradicional (sem QEMU/VirtualBox/VMware/Hyper-V Manager como produto), mas usa um
**hypervisor mínimo (MicroVM)** apenas como *piso de isolamento de hardware*. O grosso
da virtualização (arquivos, registro, processos, objetos, rede) é feito por uma
camada de compatibilidade leve sobre um guest mínimo, com disco em VHD diferencial.

> **Decisão honesta nº 1 (leia antes de tudo):** na prática, o "hypervisor mínimo"
> em Windows = **WHP (Windows Hypervisor Platform)**, a API pública sobre o
> hypervisor do Windows (o mesmo que o Hyper-V usa por baixo). É assim que QEMU
> (acelerador WHPX), o Android Emulator e o VMware 17 rodam no Windows. Você NÃO usa
> o Hyper-V Manager nem o Windows Sandbox como produto — você escreve o seu próprio
> VMM, seus device models e seu guest — mas o *primitivo de virtualização de CPU/EPT*
> vem do hypervisor da plataforma. A única alternativa é escrever seu próprio driver
> VT-x/AMD-V (enorme, exige assinatura WHQL, conflita com qualquer outro hypervisor).
> Logo: **a feature "Windows Hypervisor Platform" / virtualização precisa estar ligada**
> — é exatamente o que está desligado na sua máquina hoje (`HypervisorPresent=False`).

---

## 1. Componentes da arquitetura

Camadas, de baixo para cima:

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

**Responsabilidades:**

| Componente | Camada | Responsabilidade |
|---|---|---|
| `HostController` | L3 user | Orquestra ciclo de vida, perfis, política, agrega relatório |
| `ImageManager` | L3 user | Bases VHD por perfil; cria VHD diferencial (CoW); monta/descarta (API VirtDisk) |
| `SnapshotManager` | L3 user | Snapshot diferencial de disco + snapshot de RAM/vCPU; restore rápido |
| `VirtualNetwork` | L3 user | Switch virtual host-side; sinkhole DNS / INetSim / captura pcap; sem NIC real |
| `IntrospectionEngine` (VMI) | L3 user | Lê memória do guest via WHP/EPT; observa sem hooks no guest (anti-evasão) |
| `BehaviorRecorder` | L3 user | Funde eventos (VMI + agente + device layer) numa timeline |
| `ReportBuilder` | L3 user | Gera ChangeSet (diff de FS/registro) + relatório JSON/HTML |
| `MicroVmMonitor` (VMM) | L2 user | Cria partition WHP, mapeia memória (SLAT), roda vCPUs, despacha VM-exits |
| `DeviceModel` + Pv* | L2 user | Disco/NIC/canal/console **paravirtuais**; nega hardware real |
| `GuestAgent` | L0 guest user | Recebe a amostra pelo PvChannel, executa, simula usuário, envia telemetria |
| `FsFilter/RegMon/NetMon/ProcMon` | L0 guest kernel | Telemetria semântica fina (opcional; substituível por VMI) |

---

## 2. Fluxo de execução

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

## 3. Estruturas de dados (núcleo)

```cpp
enum class OsProfile { Win7, Win10, Win11 };
enum class NetMode   { Blackhole, Sinkhole /*INetSim-like*/, Capture, NatLimited };

struct SandboxProfile {
    OsProfile     os            = OsProfile::Win10;
    std::wstring  baseVhdPath;                 // imagem RO do perfil
    uint32_t      vcpus         = 1;
    uint64_t      ramBytes      = 1ull<<30;    // 1 GiB típico; mínimo possível
    NetMode       net           = NetMode::Sinkhole;
    uint32_t      timeoutMs     = 60'000;
    uint8_t       evasionLevel  = 2;           // 0..3 (quanto de anti-fingerprint)
    bool          useGuestDrivers = false;     // false = só VMI (mais furtivo)
};

// Cadeia de VHD diferencial (copy-on-write em nível de bloco).
struct DiffDisk { std::wstring parent; std::wstring child; uint64_t virtualSize; };

// Memória do guest exposta ao SLAT/EPT.
struct GuestRam { uint64_t gpaBase; uint64_t size; void* hostVa; uint32_t flags; };

// Resultado de WHvRunVirtualProcessor.
struct VmExit {
    enum Reason { Mmio, PortIo, Cpuid, Msr, Hlt, InterruptWindow, Unrecoverable } reason;
    uint64_t   gpa;          // p/ Mmio
    uint16_t   port;         // p/ PortIo
    bool       isWrite;
    uint64_t   value;        // dado lido/escrito
    WHV_VP_EXIT_CONTEXT raw; // contexto completo de CPU
};

// virtio-like queue p/ device models paravirtuais
struct VirtQueue { uint64_t descGpa, availGpa, usedGpa; uint16_t size, lastSeenUsed; };

// Eventos de comportamento (timeline única, ordenada por ts).
struct Actor { uint32_t pid; uint32_t tid; std::wstring image; };
struct FileEvent     { uint64_t ts; Actor by; std::wstring action, path; uint64_t bytes; };
struct RegistryEvent { uint64_t ts; Actor by; std::wstring action, key, value, data; };
struct ProcessEvent  { uint64_t ts; Actor by; std::wstring action; uint32_t childPid; };
struct NetworkEvent  { uint64_t ts; Actor by; std::wstring proto, remote; uint16_t port; uint64_t bytes; };
struct DeviceEvent   { uint64_t ts; std::wstring device, op; uint64_t lba; uint32_t len; bool denied; }; // ex.: MBR write
struct BehaviorTimeline { std::vector<std::variant<FileEvent,RegistryEvent,ProcessEvent,NetworkEvent,DeviceEvent>> events; };

struct Snapshot { std::wstring vhdDiff; std::wstring ramImage; std::wstring vcpuState; };
struct ChangeSet {                              // o "antes vs depois"
    std::vector<std::wstring> filesCreated, filesModified, filesDeleted;
    std::vector<RegistryEvent> registryDelta;
    std::vector<NetworkEvent>  networkDelta;
    bool mbrTouched = false; bool bootConfigTouched = false;
};
```

---

## 4. Classes e interfaces principais

```cpp
// ---- L2: o coração do MicroVM ----
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
    void     Discard(const DiffDisk&);                     // apaga o filho → descartável
};

class SnapshotManager {
public:
    Snapshot CaptureBooted(MicroVmMonitor&, const DiffDisk&);  // RAM+vCPU+disco no estado "pronto"
    void     Restore(MicroVmMonitor&, const Snapshot&);        // boot rápido
};

class IntrospectionEngine {                // VMI — observa o guest "de fora"
public:
    void Bind(MicroVmMonitor&, OsProfile);                  // perfis de offsets do kernel por versão
    std::vector<ProcessEvent> PollProcessList();           // anda na lista de EPROCESS via EPT
    std::vector<std::pair<uint64_t,std::wstring>> SuspiciousRegions(); // RWX, hollowing, etc.
};

class BehaviorRecorder { public: void Ingest(/*evento*/); BehaviorTimeline Build(); };
class ReportBuilder    { public: ChangeSet Diff(const DiffDisk&, OsProfile); std::wstring ToJson(...); };

class HostController {                      // fachada usada pela UI
public:
    SandboxReportV2 Detonate(const std::wstring& sample, const SandboxProfile&);
};
```

---

## 5. Estratégia — virtualização de **arquivos**

- **Mecanismo base (obrigatório, à prova de MEMZ):** o volume do guest É um **VHDX
  diferencial**. `base.vhdx` (RO, compartilhada por todas as execuções) + `diff.vhdx`
  (RW, copy-on-write em nível de bloco). Toda escrita cai no filho. Criar o filho é
  quase instantâneo e ocupa só os blocos alterados → **baixo armazenamento**.
- **Captura semântica:** dois caminhos, escolha por `evasionLevel`:
  - *In-guest minifilter* (`FsFilter`, FltMgr): eventos `create/write/delete/rename`
    com caminho + PID. Rico, porém detectável (driver presente no guest).
  - *VMI + diff de disco*: sem driver no guest. Você reconstrói "o que mudou" lendo
    os blocos do `diff.vhdx` e diffando contra a base (mais furtivo, menos "ao vivo").
- **Política/redirecionamento (opcional):** o minifilter pode redirecionar/!bloquear
  escritas a iscas (honeyfiles) e marcar tentativas de ransomware.
- **Relatório:** montar `diff.vhdx` no host (somente leitura) e enumerar arquivos
  novos/alterados/apagados → `ChangeSet.files*`.
- **Descarte:** apagar `diff.vhdx`. Base intacta. Ambiente sumiu.

## 6. Estratégia — virtualização do **registro**

- **Naturalmente isolado:** as hives (`SYSTEM`, `SOFTWARE`, `NTUSER.DAT`) são arquivos
  no volume do guest → já capturadas pelo `diff.vhdx`. Não precisa "redirecionar"
  registro como o Sandboxie faz, porque o guest tem **seu próprio kernel e suas
  próprias hives** (esse é o ganho de rodar num MicroVM em vez de container de host).
- **Captura semântica (opcional):** `CmRegisterCallbackEx` no guest → eventos
  `RegCreateKey/RegSetValue/RegDeleteKey/Value` com ator. Ou VMI lendo o cache de
  configuração. Ou, mais simples e robusto: **diff das hives** (offline, comparando
  base vs diff) para o relatório de alterações persistentes.
- **Persistência detectada:** marcar chaves clássicas (`Run`, `RunOnce`, `Services`,
  `Image File Execution Options`, `Winlogon`) → flags no relatório.

## 7. Estratégia — virtualização de **processos / objetos do kernel**

- **Isolamento por construção:** kernel separado ⇒ lista de processos, namespace de
  objetos (`\BaseNamedObjects`, mutexes, eventos), handle tables — tudo isolado do
  host. Nada vaza pra árvore de processos real.
- **Monitoramento (híbrido):**
  - *In-guest:* `PsSetCreateProcessNotifyRoutineEx` (árvore de processos),
    `PsSetLoadImageNotifyRoutine` (módulos/DLLs), `PsSetCreateThreadNotifyRoutine`,
    `ObRegisterCallbacks` (aberturas de handle p/ detectar injeção), + ETW
    `Microsoft-Windows-Kernel-Process`.
  - *Host VMI (anti-tamper):* `IntrospectionEngine` anda na lista de `EPROCESS` via
    EPT e compara com o que o agente reporta → detecta processos ocultos/rootkit e
    é **impossível de o malware desabilitar** (está fora do guest).
- **Detecção de técnicas:** process hollowing/injection via regiões RWX e
  discrepâncias VMI-vs-agente.

## 8. Estratégia — virtualização de **dispositivos** (o núcleo de segurança)

- O VMM expõe **apenas** um conjunto mínimo de dispositivos *paravirtuais*:
  `PvDisk` (disco), `PvNet` (NIC), `PvChannel` (canal host↔agente), `PvConsole`
  (framebuffer), e o mínimo de plataforma (timer/APIC que o WHP já provê).
- **Não há** mapeamento para `PhysicalDrive`, **nem** PCI passthrough, **nem** I/O
  de porta para hardware real, **nem** MMIO para memória de device real.
- Qualquer acesso do guest a endereço físico não mapeado / porta de I/O não modelada
  → **EPT violation** ou **I/O exit** → o VMM intercepta e responde *virtualmente* ou
  nega + loga.
- **DMA:** a "memória física" do guest é GPA, remapeada por SLAT/EPT para um conjunto
  de páginas-sandbox do host. O guest **não consegue endereçar memória física do host**.
  Como não há passthrough de device real, não há DMA real → o IOMMU é relevante só
  para passthrough, que esta arquitetura **proíbe por construção**.

## 9. Estratégia — **monitoramento** (3 fontes + fusão)

1. **Device layer (VMM):** loga TODO I/O de disco/rede no nível do device model —
   o guest não tem como esconder (está abaixo do kernel do guest). É aqui que a
   tentativa de escrever a MBR aparece como `DeviceEvent{op:"write", lba:0}`.
2. **VMI (host):** verdade-fundamental de processos/memória, sem hooks no guest.
3. **In-guest (opcional):** callbacks de kernel + ETW + agente → contexto semântico
   (nomes de API, argumentos, URLs).
4. **Fusão:** `BehaviorRecorder` ordena por timestamp numa `BehaviorTimeline`;
   cruzamento VMI×agente vira sinal de evasão/rootkit.

## 10. Estratégia **anti-fingerprinting** (gerência de expectativa: é corrida armamentista)

- **Prefira VMI a hooks no guest:** sem stubs hookados na ntdll para o malware achar.
- **Device models plausíveis:** strings de fabricante reais-ish (sem "VBOX"/"VMware"/
  "QEMU"), modelo de disco/firmware crível, OUI de MAC comum.
- **CPUID/MSR masking:** controlar o bit de hypervisor e a leaf 0x4000000x (esconder
  totalmente pode quebrar coisas; malware avançado usa *timing* de qualquer forma).
- **TSC consistente:** usar TSC off: minimizar latência anômala de `RDTSC` (cada exit
  é caro → escolha o que interceptar). 
- **Ambiente "habitado":** usuário com histórico, documentos, navegador, uptime,
  programas instalados, processos de fundo — derrota checagens "máquina virgem".
- **Simulação de usuário:** mouse/teclado/janelas para disparar payloads que esperam
  interação (e derrotar "nenhum humano presente").
- **Sanear artefatos conhecidos:** hostname/username/MAC/serviços/registry típicos de
  sandbox.
- **Honesto:** nenhum sandbox vence isso de forma absoluta; o objetivo é elevar o
  custo da evasão, não eliminá-la.

## 11. Como a **MicroVM protege o hardware** sem virar VM tradicional completa

- **O que ela NÃO faz:** não emula um PC inteiro (sem chipset legado, sem BIOS/UEFI
  completos emulando dezenas de devices, sem GPU emulada, sem barramento ISA/PCI
  cheio). Não usa o stack de gerência do Hyper-V (sem `vmms`/VM "de verdade" no
  Hyper-V Manager).
- **O que ela faz (mínimo):** CPU via WHP (VT-x/AMD-V + EPT), RAM isolada por SLAT,
  e **só** os device models paravirtuais. Guest mínimo (imagem enxuta/PE/library-OS).
- **O isolamento de hardware** = EPT (memória) + interceptação de I/O/MMIO + ausência
  de passthrough + GPA≠memória do host. Essa fronteira é justamente a parte que você
  **não** corta para continuar "micro". O resto (device models ricos, monitor,
  perfis) é seu código — daí "MicroVM", não "VM completa".
- **Baixo custo:** boot por *snapshot restore* (sub-segundo); RAM baixa (serviços
  mínimos + páginas imutáveis compartilhadas/dedup com a base); storage baixo (base
  RO compartilhada + diff pequeno).

## 12. Como um malware tipo **MEMZ** afeta só o ambiente virtual

MEMZ: dispara payloads (abre URLs, caos de mouse, processos) e, o golpe final,
**sobrescreve a MBR** via handle de disco bruto (`\\.\PhysicalDrive0`) e força reboot
para o payload de boot.

Nesta arquitetura:
- O handle de "disco físico" do guest aponta para o **PvDisk** → escrita vai pro
  **setor 0 do `diff.vhdx`**, **nunca** pro disco real do host.
- O VMM registra `DeviceEvent{device:"PvDisk", op:"write", lba:0, ...}` → flag
  vermelha "tentativa de sobrescrever MBR" no relatório.
- O "reboot" reinicia o **guest** (ou apenas encerramos).
- Disco/MBR/GPT reais do host são **inalcançáveis** (EPT + sem passthrough).
- No teardown, `diff.vhdx` é descartado → MEMZ some. Relatório completo, **zero**
  impacto no host.

## 13. Limitações técnicas reais (sem vender ilusão)

1. **Dependência do hypervisor da plataforma:** sem WHP/virtualização ligada, não há
   isolamento de hardware. Escrever seu próprio VMM VT-x = enorme, exige assinatura,
   conflita com Hyper-V/VMware. (É o bloqueio atual da sua máquina.)
2. **Guest mínimo é difícil:** um "NT/library-OS" enxuto de verdade foi um esforço de
   pesquisa (Drawbridge). O caminho pragmático é uma **imagem Windows reduzida** (ou
   Windows PE) por perfil — maior que o ideal, mas viável; mantida leve por base
   diferencial + memória dinâmica + dedup.
3. **VMI é frágil por versão:** offsets de estruturas do kernel mudam por build →
   manutenção, e amarra aos perfis de versão.
4. **Anti-evasão nunca é completa:** timing/side-channels ainda denunciam virtualização.
5. **Rede:** dar "internet real" ao malware aumenta risco/abuso; o normal é
   blackhole/sinkhole/captura — o que reduz fidelidade de amostras que dependem de C2.
6. **Tensões de requisito:** "RAM mínima + boot instantâneo + snapshot de memória
   completo" se contrapõem; é um trade-off a calibrar.
7. **Custo de VM-exit:** quanto mais você intercepta (RDTSC, MSR, I/O) por furtividade,
   mais lento. Equilíbrio furtividade × desempenho.
8. **Perfis 7/10/11:** Win7 não recebe mais updates e tem peculiaridades de boot sob
   hypervisor; manter 3 bases é custo de storage e curadoria.

## 14. Quais partes **exigem drivers de kernel**

| Parte | Kernel? | Observação |
|---|---|---|
| Hypervisor / EPT / vCPU | **Não** (no SEU código) | WHP roda o VMM em **user-mode**; o ring-0 é do hypervisor da plataforma. Grande vantagem: **o host não precisa de driver próprio**. |
| Device models (PvDisk/Net/Channel) | Não | User-mode, dentro do VMM. |
| VMI / introspecção | Não | Lê memória do guest via API WHP (user-mode). |
| ImageManager / VHD | Não | API `VirtDisk` (user-mode). |
| Telemetria *dentro do guest* (FsFilter/RegMon/NetMon/ProcMon) | **Sim** | São drivers, mas **no guest** (descartável). Como o guest é seu, dá pra usar *test-signing* lá dentro — sem tocar na assinatura do host. Podem ser **omitidos** se você for 100% VMI (mais furtivo). |
| Driver VT-x próprio (alternativa ao WHP) | **Sim, WHQL** | Só se recusar o WHP. Não recomendado. |

## 15. Quais partes funcionam **só em user-mode**

- **Todo o host:** VMM (WHP), device models, VirtualNetwork, IntrospectionEngine,
  ImageManager (VirtDisk), SnapshotManager, BehaviorRecorder, ReportBuilder,
  HostController, UI. → o lado do host é **inteiramente user-mode** graças ao WHP.
- **No guest:** o `GuestAgent` (recebe/lança a amostra, simula usuário, faz a ponte
  de telemetria) é user-mode. Só os monitores finos de kernel (opcionais) são ring-0,
  e ainda assim **dentro** do guest.

---

## Mapa de módulos (proposto, integra ao GuardianAV atual)

```
src/microvm/
  host/   HostController.* ImageManager.* SnapshotManager.* VirtualNetwork.*
          IntrospectionEngine.* BehaviorRecorder.* ReportBuilder.*
  vmm/    MicroVmMonitor.* (WHP)  devices/PvDisk.* PvNet.* PvChannel.* PvConsole.*
  guest/  agent/GuestAgent.*  drivers/FsFilter/ RegMon/ NetMon/ ProcMon/   (WDK)
  shared/ Protocol.h (PvChannel) Profiles.h Events.h
profiles/ win7/ win10/ win11/  (base.vhdx + offsets VMI por build)
```

Integração: a página **Sandbox** do `avui` ganha um seletor de engine
(`In-OS triage` ↔ `MicroVM`) e de perfil (Win7/10/11). O `Detonate` chama
`HostController::Detonate(...)` em vez do `LaunchWindowsSandbox` atual.

---

## Caminho escolhido: personalidade NT (Wine ao contrário), não bootar SO

Decisão de projeto: em vez de bootar uma imagem Windows real dentro da MicroVM
(firmware UEFI + AHCI/virtio + ACPI), construímos uma **personalidade NT /
library-OS**. O código *user-mode* do PE roda **nativo** dentro da partition WHP
(rápido, sem emular instrução), mas **toda syscall vira um VM-exit que o nosso
"kernel" no host atende**. Vantagem: liberdade total sobre a versão de kernel
reportada e sobre dispositivos fake (GPU, placa de som, etc.), porque o lado
kernel é 100% nosso. Segurança: a partition isola tudo — até uma `syscall`
direta de malware cai em nós, nunca no Windows real.

Custo honesto: é a **maior peça** do projeto — na prática um emulador de Windows
(loader de PE + ntdll/kernel32 sintéticos + superfície de syscalls + PEB/TEB +
gerência de memória). Cresce incrementalmente sobre o que já existe (piso WHP,
camada de devices, snapshots).

Componentes desta camada:
- **Long mode + paginação** no `MicroVm` (`SetupLongMode`) — ✅ feito.
- **Doorbell de syscall** (`MicroVm::MapIoTrap`) — o guest sinaliza uma syscall e
  o host lê registradores/memória e atende. ✅ feito (PoC `vmsyscall.exe`).
- **Tabela de syscalls NT** (synthetic kernel) — crescer caso a caso: memória
  (`NtAllocateVirtualMemory`...), arquivos (`NtCreateFile`→FS virtual no VHD),
  registro (`NtOpenKey`→hive virtual), processos/threads, devices fake.
- **Loader de PE** (host) — mapear seções na RAM do guest, resolver imports
  contra nossas DLLs sintéticas, aplicar relocations, montar PEB/TEB e pilha.
- **Trap real de `SYSCALL`** via LSTAR→página não mapeada + intercept de #PF
  (substitui o doorbell por porta, pegando até syscalls diretas) + separação
  CPL3 (user) / CPL0 (nosso kernel).

Sub-roadmap da personalidade NT (a partir do PoC atual):
1. ✅ Long mode + doorbell de syscall servido pelo host (`vmsyscall.exe`).
2. ✅ Blindagem — `vmguard.exe`: guest em **CPL3 (ring-3)** com `EFER.SCE=0`, então
   a instrução `SYSCALL` real (a nossa OU uma syscall direta de malware burlando
   nossas DLLs) gera **#UD**, interceptado via `ExceptionExitBitmap` +
   `ExtendedVmExits.ExceptionExit` e servido pelo kernel sintético. Instruções
   privilegiadas em CPL3 geram **#GP** (vetor 13), também interceptado e
   reportado como ação bloqueada. (Mais simples e robusto que LSTAR+#PF.)
3. ✅ Loader de PE mínimo — `src/microvm/host/PeLoader.*` + `vmpe.exe`: gera um
   PE32+ sem CRT, mapeia seções, aplica relocations DIR64, reescreve a IAT para
   stubs e roda. **Unificado com a blindagem (passo 2):** os stubs usam a
   instrução `SYSCALL` real e o `.exe` roda em ring-3, então o loader e o
   caminho evasion-resistant são o mesmo. (Falta: PEB/TEB + CRT pra binários
   comuns.)
4. 🟡 PEB/TEB + Ldr + heap — fundação feita: `src/microvm/host/ProcessEnv.*`
   (TEB/PEB/ProcessParameters + `BuildLoaderData` = `PEB_LDR_DATA` com lista
   circular de módulos) + `MicroVm::SetGsBase` + heap bump no `NtKernel`
   (`GetProcessHeap`/`HeapAlloc`/`VirtualAlloc`). Demos `vmpeb.exe` (GS→TEB→PEB
   →ImageBase→CommandLine) e `vmheap.exe` (anda no `Ldr`, aloca/escreve heap).
   ✅ **startup + TLS** — `vmstartup.exe` + `NtKernel`: `GetCurrentProcessId`/
   `ThreadId` (lê `TEB->ClientId`), `GetCommandLineW` (lê `PEB->params`),
   `GetModuleHandleW` (NULL=self, ou anda no `Ldr` comparando `BaseDllName`),
   `GetSystemTimeAsFileTime`/`QueryPerformanceCounter`/`GetTickCount` (cookie),
   `GetStartupInfoW`, e TLS (`TlsAlloc`/`TlsSetValue`/`TlsGetValue` nos slots do
   TEB).
   ✅ **ABI de pilha + binário real do gcc** — `NtKernel::StackArg` lê os args
   5+ (`[RSP+0x28+...]`) no momento da syscall. **`test/nt_gcc.c` compilado com
   `gcc -nostdlib` (importa de `KERNEL32.dll`) roda pelo `microsandbox`**: faz
   `GetStdHandle`+`WriteFile` (console), `CreateFileA` com `dwCreationDisposition`
   **na pilha** lido corretamente (`CREATE_ALWAYS`), `WriteFile`/`CloseHandle` no
   FS virtual, `ExitProcess` — com relatório e disco real intocado. (Falta:
   `NtAllocateVirtualMemory` real e a superfície completa de `ucrtbase`/
   `vcruntime` pra um `.exe` com CRT padrão chegar no `main`.)
5. ✅ FS virtual + relatório — `src/microvm/host/VirtualFs.*`, `NtKernel.*`,
   `ReportBuilder.*` + `vmfs.exe`: um PE faz `CreateFileA`/`WriteFile`/
   `CloseHandle`; o kernel sintético serve contra um FS em memória e registra um
   **timeline de comportamento** (created/written/bytes). O "loot" fica só no FS
   virtual — o disco real nunca é tocado. (Falta: ligar ao diff VHD do `PvDisk`,
   `CreateFileW`/paths reais.)
6. ✅ Registro virtual + perfil de sistema fake — `src/microvm/host/VirtualRegistry.*`
   + `SystemProfile` (Win7/10/11) + `vmreg.exe`: `RtlGetVersion` e o registro
   (`RegQueryValueA`/`RegSetValueA`) respondem com a **versão/ProductName/GPU que
   inventamos**. O mesmo PE vê Win11 ou Win7 conforme o perfil; a escrita de
   persistência (`HKCU\...\Run`) é capturada e o registro real nunca é tocado.
   (Falta: som/rede como devices, ABI Win32 com args de pilha.)
7. 🟡 Consolidação — `src/microvm/MicroSandbox.*` (fachada `Detonate()`) +
   `microsandbox.exe`: uma chamada junta MicroVm (ring-3, exceções) + PeLoader +
   `NtKernel` (FS/registro/perfil) + `ReportBuilder` num **relatório unificado**.
   Exposto também como `av detonate-micro`. (Falta: crescer a superfície até
   amostras reais — PEB/TEB+CRT, ABI com args de pilha, memória virtual.)
8. 🟡 GUI por **proxy pro host** (modelo Wine), não reimplementar o `win32k`
   (escala ReactOS/Wine, décadas). A execução fica isolada no sandbox; só o
   *desenho* roda nativo. ✅ **Primeiro tijolo** — `test/nt_gui.c` (gcc, importa
   `USER32.dll`) roda pelo `microsandbox`: `MessageBoxW` é encaminhado pelo
   `NtKernel` pro `user32` real e um diálogo **nativo aparece** (auto-dismiss),
   com o evento no relatório.
   - **Próximos tijolos:** `RegisterClass`/`CreateWindow`/`ShowWindow` + bombeamento
     de mensagens com o **`WndProc` chamado de volta DENTRO do guest** (re-entrada
     no vCPU) + GDI (`BeginPaint`/`TextOut`/`BitBlt`) com mapeamento de
     `HDC`/`HWND`/`HBITMAP` e marshaling de structs/pixels + threads no guest.
   - **Segurança (crítico):** o proxy NÃO pode desenhar no desktop real (canal de
     escape — o MEMZ pintaria sua tela). A versão segura renderiza numa
     **superfície que nós controlamos** (janela-filha/off-screen/`CreateDesktop`)
     exibida no viewer do sandbox — como o RemoteApp/Windows Sandbox fazem.

### Modelo B: carregar as DLLs reais (mini‑NT pelos SSNs reais)

Em vez de sintetizar cada função Win32, **carrega-se a `ntdll`/`kernel32` reais** e
intercepta-se só as **~488 syscalls `Nt*`** (os `Rtl*`/`Ldr*` vêm de graça por
serem código user-mode da própria `ntdll`). DLLs base: viável. `user32`/`gdi32`:
não (precisam do `win32k`) — por isso GUI usa o proxy acima.
- `tools/ntsyscalls.cpp` extrai a tabela real `nome→SSN` da `ntdll` viva (488
  entradas neste build; `NtCreateFile=0x55`, `NtAllocateVirtualMemory=0x18`…).
- ✅ **Provado** — `src/microvm/host/MiniNt.*` despacha pelos SSNs reais (lidos da
  `ntdll` viva); `MicroVm::MapPage2MB` mapeia o `KUSER_SHARED_DATA` em
  `0x7FFE0000`. `vmntdll.exe`: o `PeLoader` mapeia a **`ntdll.dll` real** (0x266000
  bytes, relocada), o guest **CHAMA os stubs reais** `NtAllocateVirtualMemory`/
  `NtTerminateProcess`, e o `MiniNt` atende — alocação real (`STATUS_SUCCESS`,
  escrita e lida de volta).
  ✅ **Subsistema de arquivos NT real** — `vmntfile.exe`: o guest chama os stubs
  reais `NtCreateFile`/`NtWriteFile`/`NtClose` com `OBJECT_ATTRIBUTES`/
  `UNICODE_STRING`/`IO_STATUS_BLOCK` reais (path `\??\C:\...`, `Buffer`/`Length`
  do `NtWriteFile` lidos da pilha), e o `MiniNt` serve contra o `VirtualFs` —
  arquivo capturado virtualmente, disco real intocado.
  ✅ **Seções (o core do loader)** — `vmntsect.exe`: o guest chama os stubs reais
  `NtCreateFile`→`NtCreateSection`→`NtMapViewOfSection`; o `MiniNt` cria uma seção
  **backed por arquivo** (do `VirtualFs`) e mapeia o conteúdo numa view, que o
  guest lê. É como o Windows mapeia dados.
  ✅ **Seção de imagem (`SEC_IMAGE`) — o mapeamento de DLL de verdade** —
  `vmntimage.exe`: o guest cria uma seção com `SEC_IMAGE` sobre uma **DLL real
  relocável** (`test/imgtest.dll`) no `VirtualFs` e a mapeia. O `MiniNt` não copia
  bytes crus: faz o **layout de PE completo** (`LayoutPeImage` em `MiniNt.cpp`) —
  headers + cada seção no seu RVA por `SizeOfImage`, mais as **relocações DIR64**
  para o delta de carga. O guest então **CHAMA o `Probe()` exportado** da imagem
  mapeada, que retorna `*g_ptr` (um ponteiro absoluto que só resolve dentro da
  view se a relocação foi aplicada): retornou `0xC0FFEE5` ✅ — prova de uma vez
  headers/seções no RVA certo, código executável rodando da imagem, e fixups de
  relocação.
  ✅ **Consultas + sync do loader** — `vmntquery.exe`: o que o `LdrpInitialize`
  pergunta antes de mapear qualquer DLL. O guest chama os stubs reais
  `NtQueryInformationProcess(ProcessBasicInformation)` (o `MiniNt` lê o GS base →
  `TEB+0x60` → devolve o `PebBaseAddress`), `NtQuerySystemInformation(SystemBasicInformation)`
  (1 CPU, páginas de 4 KiB, granularidade 64 KiB) e o trio de eventos
  `NtCreateEvent`/`NtSetEvent`/`NtWaitForSingleObject` (modelo single-thread: objeto
  sinalizado satisfaz na hora). Guest leu o PEB que montou, o nº de CPUs e um wait
  com `STATUS_SUCCESS` ✅.
  ✅ **Registro NT** — `vmntreg.exe`: o guest chama os stubs reais
  `NtOpenKey`/`NtQueryValueKey`/`NtSetValueKey` contra o namespace `\Registry`
  em memória do `MiniNt` (chaves `\Registry\Machine\...`, case-insensitive). Lê
  valores **seedados com o personality do sistema** com os tipos certos
  (`SafeDllSearchMode` = `REG_DWORD 1`, `ProductName` = `REG_SZ "Windows 11 Pro"`,
  via `KEY_VALUE_PARTIAL_INFORMATION`), e **captura uma escrita de persistência**
  (`Run\Updater = "...evil.exe"`) com round-trip de leitura — HKLM/HKCU real
  intocado.
  🟡 **O CHEFÃO — bring-up real do `LdrInitializeThunk`** — `vmldr.exe`: em vez de
  chamar `Nt*` à mão, aponta o vCPU pro **`LdrInitializeThunk` real da `ntdll`** (o
  entry que o próprio kernel do Windows usa numa thread nova), com ambiente de
  processo de verdade (TEB/PEB/ProcessParameters + `CONTEXT`, `RCX=&CONTEXT`,
  `RDX=ntdll base`) e deixa o **loader real dirigir**. O `MiniNt` atende e **traça
  cada syscall**; #UD/#GP/#PF interceptam pra reportar o ponto exato. Descobertas:
  - **SSE precisa estar ligado**: a `ntdll` usa `xorps`/`movaps`; sem `CR4.OSFXSR`
    dá #UD. Ligado `CR4 = PAE|OSFXSR|OSXMMEXCPT (0x620)` no `SetupLongMode`.
  - **Alinhamento da ABI**: `RSP ≡ 8 (mod 16)` na entrada (como se um `CALL`
    tivesse empilhado o retorno), senão os spills `movaps [rsp+k]` dão #GP.
  - Com isso o loader real roda **60+ syscalls fundo** no `LdrpInitializeProcess`:
    cookie de segurança, `NtProtectVirtualMemory`, heap via
    **`NtAllocateVirtualMemoryEx`**, NLS codepage, o scan inteiro de **Image File
    Execution Options / GlobalFlag** (~25 valores), Session Manager, ProductOptions,
    `ProcessCookie`, system info. Tudo servido pelo `MiniNt`.
  - 🔴 **O muro real**: depois de `NtWaitForAlertByThreadId`, o loader entra na sua
    **fila de trabalho lock-free / sincronização de thread-alert** (`LdrpDrainWorkQueue`)
    e gira num walk de lista (`ntdll!0xBBD0`) esperando progresso de threads worker
    que não existem no nosso modelo single-thread. Um **watchdog** (`MicroVm::Cancel`
    via `WHvCancelRunVirtualProcessor`) corta o spin sem-exit e revela o RIP.
  Conclusão estratégica: rodar o `LdrpInitialize` da `ntdll` até o fim esbarra no
  acoplamento dela com estado multi-thread interno — é exatamente por isso que
  **Wine e ReactOS reimplementam o loader** em vez de rodar o da Microsoft. Nosso
  probe provou que dá pra rodar o loader real fundo no bring-up e localizou com
  precisão onde o acoplamento trava.
  ✅ **O CAMINHO WINE — NOSSO loader sobe a `kernel32` real** —
  `src/microvm/host/WinLoader.*`: em vez de rodar o `LdrpInitialize` da `ntdll`,
  carregamos as DLLs nós mesmos — exatamente como Wine/ReactOS. O `WinLoader` é um
  loader recursivo host-side que: mapeia cada DLL como `SEC_IMAGE` (headers +
  seções no RVA + relocações DIR64), carrega as dependências recursivamente,
  resolve imports entre módulos (incluindo **forwarders** e a heurística de **API
  sets** `api-ms-win-*`/`ext-ms-win-*` → `kernelbase`/`ntdll`/`kernel32`) e faz
  patch de cada IAT. Provas, em três etapas:
  - **`vmwinload.exe`** (controlado): duas DLLs nossas — `libb.dll` importa `AddOne`
    de `liba.dll`. O loader mapeia as duas, resolve o import cruzado, faz patch da
    IAT, e o guest roda `libb!Compute(20)` (que chama `liba!AddOne` cruzando o
    limite de DLL) → **50** ✅, e `liba!Magic()` → `0x7E57` ✅.
  - **`vmwinsys.exe`** (real): aponta pra `C:\Windows\System32\kernel32.dll` e linka
    o grafo inteiro — **kernel32 + kernelbase + ntdll**, **2024 imports resolvidos,
    0 faltando** ✅. `GetProcAddress`/`LoadLibraryA` resolvem pra endereços reais no
    guest.
  - **`vmwincall.exe`** (capstone): o guest **CHAMA a export real `kernel32!lstrlenA`**
    numa string e recebe o comprimento certo (`24`) ✅ — código real da Microsoft
    executando no nosso sandbox de hardware, linkado 100% pelo nosso loader, **sem o
    `LdrpInitialize` da `ntdll`**. O caminho Wine funciona ponta a ponta.
  ✅ **Cobertura automática — chamar TODA export da `kernel32`** — `vmwinall.exe`:
  depois de linkar o grafo, dirige **cada uma das 1693 exports** automaticamente —
  reseta o vCPU, aponta o RIP pra função com args nulos e um trampolim `HLT` de
  retorno na pilha, roda (com o `MiniNt` servindo syscalls + TEB/PEB/KUSER reais),
  e um **watchdog por chamada** corta spins. Classifica cada resultado sem
  conhecer assinatura nenhuma. Resultado: **1317 / 1693 (77,8%) executam e retornam
  limpo** (`GetCurrentProcessId`→PID certo, `GetVersion`→versão real, etc.); ~17%
  giram em locks/waits sem estado inicializado, ~5% faltam/#UD em funções que
  precisam de heap/handles reais (CreateProcess, FindFirstFile, threadpool). É o
  mapa empírico exato do que falta — gerado automaticamente.
  ✅ **Heap + locks via hot-patch (técnica Wine) — `vmwininit.exe`** — descoberta
  chave: o `HeapCreate` real gira em **ntdll+0xBBDB**, o *mesmo* loop do chefão. Ou
  seja, `0xBBD0` não é do loader: é uma **primitiva central da `ntdll`** (lista de
  heaps do processo sob lock, com thread-alert) que tanto o loader quanto o heap
  usam, e que assume estado multi-thread inicializado — o muro comum. A solução é a
  do Wine: **hot-patch do *código*** das funções (cobre forwarders E chamadas
  internas). `ApplyWinePatches` (`WinLoader.cpp`) reescreve as `Rtl*Heap` da `ntdll`
  para `mov eax,<op>; syscall; ret` — o `MiniNt::DispatchHeap` as serve com um
  alocador bump simples — e os SRW/critical-section locks para no-ops (seguro
  single-thread). Resultado: `HeapCreate`/`HeapAlloc`/`HeapSize`/`HeapFree` reais
  fazem um round-trip completo (write+read verificado) ✅.
  ✅ **Salto na cobertura** — re-rodando o `vmwinall.exe` com os patches: o
  "returned" sobe de **1317 (77,8%) para 1589 (93,9%)** das 1693 exports, e os
  spins despencam de **289 para 24** (os no-ops de lock). Os ~6% restantes são
  funções que *devem* terminar (`ExitProcess`/`FatalAppExit`/`DebugBreak`) ou que
  precisam de args válidos/threadpool (`CreateProcess`, `GetDriveType` com buffer
  nulo). **93,9% da superfície real da `kernel32` executa e retorna limpo no nosso
  sandbox.**
  (Próximo: detonar um `.exe` real ligando-o contra a `kernel32` que subimos — o
  loop completo de análise; e modelar threadpool/fibers pros últimos %.)
  ✅ **Anti-VM / personality "bare-metal" — `vmspoof.exe` + `vmspoofreg.exe`** — um
  sample sandbox-aware não detona se cheirar VM. Cobrimos as duas vias que ele usa:
  - **CPUID (`vmspoof.exe`):** `MicroVm::SpoofCpuid(vendor, brand, nCpus)` arma o
    `WHvPartitionPropertyCodeCpuidExitList` **junto com o master enable
    `X64CpuidExit`** (bit 0 do `ExtendedVmExits` — sem ele o `CpuidExitList` é
    aceito mas *não* gera exits; achado validado nesta máquina). O `HandleCpuid`
    então **limpa o bit de hypervisor** (leaf 1 ECX[31]), **zera a leaf
    `0x40000000`** (vendor do hypervisor), e injeta vendor/brand/contagem de cores.
    O host real é um **AMD Ryzen 5 5500**; o guest vê um **Intel i7-10700K, 8
    cores, sem hypervisor** — bare metal.
  - **Registro (`vmspoofreg.exe`):** `MiniNt::SeedPersonality` semeia uma
    **identidade coerente de máquina-vítima** que malware vasculha: BIOS/placa
    **ASUS** (não "VMware"/"innotek"), CPU Intel, **GPU NVIDIA RTX 3070 + driver
    fake** (`nvlddmkm` registrado, sem módulo de kernel real), `MachineGuid`,
    `ComputerName`, `InstallDate`, dono, e uma lista de **programas instalados**
    (Chrome, Firefox, Office, Steam, Discord…). O guest lê tudo isso **pelos stubs
    reais da `ntdll`** (`NtOpenKey`/`NtQueryValueKey`) e nenhuma das probes
    devolve um *tell* de VM — a máquina lê como metal usado.
  - **CPU/RAM (`NtQuerySystemInformation`):** o default "1 CPU, 1 GiB" é tell de
    sandbox; agora `SystemBasicInformation` reporta **8 processadores e 16 GiB**
    (afinidade `0xFF`), e adicionamos `SystemProcessorInformation` (AMD64, family
    6 model 165) — coerente com o i7-10700K.
  - **Disco (`MiniNt::SeedFilesystem` + `VirtualFs`):** o `VirtualFs` ganhou uma
    **blocklist** de artefatos de VM (`vboxguest`, `vmci.sys`, `vmware`, `qemu`,
    `viostor`, `xenpci`, `prl_`, `sbiedll`…) que abrem com
    `STATUS_OBJECT_NAME_NOT_FOUND`, e um conjunto de **arquivos normais semeados**
    (kernel32, explorer, chrome, arquivos do usuário) que existem. O `Open` agora
    respeita a `CreateDisposition`: `FILE_OPEN` de um caminho ausente/bloqueado
    falha de verdade (antes *tudo* era criado on-open, o que fazia drivers de VM
    "existirem"). O `vmspoofreg.exe` prova as duas vias: `vmci.sys`/`VBoxGuest.sys`
    → NOT_FOUND, `kernel32.dll`/`notas.txt` → presentes.
  - **Rede (registro):** adaptador Intel real no class `{4d36e972-…}` (DriverDesc
    "Intel(R) Ethernet Connection (7) I219-V", `ven_8086`), sem `NetworkAddress`
    override — então nada de OUI de VM (00:05:69, 08:00:27…).
  ✅ **Anti-VM timing/execução — `vmspoofsys.exe`** — mais duas checagens que sample
  sandbox-aware faz, lidas do jeito que o malware lê:
  - **Uptime (`MiniNt::SeedSharedData`):** `GetTickCount64` lê o `KUSER_SHARED_DATA`
    direto; uma VM recém-bootada (segundos de uptime) é tell de sandbox. Semeamos o
    `TickCountQuad`/`InterruptTime`/`TickCountMultiplier` (0x0FA00000) para **~3 dias
    de uptime** (79h 21m). O guest lê `[0x7FFE0320]` e o host confirma o uptime alto.
  - **Lista de processos (`NtQuerySystemInformation` classe 5):** sandbox roda poucos
    processos e malware varre a lista por ferramentas de análise (`vmtoolsd`,
    `vboxservice`, `wireshark`, `procmon`, `x64dbg`…). Emitimos uma cadeia
    `SYSTEM_PROCESS_INFORMATION` crível de **46 processos** de desktop (svchost,
    explorer, chrome, steam, Defender…) e **nenhuma** ferramenta. O host caminha a
    lista (count ≥ 30, scan de nomes) — tudo limpo.
  ✅ **Anti-VM rede / MAC ao vivo — `vmspoofnet.exe`** — fecha a última via. Qualquer
  API que lê o MAC (`GetAdaptersInfo`/`GetAdaptersAddresses`/NDIS) bottom-outa, no
  kernel, numa **query de OID NDIS**. Implementamos o `NtDeviceIoControlFile`: ao
  receber `IOCTL_NDIS_QUERY_GLOBAL_STATS` (0x00170002) com `OID_802_3_PERMANENT_ADDRESS`
  /`CURRENT_ADDRESS`, devolve um MAC com **OUI Intel genuíno** (`8C:16:45:…`) — nunca
  um OUI de VM (VMware 00:05:69/00:0C:29/00:50:56, VirtualBox 08:00:27, Hyper-V
  00:15:5D, Xen 00:16:3E, QEMU 52:54:00). O `\Device\Tcp`/`\Device\Nsi` são semeados
  como presentes. O guest faz exatamente esse caminho (`NtCreateFile("\Device\Tcp")`
  → IOCTL) e o host valida que o OUI não está em nenhuma lista de VM. **A via de rede
  está 100% coberta** (identidade no registro + MAC ao vivo via o caminho real do NDIS).

## Roadmap de implementação realista (ordem de menor→maior risco)

1. **PoC WHP:** subir uma partition, mapear RAM, rodar um "hello world" de 16-bit/UEFI,
   ver VM-exits. (valida que WHP/virtualização está ligada na máquina.)
   ✅ **IMPLEMENTADO** — `src/microvm/vmm/WhpPoc.cpp` (alvo `vmpoc.exe`): cria a
   partition, mapeia 4 KiB em GPA 0x1000, roda guest real-mode de 13 bytes e
   intercepta os `OUT 0xE9` como VM-exits. O gate de prontidão é
   `src/microvm/host/Preflight.cpp` (comando `av vmcheck`), que detecta via
   `WHvGetCapability` se o hypervisor está rodando e, se não, como ligá-lo.
2. **PvDisk + VHD diferencial + boot de Windows PE** → boot mínimo no seu VMM.
   🟡 **PARCIAL** — a *camada de devices* está implementada e demonstrada:
   `src/microvm/vmm/MicroVmMonitor.*` (classe `MicroVm` + interface `IoDevice`),
   `devices/Devices.h` (`PvConsole`) e `devices/PvDisk.cpp` (disco diferencial
   copy-on-write). O alvo `vmdemo.exe` roda um guest que faz uma sobrescrita de
   boot sector estilo-MEMZ (512×0xCC em LBA 0): cai no `sandbox-disk.diff`, com
   `host disk opened: no`. **Falta** a parte grande: bootar um Windows PE real
   (firmware UEFI + AHCI/virtio + ACPI) — esforço de semanas, é o muro real para
   executar malware PE de verdade.
3. **PvChannel + GuestAgent** → transferir e executar a amostra; capturar exit code.
4. **Device-layer logging** (disco/rede) → já dá o relatório "MBR write" do MEMZ.
5. **VirtualNetwork** (blackhole→sinkhole→capture).
6. **SnapshotManager** (boot snapshot → restore rápido) → descartável + rápido.
   ✅ **IMPLEMENTADO** — `src/microvm/host/SnapshotManager.*` captura/restaura a
   RAM do guest + 29 registradores do vCPU. Demo `vmsnap.exe`: detona um guest
   que altera a memória, restaura o snapshot em ~100 µs (imagem de 1 MiB, sem
   reboot) e re-roda a partir do estado restaurado. É o "descartável" de fato:
   um `Restore` apaga toda mudança (RAM + CPU) em microssegundos.
7. **VMI** (perfis de offsets) → monitoramento furtivo de processos/memória.
8. **Telemetria in-guest opcional** (minifilter etc.) → contexto semântico rico.
9. **Anti-fingerprinting** (incremental) + **perfis 7/10/11**.
10. **ReportBuilder** completo (ChangeSet + timeline + screenshots).

> Onde o GuardianAV já ajuda hoje: o `Sandbox` in-OS atual (job + token low-IL +
> hidden desktop + monitor de FS/processos) é uma versão "L0 sem hypervisor" — bom
> para triagem rápida, **não** para MEMZ. O MicroVM acima é o upgrade que fecha o
> furo de hardware.
