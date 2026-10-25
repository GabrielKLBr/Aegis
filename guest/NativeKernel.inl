// Included by PeKernel.cpp, after the loader/runtime definitions.
extern "C" const volatile char GuardianNativeNtCapability[]="GuardianNativeNt/1/guest";
namespace {
bool NativeCode(const RuntimeSlot& slot,const char* name,uint64_t ip) noexcept {
    const auto* module=slot.image.Get(slot.catalog.Find(name));return module&&ip>=module->base&&ip-module->base<module->size&&slot.image.Executable(ip);
}
}
bool PeNativePrepare(uint32_t pid,guardian::threads::Scheduler& scheduler) noexcept {
    using namespace guardian;auto* slot=Find(pid);if(!slot)return false;auto& c=slot->context;
    uint32_t size=0;const auto* bytes=slot->catalog.File("c:\\windows\\system32\\guardian-nt-syscalls.bin",size);
    if(!bytes||size!=sizeof(native::SyscallMap))return false;memcpy(&slot->nativeMap,bytes,size);
    const auto& binding=*reinterpret_cast<const nt::Info*>(nt::InfoGpa);
    if(!native::ValidMap(slot->nativeMap,binding.profile))return false;
    const auto* dll=slot->catalog.File("c:\\windows\\system32\\ntdll.dll",size);
    if(!dll||pevolume::Hash(dll,size)!=slot->nativeMap.dllHash)return false;
    if(c.processes->Reserve(pid,slot->nativePeb,4096,memory::ReadWrite)||c.processes->Commit(pid,slot->nativePeb,4096,memory::ReadWrite))return false;
    uint8_t peb[4096]{},shared[4096]{};const auto target=nt::Find(binding.profile);
    const uint32_t platform=2,product=1;const uint16_t build=uint16_t(target.build);const uint64_t processors=1;
    memcpy(peb+0x10,&slot->image.Get(0)->base,8);memcpy(peb+0x118,&target.major,4);memcpy(peb+0x11C,&target.minor,4);
    memcpy(peb+0x120,&build,2);memcpy(peb+0x124,&platform,4);memcpy(peb+0xB8,&processors,8);
    if(target.id==2){const uint16_t servicePack=0x100;memcpy(peb+0x122,&servicePack,2);}
    memcpy(shared+0x260,&target.build,4);memcpy(shared+0x264,&product,4);shared[0x268]=1;
    memcpy(shared+0x26C,&target.major,4);memcpy(shared+0x270,&target.minor,4);
#ifdef GUARDIAN_WIN32_NATIVE
    if(!Win32Parameters(*slot,peb)||!Win32StorageStart(*slot,scheduler))return false;
#endif
    if(!Write(&c,slot->nativePeb,peb,sizeof(peb))||!Write(&c,slot->initialTeb+0x60,&slot->nativePeb,8)||!c.memory->NativeSharedData(c.processes->Get(pid)->space,shared))return false;
    native::Bridge bridge;bridge.context=slot;
    bridge.executable=[](void* raw,uint64_t pc) noexcept {return static_cast<RuntimeSlot*>(raw)->image.Executable(pc);};
#ifdef GUARDIAN_DESKTOP_NATIVE
    bridge.threadContext=[](void* raw,uint32_t thread,exceptions::Context& context,bool write) noexcept {return ProcessDesktopContext(static_cast<RuntimeSlot*>(raw)->pid,thread,context,write);};
    bridge.catalog=[](void* raw,const char* path,uint64_t offset,void* out,uint32_t count,uint32_t& read,uint32_t& total) noexcept {uint32_t size=0;const auto* file=static_cast<RuntimeSlot*>(raw)->catalog.File(path,size);read=0;total=size;if(!file||offset>size)return false;read=count<size-offset?count:uint32_t(size-offset);if(read)memcpy(out,file+offset,read);return true;};
#endif
    bridge.thread=[](void* raw,uint64_t entry,uint64_t argument,bool suspended,uint32_t& tid,uint64_t& handle) noexcept {return ProcessCreatePeThread(static_cast<RuntimeSlot*>(raw)->pid,entry,argument,suspended,tid,handle);};
    bridge.exit=[](void* raw,uint32_t code,bool process) noexcept {auto& s=*static_cast<RuntimeSlot*>(raw);return ProcessNativeTerminate(s.pid,s.nativeTid,code,process);};
#ifdef GUARDIAN_SECURITY_NATIVE
    bridge.extraClose=[](void* raw,uint64_t handle) noexcept {const auto st=securityCore.Close(static_cast<RuntimeSlot*>(raw)->pid,handle,2);return st?native::InvalidHandle:0;};
#endif
#ifdef GUARDIAN_WIN32_NATIVE
    slot->nativeServices=&slot->nativeOwned;
    slot->nativeReady=slot->nativeServices->Initialize(*c.memory,*c.processes,scheduler,pid,target,slot->nativePeb,bridge,&win32Storage);
#else
    slot->nativeServices=&nativeServicesInstance;
    slot->nativeReady=slot->nativeServices->Initialize(*c.memory,*c.processes,scheduler,pid,target,slot->nativePeb,bridge);
#endif
    PeNativeTick(0);return slot->nativeReady;
}
bool PeNativeLegacy(uint32_t pid,uint64_t ip) noexcept {using namespace guardian;const auto* slot=Find(pid);return slot&&
    ((ip>=cpu::UserCode&&ip<cpu::UserCode+4096)||NativeCode(*slot,"guardian-runtime-api.dll",ip)||NativeCode(*slot,"user32.dll",ip));}
