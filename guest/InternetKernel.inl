extern "C" const volatile char GuardianInternetCapability[]="GuardianInternet/1/guest";
namespace {
uint64_t internetSequence=0;
uint32_t InternetExchange(uint32_t pid,guardian::internet::Request& request) noexcept {
    using namespace guardian;request.magic=internet::Magic;request.version=1;request.bytes=sizeof(request);request.sequence=++internetSequence;request.pid=pid;request.status=0;
    auto* packet=reinterpret_cast<volatile uint8_t*>(internet::PacketGpa);
    for(uint32_t i=0;i<sizeof(request);++i)packet[i]=reinterpret_cast<const uint8_t*>(&request)[i];
    _ReadWriteBarrier();__outbyte(internet::Port,1);_ReadWriteBarrier();
    for(uint32_t i=0;i<sizeof(request);++i)reinterpret_cast<uint8_t*>(&request)[i]=packet[i];
    return request.status;
}
void InternetExit(uint32_t pid) noexcept {guardian::internet::Request r;r.op=guardian::internet::Op::Exit;InternetExchange(pid,r);}
uint32_t InternetDispatch(RuntimeSlot& slot,uint32_t tid,uint64_t address) noexcept {
    using namespace guardian;internet::Request r;
    if(!slot.nativeServices->Read(address,&r,sizeof(r))||!slot.nativeServices->Probe(address,sizeof(r),true))return 10014;
    if(r.magic!=internet::Magic||r.version!=1||r.bytes!=sizeof(r)||r.length>internet::Payload||r.flags||uint32_t(r.op)<1||uint32_t(r.op)>8)return 10022;
    auto operation=r.op==internet::Op::Send?telemetry::Operation::Send:r.op==internet::Op::Receive?telemetry::Operation::Receive:telemetry::Operation::Query;
    char name[64]{};for(uint32_t i=0;i<63&&r.name[i];++i){if(r.name[i]<32||r.name[i]>126)return 10022;name[i]=r.name[i];}
    auto event=telemetry::Begin(telemetry::Domain::Network,operation,slot.pid,slot.context.processes->Get(slot.pid)->token.user,name,"",r.id);event.tid=tid;
    if(!telemetry::Before(event))return telemetry::End(event,10013);
    const auto status=InternetExchange(slot.pid,r);
    if(!slot.nativeServices->Write(address,&r,sizeof(r)))return telemetry::End(event,10014);
    return telemetry::End(event,status);
}
}
