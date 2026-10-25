extern "C" const volatile char GuardianWin32Capability[]="GuardianWin32/1/guest";
#ifdef GUARDIAN_CRT_NATIVE
extern "C" const volatile char GuardianCrtCapability[]="GuardianCrt/1/guest";
#endif
namespace {
storage::Volume win32Storage;uint8_t win32Disk[65536]{};bool win32Mounted=false;
bool Win32Block(void*,io::Op op,uint64_t offset,uint8_t* data) noexcept {
    if(op==io::Op::Flush)return true;if(!data||(offset&511)||offset>sizeof(win32Disk)-512)return false;
    if(op==io::Op::Read)memcpy(data,win32Disk+offset,512);else if(op==io::Op::Write)memcpy(win32Disk+offset,data,512);else return false;return true;
}
bool Win32StorageStart(RuntimeSlot& slot,threads::Scheduler& scheduler) noexcept {
    if(win32Mounted)return true;win32Storage.Configure(nullptr,Win32Block,scheduler);if(win32Storage.Mount())return false;win32Mounted=true;
    storage::Command c;c.op=storage::Op::Mkdir;memcpy(c.path,"C:\\App",7);const auto status=win32Storage.Execute(slot.pid,slot.context.processes->Get(slot.pid)->token,c);
    return !status||status==storage::Collision;
}
bool Win32Parameters(RuntimeSlot& slot,uint8_t (&peb)[4096]) noexcept {
    win32::Parameters p;const uint64_t address=slot.nativePeb+win32::ParametersOffset;memcpy(peb+0x20,&address,8);
    const auto text=[&](uint32_t offset,const char* source,uint32_t capacity,win32::String& out) noexcept {
        uint32_t i=0;for(;i<capacity-1&&source[i];++i){if(uint8_t(source[i])>126||source[i]<32)return false;peb[offset+i*2]=uint8_t(source[i]);}
        if(source[i])return false;out={uint16_t(i*2),uint16_t(capacity*2),0,slot.nativePeb+offset};return true;};
    char command[128]{};if(!Read(&slot.context,cpu::UserData+0x800,command,sizeof(command))||
        !text(0x600,command,128,p.command)||!text(0x800,slot.sources[0].name,128,p.imagePath)||
        !text(0x900,"C:\\Windows\\System32",64,p.dllPath)||!text(0xA00,"C:\\App",128,p.directory))return false;
    p.environment=slot.nativePeb+win32::EnvironmentOffset;p.output=p.error=runtime::Console;
    const char16_t environment[]=u"SystemRoot=C:\\Windows\0TEMP=C:\\App\0PATH=C:\\Windows\\System32\0USERNAME=MiniNT\0";
    memcpy(peb+win32::EnvironmentOffset,environment,sizeof(environment));
    if(const auto* parent=Find(slot.context.processes->Get(slot.pid)->parent);parent&&parent->nativeReady){
        uint64_t parameters=0;win32::Parameters previous;
        if(!Read(const_cast<Context*>(&parent->context),parent->nativePeb+0x20,&parameters,8)||!Read(const_cast<Context*>(&parent->context),parameters,&previous,sizeof(previous)))return false;
        uint16_t inherited[win32::EnvironmentCharacters]{};
        if(!parent->nativeServices->Read(previous.environment,inherited,sizeof(inherited)))return false;
        bool ended=false;for(uint32_t i=1;i<win32::EnvironmentCharacters;++i)if(!inherited[i]&&!inherited[i-1]){ended=true;break;}if(!ended)return false;
        memcpy(peb+win32::EnvironmentOffset,inherited,sizeof(inherited));
        if(previous.directory.length>254||(previous.directory.length&1)||!parent->nativeServices->Read(previous.directory.buffer,peb+0xA00,previous.directory.length))return false;
        p.directory.length=previous.directory.length;
        peb[0xA00+previous.directory.length]=peb[0xA01+previous.directory.length]=0;
    }
    memcpy(peb+win32::ParametersOffset,&p,sizeof(p));return true;
}
uint32_t Win32Dispatch(RuntimeSlot& slot,uint64_t pointer) noexcept {
    win32::Request request;if(!slot.nativeServices->Read(pointer,&request,sizeof(request))||!slot.nativeServices->Probe(pointer,sizeof(request),true))return native::AccessViolation;
    if(request.magic!=0x34424B47||request.version!=1||request.bytes!=sizeof(request))return native::Invalid;
    auto event=telemetry::Begin(telemetry::Domain::Control,telemetry::Operation::Query,slot.pid,slot.context.processes->Get(slot.pid)->token.user,"Win32 service","",request.op);
    if(!telemetry::Before(event))return telemetry::End(event,native::Denied);uint32_t result=native::NotImplemented;
    if(request.op==20){const char* found=nullptr;for(uint32_t i=0;i<pe::MaxModules;++i)if(const auto* m=slot.image.Get(i);m&&((!request.args[0]&&!i)||m->base==request.args[0])){found=slot.sources[i].name;break;}
        if(!found)result=native::InvalidHandle;else {uint32_t n=0;while(found[n])++n;
            if(request.args[2]<n+1)result=native::Small;else result=slot.nativeServices->Write(request.args[1],found,n+1)?0:native::AccessViolation;request.args[3]=n;}}
#ifdef GUARDIAN_CRT_NATIVE
    else if(request.op==21){unwind::Image image{};if(!StackImage(&slot,request.args[0],image))result=native::Invalid;else {request.args[1]=image.base;request.args[2]=image.size;request.args[3]=image.table;request.args[4]=image.count;result=0;}}
    else if(request.op==22){result=native::Invalid;for(uint32_t i=0;i<pe::MaxModules;++i)if(const auto* m=slot.image.Get(i);m&&request.args[0]>=m->base&&request.args[0]-m->base<m->size){request.args[1]=m->base;result=0;break;}}
#endif
    else result=slot.nativeServices->Win32(request.op,request.args);
    if(!slot.nativeServices->Write(pointer,&request,sizeof(request)))result=native::AccessViolation;return telemetry::End(event,result);
}
}
bool Win32StorageFinish() noexcept {if(!win32Mounted)return true;const bool ok=!win32Storage.Unmount();if(ok)win32Mounted=false;return ok;}
