// Guest-side narrowing of opaque protocol IDs. These are never host HWNDs.
// Keep retired IDs until process exit so a stale HWND cannot alias a new one.
namespace {
struct CompatWindowId {uint64_t native=0;uint32_t proxy=0;};
CompatWindowId compatWindowIds[128]{};
uint32_t CompatNarrow(uint64_t native){if(!native)return 0;
    for(const auto& entry:compatWindowIds)if(entry.native==native)return entry.proxy;
    for(uint32_t i=0;i<128;++i)if(!compatWindowIds[i].native){compatWindowIds[i]={native,0x14120000u+i+1};return compatWindowIds[i].proxy;}
    return 0;
}
bool CompatWiden(uint64_t& proxy){if(!proxy)return true;for(const auto& entry:compatWindowIds)if(entry.proxy==proxy){proxy=entry.native;return true;}return false;}
DWORD CompatUserCall(guardian::nativeuser::Call* original){
    namespace u=guardian::nativeuser;u::Call call=*original;auto& command=call.command;
    if(command.op==u::Op::Create){bool available=false;for(const auto& entry:compatWindowIds)if(!entry.native){available=true;break;}if(!available)return ERROR_NOT_ENOUGH_QUOTA;}
    if(command.op!=u::Op::Reply&&command.op!=u::Op::Result&&!CompatWiden(command.window))return ERROR_INVALID_HANDLE;
    if((command.op==u::Op::Create||command.op==u::Op::Parent)&&!CompatWiden(command.other))return ERROR_INVALID_HANDLE;
    const auto status=GuardianNativeUserCall(&call);if(status||call.reply.status){original->reply=call.reply;return status;}
    auto& reply=call.reply;
    if(command.op==u::Op::Create||command.op==u::Op::Focus||command.op==u::Op::Parent){const auto value=CompatNarrow(reply.value);if(reply.value&&!value)return ERROR_NOT_ENOUGH_QUOTA;reply.value=value;}
    reply.window.id=CompatNarrow(reply.window.id);reply.window.parent=CompatNarrow(reply.window.parent);
    auto& event=reply.event;event.window=CompatNarrow(event.window);
    if(event.message==WM_COMMAND&&event.lp)event.lp=CompatNarrow(event.lp);
    if((event.message==WM_SETFOCUS||event.message==WM_KILLFOCUS)&&event.wp)event.wp=CompatNarrow(event.wp);
    original->reply=reply;return status;
}
}
