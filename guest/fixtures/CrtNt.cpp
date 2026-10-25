#include "NativeApi.h"
#include "PeRuntimeApi.h"
#include "guardian/Win32.h"
#include "../Unwind.h"
#include <intrin.h>
extern "C" uint32_t GuardianCrtMetaCall(guardian::win32::Request*) noexcept;
namespace {
using namespace guardian;
bool Image(void*,uint64_t pc,unwind::Image& out) noexcept {win32::Request r;r.op=21;r.args[0]=pc;if(GuardianCrtMetaCall(&r))return false;out={r.args[1],uint32_t(r.args[2]),uint32_t(r.args[3]),uint32_t(r.args[4])};return true;}
bool Read(void*,uint64_t at,void* out,uint32_t n) noexcept {for(uint32_t i=0;i<n;++i)static_cast<uint8_t*>(out)[i]=reinterpret_cast<const uint8_t*>(at)[i];return true;}
bool Executable(void*,uint64_t pc) noexcept {unwind::Image image{};return Image(nullptr,pc,image);}
}
extern "C" __declspec(dllexport) void* RtlPcToFileHeader(void* pc,void** out){guardian::win32::Request r;r.op=22;r.args[0]=reinterpret_cast<uint64_t>(pc);if(GuardianCrtMetaCall(&r)){if(out)*out=nullptr;return nullptr;}auto* base=reinterpret_cast<void*>(r.args[1]);if(out)*out=base;return base;}
extern "C" __declspec(dllexport) void RtlUnwind(void* frame,void* ip,guardian::exceptions::Record* record,void* value){guardian::win::RtlUnwindEx(frame,ip,record,value,nullptr,nullptr);}
extern "C" __declspec(dllexport) void* RtlLookupFunctionEntry(uint64_t pc,uint64_t* base,void*){unwind::Image image{};if(!base||!Image(nullptr,pc,image))return nullptr;*base=image.base;const auto* table=reinterpret_cast<const uint32_t*>(image.base+image.table);uint32_t lo=0,hi=image.count;while(lo<hi){const auto mid=lo+(hi-lo)/2;const auto* f=table+mid*3;if(pc-image.base<f[0])hi=mid;else if(pc-image.base>=f[1])lo=mid+1;else return const_cast<uint32_t*>(f);}return nullptr;}
extern "C" __declspec(dllexport) void* RtlVirtualUnwind(uint32_t kind,uint64_t base,uint64_t pc,void* function,exceptions::Context* context,void** data,uint64_t* frame,void* pointers){
    if(!context||!data||!frame||pointers||kind>2)__ud2();unwind::Image image{};if(!Image(nullptr,pc,image)||image.base!=base)__ud2();uint64_t actual=0;if(RtlLookupFunctionEntry(pc,&actual,nullptr)!=function)__ud2();
    unwind::Context c{};for(uint32_t i=0;i<16;++i){c.gpr[i]=context->gpr[i];for(uint32_t j=0;j<16;++j)c.xmm[i][j]=context->fx[160+i*16+j];}c.rip=pc;
    const auto teb=__readgsqword(0x30);const unwind::Stack stack{*reinterpret_cast<const uint64_t*>(teb+0x10),*reinterpret_cast<const uint64_t*>(teb+8)};const unwind::Backend api{Image,Read,Executable};unwind::Handler h{};
    const auto status=unwind::InspectHandler(api,nullptr,stack,kind==2?unwind::HandlerType::Termination:unwind::HandlerType::Exception,c,h);if(status!=unwind::Status::Ok)__ud2();
    *frame=h.establisher?h.establisher:context->gpr[4];*data=reinterpret_cast<void*>(h.data);for(uint32_t i=0;i<16;++i){context->gpr[i]=c.gpr[i];for(uint32_t j=0;j<16;++j)context->fx[160+i*16+j]=c.xmm[i][j];}context->rip=c.rip;return reinterpret_cast<void*>(kind?h.routine:0);
}
