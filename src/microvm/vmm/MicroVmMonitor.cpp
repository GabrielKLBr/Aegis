// MicroVmMonitor.cpp - implementation of the MicroVM engine.
#include "MicroVmMonitor.h"
#include "../../../shared/guardian/HardwareProjection.h"
#include "../../../shared/guardian/CpuTopology.h"

#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <filesystem>
#include "MemoryHost.h"

namespace av {
namespace microvm {

namespace {
struct LocalHandle {
    HANDLE value=nullptr;
    ~LocalHandle(){if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
struct AttributeListGuard {
    LPPROC_THREAD_ATTRIBUTE_LIST value;
    ~AttributeListGuard(){DeleteProcThreadAttributeList(value);}
};
WHV_REGISTER_VALUE RealModeSegment(UINT16 attributes) {
    WHV_REGISTER_VALUE v;
    ZeroMemory(&v, sizeof(v));
    v.Segment.Base = 0;
    v.Segment.Limit = 0xFFFF;
    v.Segment.Selector = 0;
    v.Segment.Attributes = attributes;
    return v;
}

WHV_REGISTER_VALUE FlatSegment(UINT16 selector, UINT16 attributes) {
    WHV_REGISTER_VALUE v;
    ZeroMemory(&v, sizeof(v));
    v.Segment.Base = 0;
    v.Segment.Limit = 0xFFFFF;
    v.Segment.Selector = selector;
    v.Segment.Attributes = attributes;
    return v;
}
} // namespace

MicroVm::~MicroVm() {
    if (part_) {
        for(uint32_t index=0;index<createdCount_;++index)WHvDeleteVirtualProcessor(part_,index);
        if (ram_) WHvUnmapGpaRange(part_, 0, ramBytes_);
        WHvDeletePartition(part_);
    }
    ReleaseMemory();
}

void MicroVm::ReleaseMemory() {
    if(memoryProcess_){
        if(memoryStop_)SetEvent(memoryStop_);
        if(WaitForSingleObject(memoryProcess_,5000)!=WAIT_OBJECT_0){TerminateProcess(memoryProcess_,ERROR_PROCESS_ABORTED);WaitForSingleObject(memoryProcess_,5000);}
        CloseHandle(memoryProcess_);memoryProcess_=nullptr;
    }
    if(memoryView_)UnmapViewOfFile(memoryView_);
    if(memoryMapping_)CloseHandle(memoryMapping_);
    if(memoryStop_)CloseHandle(memoryStop_);
    memoryView_=ram_=memorySource_=nullptr;memoryMapping_=memoryStop_=nullptr;
}
bool MicroVm::CreateMemory() {
    // VID associates a source process with one partition. Give each VM a
    // private memory owner, while keeping a shared local view for device I/O.
    // The helper runs no guest code and exits when its owner exits.
    if(!ramBytes_ || (ramBytes_&4095) || ramBytes_>(1ull<<30))return Fail("RAM range",E_INVALIDARG);
    SECURITY_ATTRIBUTES inherit{sizeof(inherit),nullptr,TRUE};
    const auto total=uint64_t(ramBytes_)+MemoryHostPage;
    memoryMapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,&inherit,PAGE_READWRITE,static_cast<DWORD>(total>>32),static_cast<DWORD>(total),nullptr);
    if(!memoryMapping_)return Fail("Create RAM section",HRESULT_FROM_WIN32(GetLastError()));
    memoryView_=MapViewOfFile(memoryMapping_,FILE_MAP_ALL_ACCESS,0,0,static_cast<SIZE_T>(total));
    if(!memoryView_)return Fail("Map local RAM",HRESULT_FROM_WIN32(GetLastError()));
    auto* header=static_cast<MemoryHostHeader*>(memoryView_);*header={MemoryHostMagic,ramBytes_,0};
    ram_=static_cast<uint8_t*>(memoryView_)+MemoryHostPage;
    LocalHandle ready{CreateEventW(&inherit,TRUE,FALSE,nullptr)},owner{};
    memoryStop_=CreateEventW(&inherit,TRUE,FALSE,nullptr);
    if(!ready.value || !memoryStop_ || !DuplicateHandle(GetCurrentProcess(),GetCurrentProcess(),GetCurrentProcess(),&owner.value,SYNCHRONIZE,TRUE,0))
        return Fail("RAM owner handles",HRESULT_FROM_WIN32(GetLastError()));
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
    std::vector<uint8_t> attributes(bytes);auto* list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if(!InitializeProcThreadAttributeList(list,1,0,&bytes))return Fail("RAM launch attributes",HRESULT_FROM_WIN32(GetLastError()));
    const AttributeListGuard attributesGuard{list};
    const HANDLE handles[]{memoryMapping_,ready.value,memoryStop_,owner.value};
    bool launched=false;DWORD error=ERROR_SUCCESS;PROCESS_INFORMATION process{};
    if(UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,const_cast<HANDLE*>(handles),sizeof(handles),nullptr,nullptr)){
        std::vector<wchar_t> name(32768);const auto length=GetModuleFileNameW(nullptr,name.data(),static_cast<DWORD>(name.size()));
        if(length && length<name.size()){
            const auto helper=std::filesystem::path(name.data()).parent_path()/L"guardian-whp-memory.exe";
            auto command=L"\""+helper.wstring()+L"\"";
            for(auto handle:handles)command+=L" "+std::to_wstring(reinterpret_cast<uintptr_t>(handle));
            command+=L" "+std::to_wstring(ramBytes_);
            STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=list;
            launched=CreateProcessW(helper.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&startup.StartupInfo,&process)!=0;
            if(!launched)error=GetLastError();
        }else error=ERROR_INSUFFICIENT_BUFFER;
    }else error=GetLastError();
    if(!launched)return Fail("Launch RAM owner",HRESULT_FROM_WIN32(error));
    CloseHandle(process.hThread);memoryProcess_=process.hProcess;
    const HANDLE waits[]{ready.value,memoryProcess_};const auto wait=WaitForMultipleObjects(2,waits,FALSE,5000);
    if(wait!=WAIT_OBJECT_0){DWORD code=ERROR_PROCESS_ABORTED;if(wait==WAIT_OBJECT_0+1)GetExitCodeProcess(memoryProcess_,&code);return Fail("RAM owner handshake",HRESULT_FROM_WIN32(code?code:ERROR_PROCESS_ABORTED));}
    MemoryBarrier();const auto address=header->address;
    if(!address || (address&4095) || address>UINT64_MAX-ramBytes_)return Fail("RAM owner address",E_INVALIDARG);
    memorySource_=reinterpret_cast<void*>(address);return true;
}
bool MicroVm::MapMemory(uint64_t gpa,uint64_t bytes,uint8_t access) {
    const auto hr=WHvMapGpaRange2(part_,memoryProcess_,reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(memorySource_)+gpa),gpa,bytes,static_cast<WHV_MAP_GPA_RANGE_FLAGS>(access));
    return SUCCEEDED(hr) || Fail("WHvMapGpaRange2",hr);
}

bool MicroVm::Fail(const char* step, HRESULT hr) {
    wchar_t buf[160];
    swprintf(buf, 160, L"%hs failed (hr=0x%08lX)", step, (unsigned long)hr);
    error_ = buf;
    return false;
}

bool MicroVm::SetReg(WHV_REGISTER_NAME name, uint64_t value) {
    WHV_REGISTER_VALUE v;
    ZeroMemory(&v, sizeof(v));
    v.Reg64 = value;
    HRESULT hr = WHvSetVirtualProcessorRegisters(part_, registerIndex_, &name, 1, &v);
    return SUCCEEDED(hr);
}

void MicroVm::SpoofCpuid(const std::string& vendor12, const std::string& brand48, uint32_t logicalCpus) {
    spoofCpuid_ = true;
    cpuidVendor_ = vendor12; cpuidVendor_.resize(12, '\0');
    cpuidBrand_  = brand48;  cpuidBrand_.resize(48, '\0');
    cpuidCpus_ = logicalCpus ? logicalCpus : 1;
}

bool MicroVm::HandleCpuid(const WHV_RUN_VP_EXIT_CONTEXT& exit,uint32_t index) {
    uint32_t leaf = (uint32_t)exit.CpuidAccess.Rax;
    uint32_t a = (uint32_t)exit.CpuidAccess.DefaultResultRax;
    uint32_t b = (uint32_t)exit.CpuidAccess.DefaultResultRbx;
    uint32_t c = (uint32_t)exit.CpuidAccess.DefaultResultRcx;
    uint32_t d = (uint32_t)exit.CpuidAccess.DefaultResultRdx;
    auto pack = [](const std::string& s, size_t off) {
        uint32_t v = 0;
        for (int i = 0; i < 4 && off + i < s.size(); ++i) v |= (uint32_t)(uint8_t)s[off + i] << (8 * i);
        return v;
    };
    if(!cpuIdentity_.empty()) {
        guardian::hardware::CpuIdentity(cpuIdentity_.c_str(),leaf,static_cast<uint32_t>(exit.CpuidAccess.Rcx),a,b,c,d);
    } else if (leaf == 0) {                         // vendor string in EBX:EDX:ECX
        b = pack(cpuidVendor_, 0); d = pack(cpuidVendor_, 4); c = pack(cpuidVendor_, 8);
    } else if (leaf == 1) {
        c &= ~(1u << 31);                    // clear hypervisor-present bit
        b = (b & 0x00FFFFFF) | ((cpuidCpus_ & 0xFF) << 16);  // logical CPU count
    } else if (leaf == 0x40000000) {
        a = b = c = d = 0;                   // hide the hypervisor vendor leaf
    } else if (leaf >= 0x80000002 && leaf <= 0x80000004) {
        size_t off = (size_t)(leaf - 0x80000002) * 16;       // brand string
        a = pack(cpuidBrand_, off); b = pack(cpuidBrand_, off + 4);
        c = pack(cpuidBrand_, off + 8); d = pack(cpuidBrand_, off + 12);
    }
    if(cpuCount_>1)guardian::hardware::CpuTopology(cpuCount_,index,leaf,static_cast<uint32_t>(exit.CpuidAccess.Rcx),a,b,c,d);
    const WHV_REGISTER_NAME ns[4] = { WHvX64RegisterRax, WHvX64RegisterRbx, WHvX64RegisterRcx, WHvX64RegisterRdx };
    WHV_REGISTER_VALUE vs[4]; ZeroMemory(vs, sizeof vs);
    vs[0].Reg64 = a; vs[1].Reg64 = b; vs[2].Reg64 = c; vs[3].Reg64 = d;
    return SUCCEEDED(WHvSetVirtualProcessorRegisters(part_,index,ns,4,vs));
}

bool MicroVm::Create(size_t ramBytes, uint64_t exceptionExitBitmap,uint32_t cpuCount) {
    if(part_ || ram_)return Fail("VM already initialized",E_UNEXPECTED);
    ramBytes_ = ramBytes;
    HRESULT hr = WHvCreatePartition(&part_);
    if (FAILED(hr)) return Fail("WHvCreatePartition", hr);

    if(!cpuCount||cpuCount>4)return Fail("Processor count",E_INVALIDARG);
    cpuCount_=cpuCount;
    hr = WHvSetPartitionProperty(part_, WHvPartitionPropertyCodeProcessorCount,
                                 &cpuCount, sizeof(cpuCount));
    if (FAILED(hr)) return Fail("SetProperty(ProcessorCount)", hr);

    // The ExtendedVmExits master enables must be set in one shot, before setup:
    // bit 0 (X64CpuidExit) gates CPUID intercepts, bit 2 (ExceptionExit) gates
    // the exception bitmap. Setting CpuidExitList alone does NOT trap -- the
    // master X64CpuidExit bit is what actually generates the VM-exits.
    UINT64 extExits = 0;
    if (spoofCpuid_||!cpuIdentity_.empty()) extExits |= 0x1;
    if (exceptionExitBitmap) extExits |= 0x4;  // WHV_EXTENDED_VM_EXITS.ExceptionExit
    if (extExits) {
        hr = WHvSetPartitionProperty(part_, WHvPartitionPropertyCodeExtendedVmExits,
                                     &extExits, sizeof(extExits));
        if (FAILED(hr)) return Fail("SetProperty(ExtendedVmExits)", hr);
    }

    if (spoofCpuid_||!cpuIdentity_.empty()) {
        // Trap these CPUID leaves so HandleCpuid can present a bare-metal CPU.
        UINT32 leaves[] = {0,1,4,0xB,0x1F,0x40000000,0x80000000,0x80000002,0x80000003,0x80000004,0x80000008,0x8000001D,0x8000001E};
        hr = WHvSetPartitionProperty(part_, WHvPartitionPropertyCodeCpuidExitList,
                                     leaves, sizeof(leaves));
        if (FAILED(hr)) return Fail("SetProperty(CpuidExitList)", hr);
    }

    if (exceptionExitBitmap) {
        hr = WHvSetPartitionProperty(part_, WHvPartitionPropertyCodeExceptionExitBitmap,
                                     &exceptionExitBitmap, sizeof(exceptionExitBitmap));
        if (FAILED(hr)) return Fail("SetProperty(ExceptionExitBitmap)", hr);
    }

    hr = WHvSetupPartition(part_);
    if (FAILED(hr)) return Fail("WHvSetupPartition", hr);

    if(!CreateMemory() || !MapMemory(0,ramBytes_,7))return false;
    memoryAccess_.assign(ramBytes_ / 4096, 7);

    for(uint32_t index=0;index<cpuCount_;++index){
        hr=WHvCreateVirtualProcessor(part_,index,0);
        if(FAILED(hr))return Fail("WHvCreateVirtualProcessor",hr);
        ++createdCount_;
    }
    return true;
}

bool MicroVm::LoadRealModeCode(const void* code, size_t len, uint64_t gpa) {
    if (!ram_ || gpa + len > ramBytes_) { error_ = L"code does not fit in guest RAM"; return false; }
    codeGpa_ = gpa;
    CopyMemory(static_cast<uint8_t*>(ram_) + gpa, code, len);

    const WHV_REGISTER_NAME names[] = {
        WHvX64RegisterCs, WHvX64RegisterDs, WHvX64RegisterEs,
        WHvX64RegisterSs, WHvX64RegisterRip, WHvX64RegisterRflags,
    };
    WHV_REGISTER_VALUE values[6];
    ZeroMemory(values, sizeof(values));
    values[0] = RealModeSegment(0x9B);   // CS: present, code, 16-bit
    values[1] = RealModeSegment(0x93);   // DS: present, data, rw
    values[2] = RealModeSegment(0x93);   // ES
    values[3] = RealModeSegment(0x93);   // SS
    values[4].Reg64 = codeGpa_;          // RIP
    values[5].Reg64 = 0x2;               // RFLAGS (reserved bit 1)

    HRESULT hr = WHvSetVirtualProcessorRegisters(part_, 0, names, 6, values);
    if (FAILED(hr)) return Fail("SetRegisters", hr);
    return true;
}

bool MicroVm::SetupLongMode(uint64_t entryGpa, uint64_t stackGpa, bool cpl3) {
    // Page tables live in low guest RAM; need room for them plus the stack.
    if (!ram_ || ramBytes_ < 0x80000) { error_ = L"need >= 512 KiB RAM for long mode"; return false; }
    auto* mem = static_cast<uint8_t*>(ram_);

    // Identity-map the first 1 GiB with 2 MiB pages: PML4 -> PDPT -> PD.
    // For ring-3 guests every level needs the user (U) bit so CPL3 may access it.
    const uint64_t kPml4 = 0x70000, kPdpt = 0x71000, kPd = 0x72000;
    const uint64_t u = cpl3 ? 0x4u : 0x0u;
    ZeroMemory(mem + kPml4, 0x3000);
    auto wr64 = [&](uint64_t gpa, uint64_t val) { CopyMemory(mem + gpa, &val, sizeof(val)); };
    wr64(kPml4, kPdpt | 0x3 | u);
    wr64(kPdpt, kPd | 0x3 | u);
    for (uint64_t i = 0; i < 512; ++i)
        wr64(kPd + i * 8, (i << 21) | 0x83 | u);  // 2 MiB page: present|rw|PS(|user)

    // Ring-0 (kernel) vs ring-3 (user) segment descriptors.
    const UINT16 csSel = cpl3 ? 0x2B : 0x08, dsSel = cpl3 ? 0x23 : 0x10;
    const UINT16 csAttr = cpl3 ? 0xA0FB : 0xA09B;   // +DPL3 for user code
    const UINT16 dsAttr = cpl3 ? 0xC0F3 : 0xC093;   // +DPL3 for user data

    const WHV_REGISTER_NAME names[] = {
        WHvX64RegisterCr0, WHvX64RegisterCr3, WHvX64RegisterCr4, WHvX64RegisterEfer,
        WHvX64RegisterCs,  WHvX64RegisterDs,  WHvX64RegisterEs,  WHvX64RegisterSs,
        WHvX64RegisterFs,  WHvX64RegisterGs,
        WHvX64RegisterRip, WHvX64RegisterRsp, WHvX64RegisterRflags,
    };
    WHV_REGISTER_VALUE v[13];
    ZeroMemory(v, sizeof(v));
    v[0].Reg64 = 0x80000033;            // CR0: PG|NE|ET|MP|PE
    v[1].Reg64 = kPml4;                 // CR3
    v[2].Reg64 = 0x620;                 // CR4: PAE | OSFXSR | OSXMMEXCPT (enable SSE)
    v[3].Reg64 = 0x500;                 // EFER: LMA|LME, SCE off (SYSCALL -> #UD)
    v[4]  = FlatSegment(csSel, csAttr); // CS
    v[5]  = FlatSegment(dsSel, dsAttr); // DS
    v[6]  = FlatSegment(dsSel, dsAttr); // ES
    v[7]  = FlatSegment(dsSel, dsAttr); // SS
    v[8]  = FlatSegment(dsSel, dsAttr); // FS
    v[9]  = FlatSegment(dsSel, dsAttr); // GS
    v[10].Reg64 = entryGpa;             // RIP
    v[11].Reg64 = stackGpa;             // RSP
    v[12].Reg64 = 0x2;                  // RFLAGS (reserved bit 1)

    HRESULT hr = WHvSetVirtualProcessorRegisters(part_, registerIndex_, names, 13, v);
    if (FAILED(hr)) return Fail("SetupLongMode/SetRegisters", hr);
    return true;
}

bool MicroVm::SetGsBase(uint64_t base) {
    WHV_REGISTER_NAME n = WHvX64RegisterGs;
    WHV_REGISTER_VALUE v;
    ZeroMemory(&v, sizeof(v));
    v.Segment.Base = base;
    v.Segment.Limit = 0xFFFFF;
    v.Segment.Selector = 0x23;        // user data (ring 3)
    v.Segment.Attributes = 0xC0F3;    // present, data rw, DPL3, G=1
    HRESULT hr = WHvSetVirtualProcessorRegisters(part_, 0, &n, 1, &v);
    if (FAILED(hr)) return Fail("SetGsBase", hr);
    return true;
}

bool MicroVm::MapPage2MB(uint64_t guestVa, uint64_t backingGpa) {
    if (!ram_) { error_ = L"no guest RAM"; return false; }
    auto* mem = static_cast<uint8_t*>(ram_);
    auto wr = [&](uint64_t g, uint64_t v) { CopyMemory(mem + g, &v, sizeof(v)); };
    const uint64_t kPdpt = 0x71000, kPd2 = 0x73000;   // PDPT from SetupLongMode; spare PD
    uint64_t pdpti = (guestVa >> 30) & 0x1FF;
    uint64_t pdi   = (guestVa >> 21) & 0x1FF;
    ZeroMemory(mem + kPd2, 0x1000);                    // supports one extra 1..2 GiB PD
    wr(kPdpt + pdpti * 8, kPd2 | 0x7);                 // PDPT[pdpti] -> PD2 (present|rw|user)
    wr(kPd2 + pdi * 8, (backingGpa & ~0x1FFFFFull) | 0x83 | 0x4);  // 2 MiB page present|rw|PS|user
    return true;
}

bool MicroVm::GetRegisters(const WHV_REGISTER_NAME* names, uint32_t count,
                           WHV_REGISTER_VALUE* out) {
    HRESULT hr = WHvGetVirtualProcessorRegisters(part_, registerIndex_, names, count, out);
    if (FAILED(hr)) return Fail("WHvGetVirtualProcessorRegisters", hr);
    if(queuedInterrupt_.PendingInterruption.InterruptionPending)
        for(uint32_t i=0;i<count;++i)if(names[i]==WHvRegisterPendingInterruption)out[i]=queuedInterrupt_;
    return true;
}

bool MicroVm::CanAccessMemory(uint64_t gpa, uint64_t bytes, uint8_t access) const {
    if (memoryFatal_ || !ram_ || gpa > ramBytes_ || bytes > ramBytes_ - gpa) return false;
    if (!bytes) return true;
    const auto first = static_cast<size_t>(gpa / 4096), last = static_cast<size_t>((gpa + bytes - 1) / 4096);
    if (last >= memoryAccess_.size()) return false;
    for (size_t i = first; i <= last; ++i) if ((memoryAccess_[i] & access) != access) return false;
    return true;
}

bool MicroVm::SetMemoryAccess(uint64_t gpa, uint64_t bytes, uint8_t access) {
    if (memoryFatal_ || !part_ || !ram_ || !bytes || (gpa % 4096) || (bytes % 4096) ||
        gpa > ramBytes_ || bytes > ramBytes_ - gpa || (access & ~uint8_t(7))) {
        error_ = L"Invalid guest physical permission range"; return false;
    }
    if(access){if(!MapMemory(gpa,bytes,access))return false;}
    else {const auto hr=WHvUnmapGpaRange(part_,gpa,bytes);if(FAILED(hr))return Fail("Set guest memory permissions",hr);}
    const auto first = static_cast<size_t>(gpa / 4096), end = first + static_cast<size_t>(bytes / 4096);
    for (size_t i = first; i < end; ++i) memoryAccess_[i] = access;
    return true;
}

bool MicroVm::SetRegisters(const WHV_REGISTER_NAME* names, uint32_t count,
                           const WHV_REGISTER_VALUE* in) {
    uint32_t pending=count;
    for(uint32_t i=0;i<count;++i)if(names[i]==WHvRegisterPendingInterruption){pending=i;break;}
    if(pending<count){
        // Restore interruptibility before deciding whether WHP can accept the
        // pending external interrupt. CLI/STI shadow requires a host queue.
        std::vector<WHV_REGISTER_NAME> rest;std::vector<WHV_REGISTER_VALUE> values;
        for(uint32_t i=0;i<count;++i)if(i!=pending){rest.push_back(names[i]);values.push_back(in[i]);}
        if(!rest.empty()){
            const auto hr=WHvSetVirtualProcessorRegisters(part_,registerIndex_,rest.data(),static_cast<uint32_t>(rest.size()),values.data());
            if(FAILED(hr))return Fail("Restore CPU registers",hr);
        }
        return QueueInterrupt(in[pending]);
    }
    HRESULT hr = WHvSetVirtualProcessorRegisters(part_, registerIndex_, names, count, in);
    if (FAILED(hr)) return Fail("WHvSetVirtualProcessorRegisters", hr);
    return true;
}
bool MicroVm::QueueInterrupt(const WHV_REGISTER_VALUE& value) {
    const auto name=WHvRegisterPendingInterruption;
    if(value.PendingInterruption.InterruptionPending && value.PendingInterruption.InterruptionType==WHvX64PendingInterrupt){
        const WHV_REGISTER_NAME names[]{WHvX64RegisterRflags,WHvRegisterInterruptState};WHV_REGISTER_VALUE state[2]{};
        auto hr=WHvGetVirtualProcessorRegisters(part_,registerIndex_,names,2,state);if(FAILED(hr))return Fail("IRQ interruptibility",hr);
        if(!(state[0].Reg64&0x200) || state[1].InterruptState.InterruptShadow){
            const auto notification=WHvX64RegisterDeliverabilityNotifications;WHV_REGISTER_VALUE window{};
            hr=WHvGetVirtualProcessorRegisters(part_,registerIndex_,&notification,1,&window);if(FAILED(hr))return Fail("IRQ window state",hr);
            window.DeliverabilityNotifications.InterruptNotification=1;
            hr=WHvSetVirtualProcessorRegisters(part_,registerIndex_,&notification,1,&window);if(FAILED(hr))return Fail("Queue IRQ window",hr);
            queuedInterrupt_=value;return true;
        }
    }
    const auto hr=WHvSetVirtualProcessorRegisters(part_,registerIndex_,&name,1,&value);
    if(FAILED(hr))return Fail("Set pending CPU event",hr);
    queuedInterrupt_={};return true;
}
bool MicroVm::DeliverQueuedInterrupt() {
    if(!queuedInterrupt_.PendingInterruption.InterruptionPending)return true;
    const WHV_REGISTER_NAME names[]{WHvX64RegisterRflags,WHvRegisterInterruptState};WHV_REGISTER_VALUE values[2]{};
    auto hr=WHvGetVirtualProcessorRegisters(part_,0,names,2,values);if(FAILED(hr))return Fail("Deliver IRQ state",hr);
    if(!(values[0].Reg64&0x200) || values[1].InterruptState.InterruptShadow)return true;
    const auto event=queuedInterrupt_;
    if(!QueueInterrupt(event))return false;
    const auto notification=WHvX64RegisterDeliverabilityNotifications;WHV_REGISTER_VALUE window{};
    hr=WHvGetVirtualProcessorRegisters(part_,registerIndex_,&notification,1,&window);if(FAILED(hr))return Fail("Delivered IRQ window",hr);
    window.DeliverabilityNotifications.InterruptNotification=0;
    hr=WHvSetVirtualProcessorRegisters(part_,registerIndex_,&notification,1,&window);return SUCCEEDED(hr) || Fail("Clear IRQ window",hr);
}

bool MicroVm::Run(uint64_t maxExits) {
    if (executionFatal_) return false;
    if (memoryFatal_) { if (error_.empty()) error_ = L"Guest memory mappings failed; recreate the VM"; return false; }
    error_.clear(); lastExcVector_ = 0; lastExcRip_ = 0;
    memoryAccessFault_ = false; memoryAccessGpa_ = 0; memoryAccessType_ = 0;
    paused_=false;pauseRequested_=false;
    exits_ = 0; // totalExits_ survives slices. // per-Run budget + a fresh stop flag, so callers
    stopRequested_ = false;     // can drive many independent Run() invocations.
    cancelRequested_ = false;
    for (;;) {
        if(pauseRequested_ && !cancelRequested_){paused_=true;return true;}
        if (memoryFatal_ || executionFatal_) return false;
        if (cancelRequested_) { error_ = L"Guest canceled by watchdog"; return false; }
        if (exits_ >= maxExits) { error_ = L"exit budget exceeded (possible runaway guest)"; return false; }
        if(!DeliverQueuedInterrupt())return false;

        WHV_RUN_VP_EXIT_CONTEXT exit;
        ZeroMemory(&exit, sizeof(exit));
        HRESULT hr = WHvRunVirtualProcessor(part_, 0, &exit, sizeof(exit));
        if (FAILED(hr)) return Fail("WHvRunVirtualProcessor", hr);
        ++totalExits_;

        switch (exit.ExitReason) {
            case WHvRunVpExitReasonX64IoPortAccess: {
                ++exits_;
                uint16_t port = exit.IoPortAccess.PortNumber;
                const auto& info = exit.IoPortAccess.AccessInfo;
                auto detailed = detailedIoTraps_.find(port);
                auto trap = ioTraps_.find(port);
                if (detailed != detailedIoTraps_.end()) {
                    const uint32_t mask = info.AccessSize == 1 ? 0xFFu :
                                          info.AccessSize == 2 ? 0xFFFFu : UINT32_MAX;
                    const IoAccess access{port, static_cast<uint8_t>(info.AccessSize), info.IsWrite != 0,
                        info.StringOp != 0, info.RepPrefix != 0, static_cast<uint32_t>(exit.IoPortAccess.Rax) & mask, exit.VpContext.Rip};
                    detailed->second(*this, access);
                } else if (trap != ioTraps_.end()) {
                    // Rich trap (e.g. the NT syscall doorbell): hand it the VM.
                    trap->second(*this);
                } else {
                    auto it = ioMap_.find(port);
                    if (it == ioMap_.end() && strictIo_) { error_ = L"Unregistered guest I/O port"; return false; }
                    if (exit.IoPortAccess.AccessInfo.IsWrite) {
                        uint8_t val = static_cast<uint8_t>(exit.IoPortAccess.Rax & 0xFF);
                        if (it != ioMap_.end()) it->second->OnOut(port, val);
                    } else if (it != ioMap_.end()) {
                        // Service an IN by writing AL back into the guest's RAX.
                        uint8_t in = it->second->OnIn(port);
                        uint64_t rax = (exit.IoPortAccess.Rax & ~0xFFull) | in;
                        if (!SetReg(WHvX64RegisterRax, rax))
                            return Fail("SetRax(IN)", E_FAIL);
                    }
                }
                // WHP does not auto-advance RIP on I/O; step past the instruction.
                if (!SetReg(WHvX64RegisterRip,
                            exit.VpContext.Rip + exit.VpContext.InstructionLength))
                    return Fail("AdvanceRip", E_FAIL);
                if (memoryFatal_ || executionFatal_) return false;
                if (stopRequested_) return true;
                break;
            }
            case WHvRunVpExitReasonException: {
                const auto& ex = exit.VpException;
                bool isSyscall = ex.ExceptionType == 6 /* #UD */ &&
                                 ex.InstructionByteCount >= 2 &&
                                 ex.InstructionBytes[0] == 0x0F &&
                                 ex.InstructionBytes[1] == 0x05;
                if (isSyscall && onSyscall_) {
                    ++exits_;
                    syscallRedirected_ = false;
                    onSyscall_(*this);
                    if (memoryFatal_ || executionFatal_) return false;
                    if (stopRequested_) return true;     // e.g. ExitProcess
                    if (syscallRedirected_) break;
                    // Step over the 2-byte SYSCALL and resume in the guest.
                    if (!SetReg(WHvX64RegisterRip, exit.VpContext.Rip + 2))
                        return Fail("AdvanceRip(syscall)", E_FAIL);
                    break;
                }
                // Any other intercepted exception (e.g. #GP from a privileged
                // instruction) is recorded and stops the run -- the caller can
                // report it as a blocked/evasive action.
                lastExcVector_ = ex.ExceptionType;
                lastExcRip_    = exit.VpContext.Rip;
                wchar_t buf[128];
                swprintf(buf, 128, L"guest exception vector %u at rip=0x%llX",
                         (unsigned)ex.ExceptionType, (unsigned long long)exit.VpContext.Rip);
                error_ = buf;
                return false;
            }
            case WHvRunVpExitReasonX64Cpuid: {
                ++exits_;
                if(!HandleCpuid(exit))return Fail("CPUID registers",E_FAIL);
                if (!SetReg(WHvX64RegisterRip, exit.VpContext.Rip + exit.VpContext.InstructionLength))
                    return Fail("AdvanceRip(cpuid)", E_FAIL);
                break;
            }
            case WHvRunVpExitReasonX64Halt:
                return true;  // guest finished cleanly
            case WHvRunVpExitReasonMemoryAccess: {
                memoryAccessFault_ = true;
                memoryAccessGpa_ = exit.MemoryAccess.Gpa;
                memoryAccessType_ = static_cast<uint8_t>(exit.MemoryAccess.AccessInfo.AccessType);
                lastExcRip_ = exit.VpContext.Rip;
                wchar_t message[160];
                swprintf(message, 160, L"guest memory access %u denied at GPA 0x%llX, RIP 0x%llX",
                    static_cast<unsigned>(memoryAccessType_), static_cast<unsigned long long>(memoryAccessGpa_),
                    static_cast<unsigned long long>(lastExcRip_));
                error_ = message; return false;
            }
            case WHvRunVpExitReasonCanceled: {
                if(pauseRequested_ && !cancelRequested_){paused_=true;return true;}
                if (!cancelRequested_ && serviceWake_) {
                    serviceWake_(*this);
                    if (memoryFatal_ || executionFatal_) return false;
                    if (stopRequested_) return true;
                    break;
                }
                // Watchdog fired: the guest was spinning with no VM-exits. Report
                // the RIP it was stuck at so the spin site can be identified.
                lastExcVector_ = 0xFF;  // sentinel: "canceled", not a real vector
                lastExcRip_    = exit.VpContext.Rip;
                wchar_t buf[128];
                swprintf(buf, 128, L"canceled by watchdog (no-exit spin) at rip=0x%llX",
                         (unsigned long long)exit.VpContext.Rip);
                error_ = buf;
                return false;
            }
            case WHvRunVpExitReasonX64InterruptWindow: {
                if(queuedInterrupt_.PendingInterruption.InterruptionPending){if(!DeliverQueuedInterrupt())return false;break;}
                if (serviceWake_) {
                    serviceWake_(*this);
                    if (memoryFatal_ || executionFatal_) return false;
                    break;
                }
                error_ = L"Unrequested interrupt window"; return false;
            }
            default: {
                lastExcRip_ = exit.VpContext.Rip;
                wchar_t buf[128];
                swprintf(buf, 128, L"unexpected VM-exit 0x%08lX at rip=0x%llX",
                         (unsigned long)exit.ExitReason,
                         (unsigned long long)exit.VpContext.Rip);
                error_ = buf;
                return false;
            }
        }
    }
}

bool MicroVm::Cancel() {
    cancelRequested_ = true;
    if (!part_) return false;
    return SUCCEEDED(WHvCancelRunVirtualProcessor(part_, 0, 0));
}

bool MicroVm::WakeForService() {
    return part_ && serviceWake_ && SUCCEEDED(WHvCancelRunVirtualProcessor(part_, 0, 0));
}

const std::vector<WHV_REGISTER_NAME>& MicroVm::ThreadRegisters() {
    static const std::vector<WHV_REGISTER_NAME> names{
        WHvX64RegisterRax, WHvX64RegisterRcx, WHvX64RegisterRdx, WHvX64RegisterRbx,
        WHvX64RegisterRsp, WHvX64RegisterRbp, WHvX64RegisterRsi, WHvX64RegisterRdi,
        WHvX64RegisterR8, WHvX64RegisterR9, WHvX64RegisterR10, WHvX64RegisterR11,
        WHvX64RegisterR12, WHvX64RegisterR13, WHvX64RegisterR14, WHvX64RegisterR15,
        WHvX64RegisterRip, WHvX64RegisterRflags,
        WHvX64RegisterCs, WHvX64RegisterDs, WHvX64RegisterEs, WHvX64RegisterSs, WHvX64RegisterFs, WHvX64RegisterGs,
        WHvX64RegisterCr0, WHvX64RegisterCr2, WHvX64RegisterCr3, WHvX64RegisterCr4, WHvX64RegisterCr8, WHvX64RegisterEfer,
        WHvX64RegisterDr0, WHvX64RegisterDr1, WHvX64RegisterDr2, WHvX64RegisterDr3, WHvX64RegisterDr6, WHvX64RegisterDr7,
        WHvX64RegisterXmm0, WHvX64RegisterXmm1, WHvX64RegisterXmm2, WHvX64RegisterXmm3,
        WHvX64RegisterXmm4, WHvX64RegisterXmm5, WHvX64RegisterXmm6, WHvX64RegisterXmm7,
        WHvX64RegisterXmm8, WHvX64RegisterXmm9, WHvX64RegisterXmm10, WHvX64RegisterXmm11,
        WHvX64RegisterXmm12, WHvX64RegisterXmm13, WHvX64RegisterXmm14, WHvX64RegisterXmm15,
        WHvX64RegisterFpMmx0, WHvX64RegisterFpMmx1, WHvX64RegisterFpMmx2, WHvX64RegisterFpMmx3,
        WHvX64RegisterFpMmx4, WHvX64RegisterFpMmx5, WHvX64RegisterFpMmx6, WHvX64RegisterFpMmx7,
        WHvX64RegisterFpControlStatus, WHvX64RegisterXmmControlStatus};
    return names;
}
bool MicroVm::CaptureThread(ThreadContext& context) {
    const auto& names = ThreadRegisters();
    try { context.registers.resize(names.size()); }
    catch (const std::bad_alloc&) { FailExecution(L"No memory for thread CPU context"); return false; }
    if (!GetRegisters(names.data(), static_cast<uint32_t>(names.size()), context.registers.data())) {
        executionFatal_ = true; return false;
    }
    return true;
}
bool MicroVm::RestoreThread(const ThreadContext& context) {
    const auto& names = ThreadRegisters();
    if (context.registers.size() != names.size()) { FailExecution(L"Invalid thread CPU context"); return false; }
    if (!SetRegisters(names.data(), static_cast<uint32_t>(names.size()), context.registers.data())) {
        executionFatal_ = true; return false;
    }
    RedirectSyscall(); return true;
}

} // namespace microvm
} // namespace av

