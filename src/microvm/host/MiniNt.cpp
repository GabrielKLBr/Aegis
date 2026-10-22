// MiniNt.cpp - real-SSN syscall dispatch for the mini NT kernel.
#include "MiniNt.h"

#include <windows.h>
#include <cstring>
#include <cctype>

namespace av {
namespace microvm {

namespace {
class VmMemoryBackend final : public VirtualMemoryBackend {
public:
    explicit VmMemoryBackend(MicroVm& vm) : vm_(vm) {}
    GuestMemory Memory() const override { return GuestMemory(vm_.RamData(), vm_.RamSize()); }
    bool SetAccess(uint64_t address, uint64_t bytes, uint8_t access) override { return vm_.SetMemoryAccess(address, bytes, access); }
    void FailClosed() override { vm_.FailMemoryMappings(); }
private:
    MicroVm& vm_;
};

bool Overlaps(uint64_t address, uint64_t bytes, uint64_t other, uint64_t otherBytes) {
    if (bytes > UINT64_MAX - address || otherBytes > UINT64_MAX - other) return true;
    return bytes && otherBytes && address < other + otherBytes && other < address + bytes;
}
uint64_t Reg(MicroVm& vm, WHV_REGISTER_NAME n) {
    WHV_REGISTER_VALUE v; ZeroMemory(&v, sizeof(v));
    vm.GetRegisters(&n, 1, &v);
    return v.Reg64;
}
void SetReg(MicroVm& vm, WHV_REGISTER_NAME n, uint64_t val) {
    WHV_REGISTER_VALUE v; ZeroMemory(&v, sizeof(v));
    v.Reg64 = val;
    vm.SetRegisters(&n, 1, &v);
}
uint64_t Rd64(MicroVm& vm, uint64_t gva) {
    uint64_t v = 0;
    if (vm.CanAccessMemory(gva, sizeof(v), 1)) GuestMemory(vm.RamData(), vm.RamSize()).Read(gva, v);
    return v;
}
uint32_t Rd32(MicroVm& vm, uint64_t gva) {
    uint32_t v = 0;
    if (vm.CanAccessMemory(gva, sizeof(v), 1)) GuestMemory(vm.RamData(), vm.RamSize()).Read(gva, v);
    return v;
}
void Wr64(MicroVm& vm, uint64_t gva, uint64_t v) {
    if (vm.CanAccessMemory(gva, sizeof(v), 2)) GuestMemory(vm.RamData(), vm.RamSize()).Write(gva, v);
}
void Wr32(MicroVm& vm, uint64_t gva, uint32_t v) {
    if (vm.CanAccessMemory(gva, sizeof(v), 2)) GuestMemory(vm.RamData(), vm.RamSize()).Write(gva, v);
}
void Wr16(MicroVm& vm, uint64_t gva, uint16_t v) {
    if (vm.CanAccessMemory(gva, sizeof(v), 2)) GuestMemory(vm.RamData(), vm.RamSize()).Write(gva, v);
}
// GS base at a syscall trap is the TEB (SYSCALL #UD's before any swapgs runs).
uint64_t GsBase(MicroVm& vm) {
    WHV_REGISTER_NAME n = WHvX64RegisterGs;
    WHV_REGISTER_VALUE v; ZeroMemory(&v, sizeof(v));
    vm.GetRegisters(&n, 1, &v);
    return v.Segment.Base;
}
uint64_t StackArg(MicroVm& vm, int n) {  // 1-based; n>=5 on the stack
    uint64_t rsp = Reg(vm, WHvX64RegisterRsp);
    if (n < 5) return 0;
    const auto offset = 0x28 + static_cast<uint64_t>(n - 5) * 8;
    if (rsp > (std::numeric_limits<uint64_t>::max)() - offset) return 0;
    return Rd64(vm, rsp + offset);
}
uint16_t Rd16(MicroVm& vm, uint64_t gva) {
    uint16_t v = 0;
    if (vm.CanAccessMemory(gva, sizeof(v), 1)) GuestMemory(vm.RamData(), vm.RamSize()).Read(gva, v);
    return v;
}
// Read a UNICODE_STRING's buffer (length in bytes) into a narrow path, stripping
// the NT "\??\" / "\\?\" device prefix so it reads like a DOS path.
std::string ReadNtPath(MicroVm& vm, uint64_t unicodeStringPtr) {
    const GuestMemory memory(vm.RamData(), vm.RamSize());
    if (!memory.Contains(unicodeStringPtr, 16) || !vm.CanAccessMemory(unicodeStringPtr, 16, 1)) return {};
    uint16_t lenBytes = Rd16(vm, unicodeStringPtr + 0);
    uint64_t buf = Rd64(vm, unicodeStringPtr + 8);
    if ((lenBytes & 1) || !memory.Contains(buf, lenBytes) || !vm.CanAccessMemory(buf, lenBytes, 1)) return {};
    const uint8_t* g = static_cast<const uint8_t*>(vm.RamData());
    std::string s;
    for (uint64_t i = 0; i < lenBytes; i += 2)
        s.push_back((char)g[buf + i]);
    if (s.rfind("\\??\\", 0) == 0) s = s.substr(4);
    return s;
}
std::string ToLower(std::string s) {
    for (auto& ch : s) ch = (char)std::tolower((unsigned char)ch);
    return s;
}

// --- PE image-section support ---------------------------------------------
// An image section (SEC_IMAGE) is not a raw byte copy: the file is a PE that
// must be laid out in memory by its SizeOfImage / per-section RVAs, with base
// relocations fixed up for the load delta. This is exactly how the NT loader
// maps a DLL. The two helpers below parse a PE held in a VirtualFs file.

const IMAGE_NT_HEADERS* PeHeaders(const std::vector<uint8_t>& file) {
    if (file.size() < sizeof(IMAGE_DOS_HEADER)) return nullptr;
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    if ((size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS) > file.size()) return nullptr;
    auto nth = reinterpret_cast<const IMAGE_NT_HEADERS*>(file.data() + dos->e_lfanew);
    if (nth->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    if (nth->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return nullptr;
    return nth;
}

uint64_t PeSizeOfImage(const std::vector<uint8_t>& file) {
    auto nth = PeHeaders(file);
    return nth ? nth->OptionalHeader.SizeOfImage : 0;
}

// Lay out a PE image at guest GPA `base`, applying DIR64 relocations for the
// load delta. Returns SizeOfImage on success, 0 on parse failure.
uint64_t LayoutPeImage(uint8_t* g, uint64_t base, uint64_t ramSize,
                       const std::vector<uint8_t>& file) {
    auto nth = PeHeaders(file);
    if (!nth) return 0;
    uint64_t sizeOfImage   = nth->OptionalHeader.SizeOfImage;
    uint64_t sizeOfHeaders = nth->OptionalHeader.SizeOfHeaders;
    uint64_t preferredBase = nth->OptionalHeader.ImageBase;
    if (base + sizeOfImage > ramSize) return 0;

    ZeroMemory(g + base, (size_t)sizeOfImage);
    size_t hdrCopy = (size_t)(sizeOfHeaders < file.size() ? sizeOfHeaders : file.size());
    CopyMemory(g + base, file.data(), hdrCopy);

    auto sec = IMAGE_FIRST_SECTION(nth);
    for (unsigned i = 0; i < nth->FileHeader.NumberOfSections; ++i) {
        uint64_t va  = sec[i].VirtualAddress;
        uint64_t raw = sec[i].PointerToRawData;
        uint64_t rsz = sec[i].SizeOfRawData;
        uint64_t vsz = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : rsz;
        uint64_t copy = rsz < vsz ? rsz : vsz;
        if (raw + copy > file.size()) copy = raw < file.size() ? file.size() - raw : 0;
        if (copy && base + va + copy <= ramSize)
            CopyMemory(g + base + va, file.data() + raw, (size_t)copy);
    }

    int64_t delta = (int64_t)base - (int64_t)preferredBase;
    auto& rel = nth->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (delta != 0 && rel.Size && rel.VirtualAddress) {
        uint64_t p = rel.VirtualAddress, end = p + rel.Size;
        while (p + 8 <= end && base + p + 8 <= ramSize) {
            uint32_t pageRva, blockSize;
            CopyMemory(&pageRva,   g + base + p + 0, 4);
            CopyMemory(&blockSize, g + base + p + 4, 4);
            if (blockSize < 8 || p + blockSize > end) break;
            uint32_t count = (blockSize - 8) / 2;
            for (uint32_t k = 0; k < count; ++k) {
                uint16_t ent;
                CopyMemory(&ent, g + base + p + 8 + (uint64_t)k * 2, 2);
                if ((ent >> 12) == IMAGE_REL_BASED_DIR64) {
                    uint64_t loc = base + pageRva + (ent & 0xFFF);
                    if (loc + 8 <= ramSize) {
                        uint64_t v; CopyMemory(&v, g + loc, 8);
                        v += delta; CopyMemory(g + loc, &v, 8);
                    }
                }
            }
            p += blockSize;
        }
    }
    return sizeOfImage;
}
} // namespace

uint64_t MiniNt::ImageSize(const std::vector<uint8_t>& file) { return PeSizeOfImage(file); }

MiniNt::MiniNt() { LoadSyscallTable(); SeedRegistry(); SeedFilesystem(); }

// Anti-VM, disk edition: a real machine has the usual user/system files present
// and -- crucially -- NONE of the kernel-driver/device files a VM or sandbox
// installs. Malware probes both: it opens `C:\Windows\System32\drivers\vmci.sys`
// or `\\.\VBoxGuest` and bails if they exist. Our VirtualFs reports those absent
// (blocklist) while seeding a believable lived-in set of ordinary files.
void MiniNt::SeedFilesystem() {
    // VM / sandbox artifact files + control devices -> always "not found".
    const char* tells[] = {
        // VirtualBox
        "vboxguest", "vboxsf", "vboxmouse", "vboxvideo", "vboxservice", "vboxtray",
        "vboxdisp", "vboxhook", "vboxminirdr",
        // VMware
        "vmmouse", "vmhgfs", "vmci.sys", "vmmemctl", "vmtools", "vmware",
        "vmx_svga", "vmusbmouse", "vmrawdsk", "vmnet", "\\\\.\\hgfs",
        // QEMU / KVM / virtio
        "qemu", "viostor", "vioscsi", "netkvm", "balloon.sys", "vioser",
        // Xen
        "xenpci", "xennet", "xenvbd", "xenevtchn", "xensvc",
        // Parallels / Sandboxie / generic
        "prl_", "prleth", "sbiedll", "sbiedrv", "\\\\.\\sbie",
    };
    for (const char* t : tells) fs_.Block(t);

    // A lived-in disk: ordinary files a real desktop carries (presence checks).
    const char* normals[] = {
        "C:\\Windows\\System32\\kernel32.dll",
        "C:\\Windows\\System32\\ntdll.dll",
        "C:\\Windows\\System32\\drivers\\nvlddmkm.sys",   // the real NVIDIA driver
        "C:\\Windows\\explorer.exe",
        "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
        "C:\\Program Files (x86)\\Steam\\steam.exe",
        "C:\\Users\\Gabriel\\Desktop\\notas.txt",
        "C:\\Users\\Gabriel\\Documents\\orcamento.xlsx",
        "C:\\Users\\Gabriel\\Downloads\\setup.exe",
        // Network devices a MAC query opens (serviced by NtDeviceIoControlFile).
        "\\Device\\Tcp",
        "\\Device\\Nsi",
    };
    for (const char* p : normals) fs_.Seed(p);
}

// Anti-VM, timing edition: a freshly-booted box with seconds of uptime is a
// sandbox tell (analysts spin VMs up per-sample). GetTickCount/GetTickCount64
// read KUSER_SHARED_DATA directly, so we seed it to ~3 days of uptime. The page
// is mapped at VA 0x7FFE0000 (2 MiB page at 0x7FE00000); KUSER sits 0x1E0000
// into `backingGpa`. Call after MapPage2MB + zeroing the backing page.
void MiniNt::SeedSharedData(MicroVm& vm, uint64_t backingGpa) {
    const uint64_t kusd = backingGpa + 0x1E0000;
    auto ksysTime = [&](uint64_t off, uint64_t v) {   // KSYSTEM_TIME {Low, High1, High2}
        Wr32(vm, kusd + off + 0, (uint32_t)v);
        Wr32(vm, kusd + off + 4, (uint32_t)(v >> 32));
        Wr32(vm, kusd + off + 8, (uint32_t)(v >> 32));
    };
    const uint64_t uptimeMs = 3ull*24*3600*1000 + 7ull*3600*1000 + 21ull*60*1000; // 3d 7h 21m
    const uint32_t mult = 0x0FA00000;                 // standard TickCountMultiplier
    Wr32(vm, kusd + 0x004, mult);                     // TickCountMultiplier
    Wr32(vm, kusd + 0x000, (uint32_t)uptimeMs);       // TickCountLowDeprecated
    ksysTime(0x320, (uptimeMs << 24) / mult);         // TickCount (TickCountQuad)
    ksysTime(0x008, uptimeMs * 10000);                // InterruptTime (100 ns units)
    ksysTime(0x014, 0x01DCC0F000000000ull);           // SystemTime (~2026, plausible)
    Wr32(vm, kusd + 0x2C4, 0x0A);                     // NtMajorVersion = 10
    Wr32(vm, kusd + 0x2C8, 0x00);                     // NtMinorVersion = 0
}

void MiniNt::SeedRegistry() {
    auto sz = [](const std::string& s) {           // narrow -> REG_SZ (UTF-16LE)
        std::vector<uint8_t> b;
        for (char ch : s) { b.push_back((uint8_t)ch); b.push_back(0); }
        b.push_back(0); b.push_back(0);             // null terminator
        return b;
    };
    auto dw = [](uint32_t v) {                      // REG_DWORD (LE)
        return std::vector<uint8_t>{ (uint8_t)v, (uint8_t)(v>>8), (uint8_t)(v>>16), (uint8_t)(v>>24) };
    };
    // The handful of keys the loader/CRT actually read early. Paths are stored
    // lowercased (the registry is case-insensitive).
    registry_["\\registry\\machine\\system\\currentcontrolset\\control\\session manager"]
             ["safedllsearchmode"] = { 4 /*REG_DWORD*/, dw(1) };
    auto& cv = registry_["\\registry\\machine\\software\\microsoft\\windows nt\\currentversion"];
    cv["productname"]        = { 1 /*REG_SZ*/, sz("Windows 11 Pro") };
    cv["currentbuild"]       = { 1, sz("22631") };
    cv["currentbuildnumber"] = { 1, sz("22631") };
    cv["currentversion"]     = { 1, sz("6.3") };
    // Force the loader single-threaded: MaxLoaderThreads=1 disables the parallel
    // loader work queue, whose lock-free drain otherwise spins forever without
    // worker threads. The loader queries this under the image's IFEO key.
    const char* ifeo = "\\registry\\machine\\software\\microsoft\\windows nt\\currentversion\\image file execution options\\";
    registry_[ifeo]["maxloaderthreads"] = { 4 /*REG_DWORD*/, dw(1) };

    SeedPersonality(sz, dw);
}
void MiniNt::ConfigureHardware(const guardian::hardware::Info& info) {
    if(!guardian::hardware::Valid(info,info.ramBytes))throw std::invalid_argument("Invalid MiniNt hardware profile");
    hardware_=info;
    for(auto it=registry_.begin();it!=registry_.end();){const auto& key=it->first;
        if(key.starts_with("\\registry\\machine\\hardware")||key.starts_with("\\registry\\machine\\system\\currentcontrolset\\enum")||
            key.starts_with("\\registry\\machine\\system\\currentcontrolset\\control\\video")||key.starts_with("\\registry\\machine\\system\\currentcontrolset\\control\\class\\{4d36e96")||
            key=="\\registry\\machine\\system\\currentcontrolset\\services\\nvlddmkm")it=registry_.erase(it);else ++it;}
    guardian::hardware::RegistryValue value{};
    for(uint32_t n=0;guardian::hardware::RegistryAt(info,n,value);++n){
        std::string path="\\registry\\machine\\";path+=value.key;for(auto& c:path)c=guardian::hardware::Fold(c);
        std::string name=value.name;for(auto& c:name)c=guardian::hardware::Fold(c);
        std::vector<uint8_t> bytes;for(uint32_t k=0;value.value[k];++k){bytes.push_back(uint8_t(value.value[k]));bytes.push_back(0);}bytes.push_back(0);bytes.push_back(0);
        registry_[path][name]={1,std::move(bytes)};
        for(size_t at=path.find('\\',1);at!=path.npos;at=path.find('\\',at+1))registry_.try_emplace(path.substr(0,at));
    }
}

// Build a believable bare-metal "victim machine" identity in the registry so
// sandbox-aware malware that probes for VM/sandbox artifacts detonates instead
// of bailing. Every value here is one a *real* Intel desktop carries and that
// VMs/sandboxes either lack or set to a tell-tale string ("VMware", "VirtualBox",
// "QEMU", "Xen", "innotek", "VBOX"). The identity is consistent with the CPUID
// spoof (GenuineIntel i7-10700K) -- a single coherent persona, not a grab-bag.
void MiniNt::SeedPersonality(const std::function<std::vector<uint8_t>(const std::string&)>& sz,
                             const std::function<std::vector<uint8_t>(uint32_t)>& dw) {
    auto multisz = [](std::initializer_list<std::string> items) {  // REG_MULTI_SZ
        std::vector<uint8_t> b;
        for (const auto& s : items) {
            for (char ch : s) { b.push_back((uint8_t)ch); b.push_back(0); }
            b.push_back(0); b.push_back(0);
        }
        b.push_back(0); b.push_back(0);            // extra null = end of list
        return b;
    };

    // --- BIOS / system manufacturer (the #1 anti-VM string check) -----------
    auto& bios = registry_["\\registry\\machine\\hardware\\description\\system\\bios"];
    bios["systemmanufacturer"]  = { 1, sz("ASUSTeK COMPUTER INC.") };
    bios["systemproductname"]   = { 1, sz("ROG STRIX Z490-E GAMING") };
    bios["systemfamily"]        = { 1, sz("To be filled by O.E.M.") };
    bios["systemversion"]       = { 1, sz("Rev 1.xx") };
    bios["systemsku"]           = { 1, sz("SKU") };
    bios["baseboardmanufacturer"] = { 1, sz("ASUSTeK COMPUTER INC.") };
    bios["baseboardproduct"]    = { 1, sz("ROG STRIX Z490-E GAMING") };
    bios["biosvendor"]          = { 1, sz("American Megatrends Inc.") };
    bios["biosversion"]         = { 1, sz("1003") };
    bios["biosreleasedate"]     = { 1, sz("07/24/2020") };

    auto& sysd = registry_["\\registry\\machine\\hardware\\description\\system"];
    sysd["systembiosversion"]   = { 7 /*REG_MULTI_SZ*/, multisz({"ALASKA - 1072009", "1003"}) };
    sysd["videobiosversion"]    = { 7, multisz({"NVIDIA GeForce RTX 3070"}) };
    sysd["systembiosdate"]      = { 1, sz("07/24/20") };

    // --- CPU description (consistent with the CPUID i7-10700K spoof) ---------
    auto& cpu = registry_["\\registry\\machine\\hardware\\description\\system\\centralprocessor\\0"];
    cpu["processornamestring"]  = { 1, sz("Intel(R) Core(TM) i7-10700K CPU @ 3.80GHz") };
    cpu["identifier"]           = { 1, sz("Intel64 Family 6 Model 165 Stepping 5") };
    cpu["vendoridentifier"]     = { 1, sz("GenuineIntel") };
    cpu["~mhz"]                 = { 4, dw(3800) };
    cpu["featureset"]           = { 4, dw(0x21193FFF) };

    // --- GPU adapter + a fake driver that does nothing ----------------------
    // Display-adapter class {4d36e968-...}. Malware reads DriverDesc/ProviderName
    // looking for "VMware SVGA", "VirtualBox Graphics", "Microsoft Basic Display".
    const char* gpu = "\\registry\\machine\\system\\currentcontrolset\\control\\class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000";
    auto& g = registry_[gpu];
    g["driverdesc"]    = { 1, sz("NVIDIA GeForce RTX 3070") };
    g["providername"]  = { 1, sz("NVIDIA") };
    g["drivername"]    = { 1, sz("nvlddmkm.sys") };
    g["driverversion"] = { 1, sz("31.0.15.3713") };
    g["hardwareinformation.adapterstring"] = { 1, sz("NVIDIA GeForce RTX 3070") };
    g["hardwareinformation.memorysize"]    = { 4, dw(0x20000000) };  // 8 GiB VRAM
    g["matchingdeviceid"] = { 1, sz("pci\\ven_10de&dev_2484") };
    // The "fake driver that does nothing": a registered service entry with a
    // believable image path and a running state, but no actual kernel module.
    auto& nvsvc = registry_["\\registry\\machine\\system\\currentcontrolset\\services\\nvlddmkm"];
    nvsvc["type"]         = { 4, dw(1) };       // SERVICE_KERNEL_DRIVER
    nvsvc["start"]        = { 4, dw(3) };       // SERVICE_DEMAND_START
    nvsvc["imagepath"]    = { 1, sz("\\SystemRoot\\System32\\DriverStore\\FileRepository\\nv_dispi.inf_amd64\\nvlddmkm.sys") };
    nvsvc["displayname"]  = { 1, sz("NVIDIA Display Driver Service") };

    // --- Network adapter (anti-VM: a real Intel NIC, not a VM/virtio adapter) -
    // Net class {4d36e972-...}. Malware reads DriverDesc looking for "VMware
    // Accelerated", "VirtualBox", "Red Hat VirtIO Ethernet". A registry NetworkAddress
    // override would show a VM OUI (00:05:69, 08:00:27...) -- a real machine has none.
    const char* nic = "\\registry\\machine\\system\\currentcontrolset\\control\\class\\{4d36e972-e325-11ce-bfc1-08002be10318}\\0000";
    auto& n = registry_[nic];
    n["driverdesc"]    = { 1, sz("Intel(R) Ethernet Connection (7) I219-V") };
    n["providername"]  = { 1, sz("Intel") };
    n["matchingdeviceid"] = { 1, sz("pci\\ven_8086&dev_15bc") };  // 8086 = Intel
    n["componentid"]   = { 1, sz("pci\\ven_8086&dev_15bc") };
    // No NetworkAddress override -> the adapter uses its (non-VM) burned-in MAC.

    // --- Machine identity: MachineGuid, owner, ComputerName, InstallDate -----
    registry_["\\registry\\machine\\software\\microsoft\\cryptography"]["machineguid"]
        = { 1, sz("a7f3c2e9-1b84-4d6f-9c05-2e8b7a1d4f60") };
    auto& cv2 = registry_["\\registry\\machine\\software\\microsoft\\windows nt\\currentversion"];
    cv2["registeredowner"]        = { 1, sz("Gabriel") };
    cv2["registeredorganization"] = { 1, sz("") };
    cv2["productid"]              = { 1, sz("00330-80000-00000-AA483") };
    cv2["installdate"]            = { 4, dw(0x6347B280) };   // ~Oct 2022, fixed
    cv2["editionid"]              = { 1, sz("Professional") };
    cv2["systemroot"]             = { 1, sz("C:\\Windows") };
    const char* cn = "\\registry\\machine\\system\\currentcontrolset\\control\\computername\\computername";
    registry_[cn]["computername"] = { 1, sz("DESKTOP-K7F2M9A") };
    registry_["\\registry\\machine\\system\\currentcontrolset\\control\\computername\\activecomputername"]
        ["computername"] = { 1, sz("DESKTOP-K7F2M9A") };

    // --- Installed programs (Uninstall keys) -- a lived-in machine -----------
    // Sandboxes are pristine; real users have a browser, Office, comms, games.
    auto prog = [&](const char* key, const std::string& name, const std::string& ver,
                    const std::string& pub) {
        // Registry paths are stored lowercased (queries lowercase before lookup).
        std::string sub = key; for (char& ch : sub) ch = (char)tolower((unsigned char)ch);
        std::string base = std::string("\\registry\\machine\\software\\microsoft\\windows"
                                       "\\currentversion\\uninstall\\") + sub;
        auto& k = registry_[base];
        k["displayname"]    = { 1, sz(name) };
        k["displayversion"] = { 1, sz(ver) };
        k["publisher"]      = { 1, sz(pub) };
    };
    prog("Google Chrome",         "Google Chrome",                 "125.0.6422.142", "Google LLC");
    prog("Mozilla Firefox",       "Mozilla Firefox (x64 en-US)",   "126.0",          "Mozilla");
    prog("{O365HomePremRetail}",  "Microsoft 365 - en-us",         "16.0.17628.20144","Microsoft Corporation");
    prog("Steam",                 "Steam",                         "2.10.91.91",     "Valve Corporation");
    prog("{Discord}",             "Discord",                       "1.0.9036",       "Discord Inc.");
    prog("{7-Zip}",               "7-Zip 23.01 (x64)",             "23.01",          "Igor Pavlov");
    prog("{VLC}",                 "VLC media player",              "3.0.20",         "VideoLAN");
    prog("{NVIDIA}",              "NVIDIA Graphics Driver 537.13", "537.13",         "NVIDIA Corporation");

    // --- User profile -------------------------------------------------------
    const char* pl = "\\registry\\machine\\software\\microsoft\\windows nt\\currentversion"
                     "\\profilelist\\s-1-5-21-3402918240-1186875013-2287327861-1001";
    registry_[pl]["profileimagepath"] = { 1, sz("C:\\Users\\Gabriel") };
}

void MiniNt::LoadSyscallTable() {
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    if (!nt) return;
    auto base = reinterpret_cast<uint8_t*>(nt);
    auto dos  = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nth  = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto dir  = nth->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    auto exp  = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
    auto names = reinterpret_cast<uint32_t*>(base + exp->AddressOfNames);
    auto ords  = reinterpret_cast<uint16_t*>(base + exp->AddressOfNameOrdinals);
    auto funcs = reinterpret_cast<uint32_t*>(base + exp->AddressOfFunctions);
    for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
        const char* nm = reinterpret_cast<const char*>(base + names[i]);
        if (!(nm[0] == 'N' && nm[1] == 't')) continue;
        uint8_t* fn = base + funcs[ords[i]];
        int off = (fn[0] == 0xF3 && fn[1] == 0x0F && fn[2] == 0x1E && fn[3] == 0xFA) ? 4 : 0;
        if (fn[off] == 0x4C && fn[off+1] == 0x8B && fn[off+2] == 0xD1 && fn[off+3] == 0xB8) {
            uint32_t ssn; std::memcpy(&ssn, fn + off + 4, 4);
            ssnToName_[ssn] = nm;
            nameToSsn_[nm] = ssn;
        }
    }
}

int MiniNt::SsnOf(const std::string& n) const {
    auto it = nameToSsn_.find(n);
    return it == nameToSsn_.end() ? -1 : (int)it->second;
}
std::string MiniNt::NameOf(uint32_t ssn) const {
    auto it = ssnToName_.find(ssn);
    return it == ssnToName_.end() ? std::string() : it->second;
}

void MiniNt::DispatchHeap(MicroVm& vm, uint32_t op) {
    // ntdll exports are patched to our syscall sentinels. The real Windows heap
    // implementation is bypassed; allocation/free/resize happen in GuestHeap.
    const GuestMemory memory(vm.RamData(), vm.RamSize());
    const auto handle = Reg(vm, WHvX64RegisterRcx);
    const auto flags = Reg(vm, WHvX64RegisterRdx);
    switch (op) {
        case 0xF0000001: {  // RtlAllocateHeap(heap, flags, size[r8])
            SetReg(vm, WHvX64RegisterRax, heap_.Allocate(memory, handle, flags, Reg(vm, WHvX64RegisterR8)));
            return;
        }
        case 0xF0000002:    // RtlFreeHeap(heap, flags, pointer[r8])
            SetReg(vm, WHvX64RegisterRax, heap_.Free(memory, handle, flags, Reg(vm, WHvX64RegisterR8)) ? 1 : 0); return;
        case 0xF0000003:    // Return the single configured heap; no fabricated fallback.
            SetReg(vm, WHvX64RegisterRax, heap_.Handle(memory)); return;
        case 0xF0000004: {  // RtlReAllocateHeap(heap, flags, ptr[r8], size[r9])
            SetReg(vm, WHvX64RegisterRax, heap_.Reallocate(memory, handle, flags,
                Reg(vm, WHvX64RegisterR8), Reg(vm, WHvX64RegisterR9))); return;
        }
        case 0xF0000005: {  // RtlSizeHeap(heap, flags, ptr[r8])
            SetReg(vm, WHvX64RegisterRax, heap_.Size(memory, handle, flags, Reg(vm, WHvX64RegisterR8))); return;
        }
    }
    SetReg(vm, WHvX64RegisterRax, 0);
}

bool MiniNt::DispatchVirtualMemory(MicroVm& vm, const std::string& name) {
    using M = GuestVirtualMemory;
    const bool allocate = name == "NtAllocateVirtualMemory" || name == "NtAllocateVirtualMemoryEx";
    const bool extended = name == "NtAllocateVirtualMemoryEx";
    const bool query = name == "NtQueryVirtualMemory", protectCall = name == "NtProtectVirtualMemory";
    if (!allocate && !query && !protectCall && name != "NtFreeVirtualMemory") return false;
    const GuestMemory memory(vm.RamData(), vm.RamSize());
    VmMemoryBackend backend(vm);
    auto finish = [&](uint32_t status) {
        SetReg(vm, WHvX64RegisterRax, status);
        report_.Add(ReportBuilder::Cat::Process, name, "", "NTSTATUS " + std::to_string(status));
        return true;
    };
    auto access = [&](uint64_t pointer, uint64_t bytes, uint8_t flags) {
        return pointer && vm.CanAccessMemory(pointer, bytes, flags) && virtualMemory_.Allows(pointer, bytes, flags);
    };
    auto stack = [&](unsigned index, uint64_t& value) {
        const auto rsp = Reg(vm, WHvX64RegisterRsp), offset = 0x28ull + (index - 5ull) * 8;
        return rsp <= UINT64_MAX - offset && access(rsp + offset, 8, 1) && memory.Read(rsp + offset, value);
    };
    if (Reg(vm, WHvX64RegisterRcx) != UINT64_MAX) return finish(M::InvalidHandle);
    if (heap_.Base() && heap_.Base() < virtualMemory_.End() && virtualMemory_.Base() < heap_.End()) return finish(M::Conflict);
    if (query) {
        if (Reg(vm, WHvX64RegisterR8) != 0) return finish(0xC0000003); // MemoryBasicInformation only
        uint64_t length = 0, returnLength = 0;
        if (!stack(5, length) || !stack(6, returnLength)) return finish(M::AccessViolation);
        if (returnLength && !access(returnLength, 8, 2)) return finish(M::AccessViolation);
        if (length < sizeof(M::BasicInformation)) {
            if (returnLength) memory.Write(returnLength, uint64_t(sizeof(M::BasicInformation)));
            return finish(0xC0000004); // STATUS_INFO_LENGTH_MISMATCH
        }
        const auto out = Reg(vm, WHvX64RegisterR9);
        if (!access(out, sizeof(M::BasicInformation), 2)) return finish(M::AccessViolation);
        if (returnLength && Overlaps(out, sizeof(M::BasicInformation), returnLength, 8)) return finish(M::InvalidParameter);
        M::BasicInformation information{};
        const auto status = virtualMemory_.Query(backend, Reg(vm, WHvX64RegisterRdx), information);
        if (!status) {
            memory.Write(out, information);
            if (returnLength) memory.Write(returnLength, uint64_t(sizeof(information)));
        }
        return finish(status);
    }
    const auto basePointer = Reg(vm, WHvX64RegisterRdx);
    const auto sizePointer = Reg(vm, extended || !allocate ? WHvX64RegisterR8 : WHvX64RegisterR9);
    if (!access(basePointer, 8, 3) || !access(sizePointer, 8, 3)) return finish(M::AccessViolation);
    if (Overlaps(basePointer, 8, sizePointer, 8)) return finish(M::InvalidParameter);
    uint64_t address = 0, bytes = 0; memory.Read(basePointer, address); memory.Read(sizePointer, bytes);
    if (address && ThreadMemoryOverlap(address, bytes ? bytes : 1)) return finish(GuestObjects::AccessDenied);
    uint32_t status = M::InvalidParameter;
    if (allocate) {
        uint64_t type = 0, protection = 0;
        if (extended) {
            // (Process, Base*, Size*, Type, Protection[5], Parameters[6], Count[7])
            uint64_t parameters = 0, count = 0;
            type = Reg(vm, WHvX64RegisterR9);
            if (!stack(5, protection) || !stack(6, parameters) || !stack(7, count)) return finish(M::AccessViolation);
            if (static_cast<uint32_t>(count)) return finish(M::NotSupported);
        } else if (!stack(5, type) || !stack(6, protection)) return finish(M::AccessViolation);
        status = virtualMemory_.Allocate(backend, address, bytes, static_cast<uint32_t>(type),
            static_cast<uint32_t>(protection), extended ? 0 : Reg(vm, WHvX64RegisterR8));
    } else {
        uint64_t low = address, span = bytes;
        if (protectCall || bytes) {
            if (!bytes || bytes > UINT64_MAX - address || address + bytes > UINT64_MAX - 4095) return finish(M::InvalidParameter);
            low = address & ~uint64_t(4095); span = ((address + bytes + 4095) & ~uint64_t(4095)) - low;
        } else {
            uint32_t type = 0;
            virtualMemory_.ReservationAt(address, low, span, type);
        }
        if (Overlaps(low, span, basePointer, 8) || Overlaps(low, span, sizePointer, 8)) return finish(M::InvalidParameter);
        if (protectCall) {
            uint64_t previousPointer = 0;
            if (!stack(5, previousPointer) || !access(previousPointer, 4, 2)) return finish(M::AccessViolation);
            if (Overlaps(low, span, previousPointer, 4) || Overlaps(previousPointer, 4, basePointer, 8) || Overlaps(previousPointer, 4, sizePointer, 8)) return finish(M::InvalidParameter);
            uint32_t previous = 0;
            status = virtualMemory_.Protect(backend, address, bytes, static_cast<uint32_t>(Reg(vm, WHvX64RegisterR9)), previous);
            if (!status) memory.Write(previousPointer, previous); // ULONG, exactly four bytes
        } else status = virtualMemory_.Free(backend, address, bytes, static_cast<uint32_t>(Reg(vm, WHvX64RegisterR9)));
    }
    if (!status) { memory.Write(basePointer, address); memory.Write(sizePointer, bytes); }
    return finish(status);
}

void MiniNt::Dispatch(MicroVm& vm) {
    if (threadsStopped_) { vm.FailExecution(L"The guest thread process has terminated; recreate the VM"); return; }
    uint32_t ssn = (uint32_t)Reg(vm, WHvX64RegisterRax);
    if (ssn >= 0xF0000001 && ssn <= 0xF0000005) { DispatchHeap(vm, ssn); return; }
    std::string name = NameOf(ssn);
    if (DispatchVirtualMemory(vm, name) || DispatchThreads(vm, name) ||
        DispatchSynchronization(vm, name) || DispatchObjects(vm, name)) return;

    if (name == "NtWaitForAlertByThreadId") {
        // Modern thread-alert wait (SRW locks / loader lock). Single-threaded:
        // pretend we were alerted immediately so spin-then-wait loops break.
        report_.Add(ReportBuilder::Cat::Process, "NtWaitForAlertByThreadId", "", "");
        SetReg(vm, WHvX64RegisterRax, 0 /*STATUS_SUCCESS = alerted*/);
        return;
    }
    if (name == "NtAlertThreadByThreadId") {
        report_.Add(ReportBuilder::Cat::Process, "NtAlertThreadByThreadId", "", "");
        SetReg(vm, WHvX64RegisterRax, 0);
        return;
    }
    if (name == "NtQueryInformationProcess") {
        // (HANDLE, PROCESSINFOCLASS, PVOID Out, ULONG Len, PULONG RetLen[5])
        uint64_t cls    = Reg(vm, WHvX64RegisterRdx);
        uint64_t out    = Reg(vm, WHvX64RegisterR8);
        uint64_t len    = Reg(vm, WHvX64RegisterR9);
        uint64_t retLen = StackArg(vm, 5);
        uint32_t written = 0;
        uint32_t status = 0xC0000003; /*STATUS_INVALID_INFO_CLASS*/
        if (cls == 0 /*ProcessBasicInformation*/ && out && len >= 0x30) {
            uint64_t teb = GsBase(vm);
            uint64_t peb = teb ? Rd64(vm, teb + 0x60) : 0;
            Wr64(vm, out + 0x00, 0);            // ExitStatus = STILL_ACTIVE-ish
            Wr64(vm, out + 0x08, peb);          // PebBaseAddress
            Wr64(vm, out + 0x10, 1);            // AffinityMask
            Wr64(vm, out + 0x18, 8);            // BasePriority
            Wr64(vm, out + 0x20, 0x1000);       // UniqueProcessId
            Wr64(vm, out + 0x28, 0);            // InheritedFromUniqueProcessId
            written = 0x30; status = 0;
            report_.Add(ReportBuilder::Cat::Process, "NtQueryInformationProcess",
                        "ProcessBasicInformation", "PEB @ 0x" + std::to_string(peb));
        } else if (cls == 36 /*ProcessCookie*/ && out && len >= 4) {
            Wr32(vm, out, 0x1A2B3C4D);          // a stable per-process cookie
            written = 4; status = 0;
            report_.Add(ReportBuilder::Cat::Process, "NtQueryInformationProcess",
                        "ProcessCookie", "0x1A2B3C4D");
        } else {
            // Don't fake success with an unfilled buffer -- that feeds the loader
            // garbage. Report the class is unsupported so it takes the default.
            report_.Add(ReportBuilder::Cat::Process, "NtQueryInformationProcess",
                        "class " + std::to_string(cls), "(INVALID_INFO_CLASS)");
        }
        if (retLen) Wr32(vm, retLen, written);
        SetReg(vm, WHvX64RegisterRax, status);
        return;
    }
    if (name == "NtQuerySystemInformation") {
        // (SYSTEM_INFORMATION_CLASS, PVOID Out, ULONG Len, PULONG RetLen)
        uint64_t cls    = Reg(vm, WHvX64RegisterRcx);
        uint64_t out    = Reg(vm, WHvX64RegisterRdx);
        uint64_t len    = Reg(vm, WHvX64RegisterR8);
        uint64_t retLen = Reg(vm, WHvX64RegisterR9);
        uint32_t written = 0;
        uint32_t status = 0xC0000003; /*STATUS_INVALID_INFO_CLASS*/
        std::string detail = "(INVALID_INFO_CLASS)";
        // Anti-VM: a real desktop has many cores and lots of RAM. The default
        // "1 CPU, 1 GiB" is a textbook sandbox tell, so we present the persona's
        // 8 logical processors and 16 GiB (consistent with the i7-10700K spoof).
        const uint32_t kCpus = hardware_?hardware_->vcpus:8;
        const uint64_t kAffinity = (1ull << kCpus) - 1;          // 0xFF
        const uint32_t kPhysPages = hardware_?static_cast<uint32_t>(hardware_->ramBytes/4096):16u * 1024 * 1024 * 1024 / 4096;
        if(cls==76&&hardware_){
            if(!out||len<16||!vm.CanAccessMemory(out,len,3)){SetReg(vm,WHvX64RegisterRax,0xC0000005);return;}
            if(Rd32(vm,out)!=guardian::hardware::Rsmb||Rd32(vm,out+4)!=1||Rd32(vm,out+8)!=0){SetReg(vm,WHvX64RegisterRax,0xC0000002);return;}
            const auto firmware=guardian::hardware::Smbios(*hardware_);const uint32_t required=16+firmware.size;
            Wr32(vm,out+12,firmware.size);if(retLen)Wr32(vm,retLen,required);
            if(len<required){SetReg(vm,WHvX64RegisterRax,0xC0000023);return;}
            std::memcpy(static_cast<uint8_t*>(vm.RamData())+out+16,firmware.bytes,firmware.size);SetReg(vm,WHvX64RegisterRax,0);return;
        }
        if (cls == 0 /*SystemBasicInformation*/ && out && len >= 0x40) {
            for (uint64_t i = 0; i < 0x40; i += 8) Wr64(vm, out + i, 0);
            Wr32(vm, out + 0x08, 0x1000);       // PageSize
            Wr32(vm, out + 0x0C, kPhysPages);   // NumberOfPhysicalPages (16 GiB)
            Wr32(vm, out + 0x18, 0x10000);      // AllocationGranularity
            Wr64(vm, out + 0x20, 0x10000);      // MinimumUserModeAddress
            Wr64(vm, out + 0x28, 0x7FFFFFFEFFFFull); // MaximumUserModeAddress
            Wr64(vm, out + 0x30, kAffinity);    // ActiveProcessorsAffinityMask
            Wr32(vm, out + 0x38, kCpus);        // NumberOfProcessors (byte at +0x38)
            written = 0x40; status = 0;
            detail = std::to_string(kCpus) + " CPUs, 16 GiB RAM";
        } else if (cls == 1 /*SystemProcessorInformation*/ && out && len >= 0x08) {
            Wr16(vm, out + 0x00, 9);            // ProcessorArchitecture = AMD64
            Wr16(vm, out + 0x02, 0);            // Reserved
            Wr16(vm, out + 0x04, 6);            // ProcessorLevel (family 6)
            Wr16(vm, out + 0x06, (uint16_t)((165 << 8) | 5)); // Model 165, stepping 5
            written = 0x08; status = 0;
            detail = "Intel64 Family 6 Model 165";
        } else if (cls == 5 /*SystemProcessInformation*/ && out) {
            // Anti-VM: a real desktop runs dozens of processes; a sandbox runs a
            // handful, and malware also scans this list for analysis tools
            // (vmtoolsd, vboxservice, wireshark, procmon, x64dbg...). We emit a
            // believable SYSTEM_PROCESS_INFORMATION chain with NONE of those.
            static const struct { const char* name; uint32_t pid; uint32_t threads; } procs[] = {
                {"",4,1},{"System",4,180},{"Registry",72,4},{"smss.exe",340,2},
                {"csrss.exe",456,11},{"wininit.exe",532,1},{"csrss.exe",544,13},
                {"winlogon.exe",604,5},{"services.exe",660,8},{"lsass.exe",680,9},
                {"svchost.exe",772,18},{"fontdrvhost.exe",812,5},{"svchost.exe",880,12},
                {"svchost.exe",952,22},{"svchost.exe",1012,8},{"svchost.exe",1060,15},
                {"svchost.exe",1144,6},{"svchost.exe",1216,9},{"svchost.exe",1304,11},
                {"svchost.exe",1388,7},{"svchost.exe",1452,10},{"MsMpEng.exe",1580,28},
                {"dwm.exe",1024,14},{"spoolsv.exe",1736,12},{"svchost.exe",1844,5},
                {"SecurityHealthService.exe",2012,4},{"nvcontainer.exe",2140,9},
                {"explorer.exe",3260,52},{"SearchIndexer.exe",3420,16},
                {"StartMenuExperienceHost.exe",3680,9},{"RuntimeBroker.exe",3760,6},
                {"SearchApp.exe",3920,30},{"RuntimeBroker.exe",4044,8},
                {"sihost.exe",4120,7},{"taskhostw.exe",4180,9},{"ctfmon.exe",4260,4},
                {"NVIDIA Web Helper.exe",4400,11},{"audiodg.exe",4560,5},
                {"chrome.exe",5120,32},{"chrome.exe",5200,14},{"chrome.exe",5288,12},
                {"chrome.exe",5360,11},{"chrome.exe",5444,9},{"steam.exe",6020,38},
                {"Discord.exe",6210,26},{"conhost.exe",6480,3},
            };
            const int NP = (int)(sizeof(procs)/sizeof(procs[0]));
            // Compute needed size first (entry = 0x100 header + name buffer, 8-aligned).
            auto entrySize = [](const char* nm){
                uint64_t nb = (uint64_t)std::strlen(nm) * 2;
                return 0x100 + ((nb + 2 + 7) & ~7ull);
            };
            uint64_t need = 0; for (auto& p : procs) need += entrySize(p.name);
            if (len < need) { status = 0xC0000004 /*INFO_LENGTH_MISMATCH*/; written = (uint32_t)need; }
            else {
                for (uint64_t i = 0; i < need; i += 8) Wr64(vm, out + i, 0);  // zero region
                uint64_t off = 0;
                for (int i = 0; i < NP; ++i) {
                    uint64_t e = out + off;
                    uint64_t es = entrySize(procs[i].name);
                    Wr32(vm, e + 0x00, (i == NP - 1) ? 0 : (uint32_t)es);  // NextEntryOffset
                    Wr32(vm, e + 0x04, procs[i].threads);                  // NumberOfThreads
                    uint64_t nb = (uint64_t)std::strlen(procs[i].name) * 2;
                    Wr16(vm, e + 0x38, (uint16_t)nb);                      // ImageName.Length
                    Wr16(vm, e + 0x3A, (uint16_t)(nb ? nb + 2 : 0));       // MaximumLength
                    Wr64(vm, e + 0x40, nb ? (e + 0x100) : 0);              // ImageName.Buffer
                    Wr64(vm, e + 0x50, procs[i].pid);                      // UniqueProcessId
                    for (uint64_t k = 0; k < nb; k += 2)
                        Wr16(vm, e + 0x100 + k, (uint16_t)(uint8_t)procs[i].name[k/2]);
                    off += es;
                }
                written = (uint32_t)need; status = 0;
                detail = std::to_string(NP) + " processes (no analysis tools)";
            }
        }
        if (retLen) Wr32(vm, retLen, written);
        report_.Add(ReportBuilder::Cat::Process, "NtQuerySystemInformation",
                    "class " + std::to_string(cls), detail);
        SetReg(vm, WHvX64RegisterRax, status);
        return;
    }
    if (name == "NtMapViewOfSection") {
        // (HANDLE Section, HANDLE Process, PVOID* Base, ULONG_PTR ZeroBits,
        //  SIZE_T Commit[5], PLARGE_INTEGER Off[6], PSIZE_T ViewSize[7], ...)
        uint64_t sh       = Reg(vm, WHvX64RegisterRcx);
        uint64_t baseOut  = Reg(vm, WHvX64RegisterR8);
        using M = GuestVirtualMemory;
        const GuestMemory memory(vm.RamData(), vm.RamSize());
        const auto access = [&](uint64_t p, uint8_t bits) { return p && vm.CanAccessMemory(p, 8, bits) && virtualMemory_.Allows(p, 8, bits); };
        uint64_t arguments[6]{};
        const auto rsp = Reg(vm, WHvX64RegisterRsp);
        for (unsigned i = 0; i < 6; ++i) {
            const auto offset = 0x28ull + i * 8ull;
            if (rsp > UINT64_MAX - offset || !access(rsp + offset, 1) || !memory.Read(rsp + offset, arguments[i])) {
                SetReg(vm, WHvX64RegisterRax, M::AccessViolation); return;
            }
        }
        const auto viewPtr = arguments[2];
        if (Reg(vm, WHvX64RegisterRdx) != UINT64_MAX) { SetReg(vm, WHvX64RegisterRax, M::InvalidHandle); return; }
        if (!access(baseOut, 3) || !access(viewPtr, 3)) { SetReg(vm, WHvX64RegisterRax, M::AccessViolation); return; }
        if (Overlaps(baseOut, 8, viewPtr, 8)) { SetReg(vm, WHvX64RegisterRax, M::InvalidParameter); return; }
        if (heap_.Base() && heap_.Base() < virtualMemory_.End() && virtualMemory_.Base() < heap_.End()) { SetReg(vm, WHvX64RegisterRax, M::Conflict); return; }
        std::shared_ptr<SectionObject> section;
        const auto handleStatus = objects_.Reference(sh, 0, section);
        if (handleStatus) { SetReg(vm, WHvX64RegisterRax, handleStatus); return; }
        const auto offsetPointer = arguments[1];
        if (offsetPointer && !access(offsetPointer, 1)) { SetReg(vm, WHvX64RegisterRax, M::AccessViolation); return; }
        if (Reg(vm, WHvX64RegisterR9) || arguments[0] || arguments[4] || (offsetPointer && Rd64(vm, offsetPointer))) {
            SetReg(vm, WHvX64RegisterRax, M::NotSupported); return;
        }
        const auto inherit = arguments[3];
        if (inherit != 1 && inherit != 2) { SetReg(vm, WHvX64RegisterRax, M::InvalidParameter); return; }
        uint64_t size = section->size, requestedSize = 0, base = 0;
        memory.Read(viewPtr, requestedSize); memory.Read(baseOut, base);
        if (!size || (requestedSize && requestedSize > size)) { SetReg(vm, WHvX64RegisterRax, M::InvalidParameter); return; }
        if (requestedSize && !section->image) size = requestedSize;
        // Image layout still uses the previous per-image RWX policy. Section-by-
        // section PE permissions are a separate loader milestone; no false claim
        // that those permissions were implemented by this private-memory change.
        const auto protection = section->image ? 0x40u : static_cast<uint32_t>(arguments[5]);
        if (!section->image && (M::Access(protection) & ~M::Access(section->protect))) {
            SetReg(vm, WHvX64RegisterRax, 0xC000004E); return; // STATUS_SECTION_PROTECTION
        }
        if (!M::ValidProtection(protection)) { SetReg(vm, WHvX64RegisterRax, M::InvalidProtection); return; }
        const auto accessBits = M::Access(protection);
        const uint32_t required = ((accessBits & 1) ? 4u : 0u) | ((accessBits & 2) ? 2u : 0u) | ((accessBits & 4) ? 8u : 0u);
        const auto rightsStatus = objects_.Reference(sh, section->image ? 12u : required, section);
        if (rightsStatus) { SetReg(vm, WHvX64RegisterRax, rightsStatus); return; }
        GuestObjects::BasicInformation handleInfo{}; objects_.Query(sh, handleInfo);
        const auto maximumBits = M::Access(section->protect) &
            (((handleInfo.grantedAccess & 4) ? 1 : 0) | ((handleInfo.grantedAccess & 2) ? 2 : 0) | ((handleInfo.grantedAccess & 8) ? 4 : 0));
        const uint32_t maximumProtect = maximumBits == 7 ? 0x40 : maximumBits == 5 ? 0x20 : maximumBits == 3 ? 4 : maximumBits == 1 ? 2 : 1;
        VmMemoryBackend backend(vm);
        const auto status = virtualMemory_.Allocate(backend, base, size, M::Reserve | M::Commit,
            protection, 0, section->image ? M::Image : M::Mapped, maximumProtect);
        if (status) { SetReg(vm, WHvX64RegisterRax, status); return; }
        try { views_.emplace(base, section); }
        catch (const std::bad_alloc&) {
            virtualMemory_.UnmapView(backend, base); SetReg(vm, WHvX64RegisterRax, GuestObjects::NoMemory); return;
        }
        uint8_t* g = static_cast<uint8_t*>(vm.RamData());

        if (section->image && section->file) {
            // SEC_IMAGE: full PE layout (headers + sections by RVA + relocations).
            auto f = fs_.Files().find(section->file->path);
            uint64_t laid = (f != fs_.Files().end())
                          ? LayoutPeImage(g, base, vm.RamSize(), f->second) : 0;
            if (!laid) { virtualMemory_.UnmapView(backend, base); views_.erase(base); SetReg(vm, WHvX64RegisterRax, M::AccessViolation); return; }
            Wr64(vm, baseOut, base);
            if (viewPtr) Wr64(vm, viewPtr, laid);
            report_.Add(ReportBuilder::Cat::Process, "NtMapViewOfSection(SEC_IMAGE)",
                        section->file->path,
                        std::to_string(laid) + " bytes laid out @ 0x" + std::to_string(base));
            SetReg(vm, WHvX64RegisterRax, 0);
            return;
        }

        if (section->file) {                // copy the file's bytes into the view
            auto f = fs_.Files().find(section->file->path);
            if (f != fs_.Files().end()) {
                const auto count = (std::min)(size, static_cast<uint64_t>(f->second.size()));
                if (memory.Contains(base, count)) CopyMemory(g + base, f->second.data(), static_cast<size_t>(count));
            }
        }
        Wr64(vm, baseOut, base);
        if (viewPtr) Wr64(vm, viewPtr, size);
        report_.Add(ReportBuilder::Cat::Process, "NtMapViewOfSection", "",
                    std::to_string(size) + " bytes @ 0x" + std::to_string(base));
        SetReg(vm, WHvX64RegisterRax, 0);
        return;
    }
    if (name == "NtUnmapViewOfSection") {
        uint64_t viewBase = 0, viewSize = 0; uint32_t viewType = 0;
        virtualMemory_.ReservationAt(Reg(vm, WHvX64RegisterRdx), viewBase, viewSize, viewType);
        VmMemoryBackend backend(vm);
        const auto status = Reg(vm, WHvX64RegisterRcx) == UINT64_MAX
            ? virtualMemory_.UnmapView(backend, Reg(vm, WHvX64RegisterRdx)) : GuestVirtualMemory::InvalidHandle;
        if (!status) views_.erase(viewBase);
        report_.Add(ReportBuilder::Cat::Process, "NtUnmapViewOfSection", "", "NTSTATUS " + std::to_string(status));
        SetReg(vm, WHvX64RegisterRax, status);
        return;
    }
    if (name == "NtDeviceIoControlFile") {
        // (HANDLE, Event, ApcRoutine, ApcContext, PIO_STATUS_BLOCK[5],
        //  ULONG IoControlCode[6], PVOID In[7], ULONG InLen[8], PVOID Out[9], ULONG OutLen[10])
        // Anti-VM, network edition: this is the low-level path GetAdaptersInfo and
        // MAC-reading tools bottom out in. We answer the NDIS MAC OID queries with a
        // genuine Intel OUI -- never a VMware/VirtualBox/Hyper-V/QEMU OUI.
        uint64_t iosb  = StackArg(vm, 5);
        std::shared_ptr<FileObject> device;
        const auto handleStatus = objects_.Reference(Reg(vm, WHvX64RegisterRcx), 0, device);
        if (handleStatus) { SetReg(vm, WHvX64RegisterRax, handleStatus); return; }
        uint32_t ioctl = (uint32_t)StackArg(vm, 6);
        uint64_t inBuf = StackArg(vm, 7);
        uint64_t outBuf = StackArg(vm, 9);
        uint64_t outLen = StackArg(vm, 10);
        uint32_t status = 0xC0000010 /*STATUS_INVALID_DEVICE_REQUEST*/, info = 0;
        if (ioctl == 0x00170002 /*IOCTL_NDIS_QUERY_GLOBAL_STATS*/ && inBuf) {
            uint32_t oid = Rd32(vm, inBuf);
            if ((oid == 0x01010101 /*OID_802_3_PERMANENT_ADDRESS*/ ||
                 oid == 0x01010102 /*OID_802_3_CURRENT_ADDRESS*/) && outBuf && outLen >= 6) {
                // 8C:16:45 is a registered Intel OUI -> reads as a real Intel NIC.
                const uint8_t mac[6] = { 0x8C, 0x16, 0x45, 0x3A, 0x9D, 0x71 };
                uint8_t* g = static_cast<uint8_t*>(vm.RamData());
                if (outBuf + 6 <= vm.RamSize()) CopyMemory(g + outBuf, mac, 6);
                status = 0; info = 6;
                report_.Add(ReportBuilder::Cat::Process, "NtDeviceIoControlFile",
                            "OID_802_3_ADDRESS", "8C:16:45:3A:9D:71 (Intel)");
            }
        }
        if (iosb) { Wr64(vm, iosb + 0, status); Wr64(vm, iosb + 8, info); }
        if (status != 0)
            report_.Add(ReportBuilder::Cat::Process, "NtDeviceIoControlFile",
                        "ioctl 0x" + std::to_string(ioctl), "(unsupported)");
        SetReg(vm, WHvX64RegisterRax, status);
        return;
    }
    if (name == "NtRaiseException") {
        // (PEXCEPTION_RECORD[RCX], PCONTEXT, BOOLEAN FirstChance). The loader
        // raises this when bring-up fails. Capture the code+address and stop so
        // we see the real failure instead of an exception-dispatch flood.
        uint64_t rec = Reg(vm, WHvX64RegisterRcx);
        uint32_t code = 0;
        const uint8_t* g = static_cast<const uint8_t*>(vm.RamData());
        if (rec + 4 <= vm.RamSize()) std::memcpy(&code, g + rec, 4);
        uint64_t excAddr = (rec + 0x18 <= vm.RamSize()) ? Rd64(vm, rec + 0x10) : 0;
        char buf[64]; std::snprintf(buf, sizeof(buf), "code 0x%08X @ 0x%llX",
                                    code, (unsigned long long)excAddr);
        report_.Add(ReportBuilder::Cat::Other, "NtRaiseException", buf, "");
        exitCode_ = (int)code;
        SetReg(vm, WHvX64RegisterRax, 0);
        vm.RequestStop();
        return;
    }
    if (name == "NtContinue" || name == "NtContinueEx") {
        // The loader calls NtContinue(Context, ...) at the END of init to jump to
        // the image entry point. Reaching here means LdrpInitialize COMPLETED.
        uint64_t ctx = Reg(vm, WHvX64RegisterRcx);
        reachedContinue_ = true;
        continueRip_ = Rd64(vm, ctx + 0xF8);       // CONTEXT.Rip
        report_.Add(ReportBuilder::Cat::Process, "NtContinue", "",
                    "loader init complete -> entry 0x" + std::to_string(continueRip_));
        SetReg(vm, WHvX64RegisterRax, 0);
        vm.RequestStop();
        return;
    }
    if (name == "NtTerminateProcess") {            // (HANDLE, NTSTATUS ExitStatus)
        if (threading_ && Reg(vm, WHvX64RegisterRcx) != UINT64_MAX) { SetReg(vm, WHvX64RegisterRax, GuestObjects::InvalidHandle); return; }
        exitCode_ = (int)Reg(vm, WHvX64RegisterRdx);
        if (threading_) StopThreads(vm, static_cast<uint32_t>(exitCode_));
        report_.Add(ReportBuilder::Cat::Process, "NtTerminateProcess", "",
                    std::to_string(exitCode_));
        SetReg(vm, WHvX64RegisterRax, 0);
        vm.RequestStop();
        return;
    }
    // Anything else: record the REAL name so the surface to grow is visible.
    report_.Add(ReportBuilder::Cat::Other, "unimplemented",
                name.empty() ? ("SSN 0x" + std::to_string(ssn)) : name, "");
    SetReg(vm, WHvX64RegisterRax, 0xC0000002 /*STATUS_NOT_IMPLEMENTED*/);
    // Backstop against runaway. In loader mode we tolerate many unimplemented
    // calls so the whole bring-up trace is visible before stopping.
    if (++unimpl_ > (loaderMode_ ? 4000 : 64)) vm.RequestStop();
}

} // namespace microvm
} // namespace av
