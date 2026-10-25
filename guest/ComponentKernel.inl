extern "C" const volatile char GuardianComponentCapability[]="GuardianComponent/1/guest";
namespace {
uint32_t ComponentDispatch(RuntimeSlot& slot,uint32_t tid,uint64_t address) noexcept {
    component::Request request;
    if(!slot.nativeServices->Read(address,&request,sizeof(request))||!slot.nativeServices->Probe(address,sizeof(request),true))return 998;
    componentCore.Initialize(*slot.context.processes);
    auto event=telemetry::Begin(telemetry::Domain::Control,telemetry::Operation::Query,slot.pid,slot.context.processes->Get(slot.pid)->token.user,"Component IPC","",uint32_t(request.op));
    if(!telemetry::Before(event))return telemetry::End(event,5);
    auto status=componentCore.Execute(slot.pid,tid,request);
    if(!slot.nativeServices->Write(address,&request,sizeof(request)))status=998;
    return telemetry::End(event,status);
}
}