void PeNativeTick(uint64_t now) noexcept {
    using namespace guardian;const auto* boot=reinterpret_cast<const abi::BootInfo*>(0x100000);time::Sample sample{};
    if(!time::ReadPublished(*reinterpret_cast<const volatile cpu::Clock*>(cpu::ClockGpa),boot->bootId,sample))return;
    for(auto& slot:runtimes)if(slot.pid&&slot.nativeReady){const auto* p=slot.context.processes->Get(slot.pid);if(!p||!p->space.nativePage)continue;
        auto* data=static_cast<uint8_t*>(slot.context.memory->physical.At(p->space.nativePage));
        const auto set=[&](uint32_t offset,uint64_t value) noexcept {auto* fields=reinterpret_cast<volatile uint32_t*>(data+offset);fields[1]=uint32_t(value>>32);fields[0]=uint32_t(value);fields[2]=uint32_t(value>>32);};
        set(0x14,sample.utc100ns);set(8,now/100);set(0x320,now/1000000);const uint32_t multiplier=1u<<24;memcpy(data+4,&multiplier,4);
    }
}
bool PeNativeDispatch(uint32_t pid,uint32_t tid,uint64_t now,const TrapFrame& f,uint64_t teb,uint32_t& status,bool inApc) noexcept {
    using namespace guardian;auto* slot=Find(pid);if(!slot||!slot->nativeReady)return false;
    // Guardian's existing protocol remains available only from its bootstrap
    // and runtime modules. Other user code uses the native service domain.
    if(PeNativeLegacy(pid,f.rip))return false;
#ifdef GUARDIAN_WIN32_NATIVE
#ifdef GUARDIAN_DESKTOP_NATIVE
    if(f.rax==0xA7&&NativeCode(*slot,"win32u.dll",f.rip)){status=DesktopDispatch(*slot,tid,now,f.rdx);return true;}
#endif
#ifdef GUARDIAN_SECURITY_NATIVE
    if(f.rax==0xA6&&NativeCode(*slot,"advapi32.dll",f.rip)){
        security::Request request;status=87;
        if(!slot->nativeServices->Read(f.rdx,&request,sizeof(request))||!slot->nativeServices->Probe(f.rdx,sizeof(request),true)){status=998;return true;}
        auto event=telemetry::Begin(telemetry::Domain::Control,telemetry::Operation::Query,pid,slot->context.processes->Get(pid)->token.user,"Security service","",uint32_t(request.op));
        if(!telemetry::Before(event)){status=5;return true;}status=securityCore.Execute(pid,tid,request);
        if(!slot->nativeServices->Write(f.rdx,&request,sizeof(request)))status=998;telemetry::End(event,status);return true;
    }
#endif
    if(f.rax==0xA4&&NativeCode(*slot,"kernelbase.dll",f.rip)){slot->nativeTid=tid;status=Win32Dispatch(*slot,f.rdx);return true;}
#ifdef GUARDIAN_CRT_NATIVE
    if(f.rax==0xA5&&NativeCode(*slot,"ntdll.dll",f.rip)){win32::Request r;if(!slot->nativeServices->Read(f.rdx,&r,sizeof(r))||(r.op!=21&&r.op!=22)){status=native::Invalid;return true;}status=Win32Dispatch(*slot,f.rdx);return true;}
#endif
#endif
    native::Packet packet;slot->nativeTid=tid;
    if(f.rax==0xA2&&NativeCode(*slot,"ntdll.dll",f.rip)&&!slot->nativeMap.original){
        if(!slot->nativeServices->Read(f.rdx,&packet,sizeof(packet))||packet.version!=1||packet.bytes!=sizeof(packet)||packet.reserved){status=native::AccessViolation;RuntimeRejected(pid,0xA2,status);return true;}
    }else {
        packet.op=native::Op::Invalid;for(uint32_t i=0;i<slot->nativeMap.count;++i)if(slot->nativeMap.entries[i].number==f.rax)packet.op=slot->nativeMap.entries[i].operation;
        packet.args[0]=f.r10;packet.args[1]=f.rdx;packet.args[2]=f.r8;packet.args[3]=f.r9;
        // Only read the stack arguments a recognized signature actually needs.
        uint32_t count=0;switch(packet.op){case native::Op::Allocate:case native::Op::QueryMemory:case native::Op::SetValue:case native::Op::QueryValue:case native::Op::OpenFile:count=6;break;
            case native::Op::CreateSection:case native::Op::Duplicate:case native::Op::SetTimer:case native::Op::CreateKey:count=7;break;
            case native::Op::MapSection:count=10;break;case native::Op::CreateFile:case native::Op::CreateThread:count=11;break;
            case native::Op::ReadFile:case native::Op::WriteFile:count=9;break;
            case native::Op::Protect:case native::Op::QueryObject:case native::Op::QueryProcess:case native::Op::QueryThread:case native::Op::CreateEvent:case native::Op::CreateSemaphore:case native::Op::WaitMultiple:case native::Op::QueueApc:case native::Op::QueryFile:count=5;break;default:break;}
        if(count>4&&!slot->nativeServices->Read(f.rsp+40,packet.args+4,(count-4)*8)){status=native::AccessViolation;RuntimeRejected(pid,f.rax,status);return true;}
        uint32_t narrow=0,boolean=0;
        switch(packet.op){
        case native::Op::Allocate:narrow=(1u<<4)|(1u<<5);break;case native::Op::Free:narrow=1u<<3;break;case native::Op::Protect:narrow=1u<<3;break;case native::Op::QueryMemory:narrow=1u<<2;break;
        case native::Op::CreateSection:narrow=(1u<<1)|(1u<<4)|(1u<<5);break;case native::Op::MapSection:narrow=(1u<<7)|(1u<<8)|(1u<<9);break;
        case native::Op::Duplicate:narrow=(1u<<4)|(1u<<5)|(1u<<6);break;
        case native::Op::QueryObject:case native::Op::QueryProcess:case native::Op::QueryThread:narrow=(1u<<1)|(1u<<3);break;
        case native::Op::QuerySystem:narrow=(1u<<0)|(1u<<2);break;
        case native::Op::CreateEvent:narrow=(1u<<1)|(1u<<3);boolean=1u<<4;break;
        case native::Op::CreateMutant:narrow=1u<<1;boolean=1u<<3;break;
        case native::Op::CreateSemaphore:narrow=(1u<<1)|(1u<<3)|(1u<<4);break;case native::Op::ReleaseSemaphore:narrow=1u<<1;break;
        case native::Op::CreateTimer:narrow=(1u<<1)|(1u<<3);break;case native::Op::SetTimer:boolean=1u<<4;narrow=1u<<5;break;
        case native::Op::WaitSingle:boolean=1u<<1;break;case native::Op::WaitMultiple:narrow=(1u<<0)|(1u<<2);boolean=1u<<3;break;case native::Op::Delay:boolean=1u<<0;break;
        case native::Op::OpenProcess:case native::Op::OpenThread:narrow=1u<<1;break;case native::Op::CreateThread:narrow=(1u<<1)|(1u<<6);break;
        case native::Op::CreateFile:narrow=(1u<<1)|(1u<<5)|(1u<<6)|(1u<<7)|(1u<<8)|(1u<<10);break;case native::Op::OpenFile:narrow=(1u<<1)|(1u<<4)|(1u<<5);break;
        case native::Op::ReadFile:case native::Op::WriteFile:narrow=1u<<6;break;case native::Op::QueryFile:narrow=(1u<<3)|(1u<<4);break;
        case native::Op::CreateKey:narrow=(1u<<1)|(1u<<3)|(1u<<5);break;case native::Op::OpenKey:narrow=1u<<1;break;case native::Op::SetValue:narrow=(1u<<2)|(1u<<3)|(1u<<5);break;
        case native::Op::QueryValue:narrow=(1u<<2)|(1u<<4);break;default:break;}
        for(uint32_t i=0;i<11;++i){if(narrow&(1u<<i))packet.args[i]=uint32_t(packet.args[i]);if(boolean&(1u<<i))packet.args[i]=uint8_t(packet.args[i]);}
    }
    const auto* boot=reinterpret_cast<const abi::BootInfo*>(0x100000);time::Sample clock{};
    if(!time::ReadPublished(*reinterpret_cast<const volatile cpu::Clock*>(cpu::ClockGpa),boot->bootId,clock)){status=native::Invalid;return true;}
    status=slot->nativeServices->Execute(packet.op,packet.args,tid,teb,now,clock.utc100ns,inApc);if(packet.op==native::Op::Invalid)RuntimeRejected(pid,f.rax,status);return true;
}