namespace av::microvm {
namespace {
const std::vector<WHV_REGISTER_NAME>& SnapshotNames(){
    static const auto names=[]{auto out=MicroVm::ThreadRegisters();
        const WHV_REGISTER_NAME extra[]={WHvX64RegisterLdtr,WHvX64RegisterTr,WHvX64RegisterIdtr,WHvX64RegisterGdtr,
            WHvX64RegisterXCr0,WHvX64RegisterTsc,WHvX64RegisterTscAux,WHvX64RegisterKernelGsBase,WHvX64RegisterApicBase,
            WHvX64RegisterPat,WHvX64RegisterSysenterCs,WHvX64RegisterSysenterEip,WHvX64RegisterSysenterEsp,
            WHvX64RegisterStar,WHvX64RegisterLstar,WHvX64RegisterCstar,WHvX64RegisterSfmask,
            WHvRegisterPendingInterruption,WHvRegisterInterruptState,WHvRegisterPendingEvent,
            WHvRegisterDeliverabilityNotifications,WHvRegisterInternalActivityState};
        out.insert(out.end(),std::begin(extra),std::end(extra));return out;}();return names;
}
uint64_t HostCpuIdentity(){int a[4]{},b[4]{};__cpuid(a,0);__cpuid(b,1);b[1]=0;state::Writer w;w.Raw(a,sizeof(a));w.Raw(b,sizeof(b));return state::Hash(w.data);}
}
bool MicroVm::RequestPause(){pauseRequested_=true;return part_ && SUCCEEDED(WHvCancelRunVirtualProcessor(part_,0,0));}
bool MicroVm::Capture(state::MachineState& out){
    if(memoryFatal_ || executionFatal_ || !ram_)return false;
    const auto& names=SnapshotNames();std::vector<WHV_REGISTER_VALUE> values(names.size());
    if(!GetRegisters(names.data(),static_cast<uint32_t>(names.size()),values.data()))return false;
    std::vector<uint8_t> xsave(65536);UINT32 written=0;
    const auto hr=WHvGetVirtualProcessorXsaveState(part_,registerIndex_,xsave.data(),static_cast<UINT32>(xsave.size()),&written);
    if(FAILED(hr) || !written || written>xsave.size())return Fail("CaptureXsave",hr);xsave.resize(written);
    state::Writer w;w.Pod(HostCpuIdentity());w.Pod(uint32_t(names.size()));w.Raw(values.data(),values.size()*sizeof(values[0]));w.Bytes(xsave);
    if(!cpuIdentity_.empty()){w.Pod(uint32_t(0x31444943));w.Text(cpuIdentity_);}
    state::MachineState saved;saved.backend=2;saved.exits=totalExits_;saved.access=memoryAccess_;
    const auto* ram=static_cast<const uint8_t*>(ram_);saved.ram.assign(ram,ram+ramBytes_);saved.processor=std::move(w.data);out=std::move(saved);return true;
}
bool MicroVm::Restore(const state::MachineState& saved){
    saved.Validate();state::Require(saved.backend==2 && saved.ram.size()==ramBytes_,"Snapshot backend/RAM mismatch");
    state::Reader r(saved.processor);state::Require(r.Pod<uint64_t>()==HostCpuIdentity(),"Saved CPU host mismatch");
    const auto& names=SnapshotNames();state::Require(r.Pod<uint32_t>()==names.size(),"Saved register inventory mismatch");
    std::vector<WHV_REGISTER_VALUE> values(names.size());r.Raw(values.data(),values.size()*sizeof(values[0]));auto xsave=r.Bytes(65536);
    if(!cpuIdentity_.empty())state::Require(r.Pod<uint32_t>()==0x31444943&&r.Text(48)==cpuIdentity_,"Saved CPU identity mismatch");
    r.End();
    std::vector<WHV_REGISTER_NAME> writable;std::vector<WHV_REGISTER_VALUE> registers;
    for(size_t i=0;i<names.size();++i){
        // Hyper-V exposes this summary as read-only. Our checkpoints stop on
        // I/O boundaries with a runnable CPU; suspended startup/halt states
        // require a separate emulation contract, never silently discard them.
        if(names[i]==WHvRegisterInternalActivityState){state::Require(values[i].InternalActivity.AsUINT64==0,"Snapshot CPU activity cannot be restored");continue;}
        writable.push_back(names[i]);registers.push_back(values[i]);
    }
    for(size_t at=0;at<saved.access.size();){const auto first=at;const auto a=saved.access[at];while(at<saved.access.size() && saved.access[at]==a)++at;
        if(!SetMemoryAccess(first*4096,(at-first)*4096,a))return false;}
    std::memcpy(ram_,saved.ram.data(),ramBytes_);
    if(!SetRegisters(writable.data(),static_cast<uint32_t>(writable.size()),registers.data()))return false;
    const auto hr=WHvSetVirtualProcessorXsaveState(part_,registerIndex_,xsave.data(),static_cast<UINT32>(xsave.size()));if(FAILED(hr))return Fail("RestoreXsave",hr);
    stopRequested_=false;cancelRequested_=false;pauseRequested_=false;paused_=true;totalExits_=saved.exits;return true;
}
}
