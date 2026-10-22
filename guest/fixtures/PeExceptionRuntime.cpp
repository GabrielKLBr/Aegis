#define GUARDIAN_RUNTIME_BUILD
#include "PeRuntimeApi.h"
#include "guardian/Exception.h"
#include <intrin.h>
extern "C" __declspec(noreturn) void GuardianExceptionReply(uint64_t,uint32_t,void*);
extern "C" __declspec(noreturn) void GuardianUnwindCall(uint64_t,uint64_t,uint64_t);
extern "C" void GuardianRaiseCall(const guardian::exceptions::Record*);
using namespace guardian::exceptions;
extern "C" __declspec(noreturn) void GuardianExceptionDispatch(Packet* packet){
    using Handler=uint32_t (*)(Record*,uint64_t,Context*,Dispatcher*);
    const auto handler=reinterpret_cast<Handler>(packet->routine);
    const auto disposition=handler(&packet->record,packet->dispatcher.establisher,&packet->context,&packet->dispatcher);
    GuardianExceptionReply(packet->token,disposition,packet);
}
namespace guardian::win {
extern "C" __declspec(noreturn) void RtlUnwindEx(void* frame,void* ip,Record*,void* value,Context*,void*){
    GuardianUnwindCall(reinterpret_cast<uint64_t>(frame),reinterpret_cast<uint64_t>(ip),reinterpret_cast<uint64_t>(value));
}
extern "C" void RaiseException(Dword code,Dword flags,Dword count,const uint64_t* information){
    if(!code || (flags&~1u) || count>15 || (count && !information))__ud2();
    Record record{};record.code=code;record.flags=flags;record.count=count;
    for(uint32_t i=0;i<count;++i)record.information[i]=information[i];GuardianRaiseCall(&record);
}
}
// Standard compiler-emitted C scope table. Filters/finally routines always
// execute in CPL3; faults in this routine follow the ordinary failure path.
extern "C" uint32_t __C_specific_handler(Record* record,uint64_t frame,Context* context,Dispatcher* dispatcher){
    struct Scope {uint32_t begin,end,handler,jump;};
    struct Table {uint32_t count;Scope entries[1];};
    const auto* table=reinterpret_cast<const Table*>(dispatcher->data);
    if(table->count>64 || dispatcher->controlPc<dispatcher->imageBase)__ud2();
    const auto control=dispatcher->controlPc-dispatcher->imageBase;
    for(uint32_t i=dispatcher->scopeIndex;i<table->count;++i){const auto scope=table->entries[i];
        if(scope.begin>=scope.end)__ud2();
        if(control<scope.begin || control>=scope.end)continue;
        if(record->flags&Unwinding){
            if(scope.jump)continue;
            if((record->flags&TargetUnwind) && dispatcher->targetIp>=dispatcher->imageBase+scope.begin && dispatcher->targetIp<dispatcher->imageBase+scope.end)continue;
            dispatcher->scopeIndex=i+1;
            reinterpret_cast<void (*)(uint8_t,uint64_t)>(dispatcher->imageBase+scope.handler)(1,frame);
        }else if(scope.jump){
            Pointers pointers{record,context};int32_t decision=1;
            if(scope.handler!=1)decision=reinterpret_cast<int32_t (*)(Pointers*,uint64_t)>(dispatcher->imageBase+scope.handler)(&pointers,frame);
            if(decision<0)return 0;
            if(decision>0)guardian::win::RtlUnwindEx(reinterpret_cast<void*>(frame),reinterpret_cast<void*>(dispatcher->imageBase+scope.jump),record,reinterpret_cast<void*>(uint64_t(record->code)),context,nullptr);
        }
    }return 1;
}
