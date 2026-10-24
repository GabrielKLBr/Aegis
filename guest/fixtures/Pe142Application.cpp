#include "PeRuntimeApi.h"
#include "guardian/NtCompatibility.h"
#include <intrin.h>
using namespace guardian::win;
extern "C" __declspec(dllimport) uint32_t ProbeOrdinal();
extern "C" __declspec(dllimport) uint32_t ProbeData;
extern "C" __declspec(dllimport) uint32_t ForwardValue();
extern "C" __declspec(dllimport) uint32_t DelayValue();
extern "C" __declspec(dllimport) uint32_t WidgetValue();
extern "C" __declspec(dllimport) uint32_t RecordCount(uint32_t);
extern "C" uint32_t GuardianCompatibilityCall(void*) noexcept;
alignas(4096) const volatile uint8_t payload[2u<<20]={0x51};
const void* volatile imageAnchor=const_cast<const uint8_t*>(payload);
extern "C" void* __delayLoadHelper2(void*,void*){__ud2();return nullptr;}
uint32_t Worker(void*){return ProbeOrdinal()==0x43&&WidgetValue()==1?0:90;}
extern "C" int GuardianEntry(){
    if(payload[0]!=0x51||payload[sizeof(payload)-1]!=0||!imageAnchor)return 1;
    if(ProbeOrdinal()!=0x43||ProbeData!=0x12345678||ForwardValue()!=0x43||DelayValue()!=0x5555||WidgetValue()!=1)return 2;
    if(RecordCount(11)!=1||RecordCount(21)!=1)return 3;
    const auto self=GetModuleHandleA(nullptr);const auto resource=FindResourceA(self,reinterpret_cast<const char*>(101),reinterpret_cast<const char*>(10));
    if(!resource||SizeofResource(self,resource)!=4)return 4;
    const auto data=static_cast<const uint16_t*>(LockResource(LoadResource(self,resource)));if(!data||data[0]!=0x1234||data[1]!=0x5678)return 5;
    const auto leaf=GetModuleHandleA("guardian_leaf.dll");if(!leaf)return 6;
    const auto duplicate=LoadLibraryA("GUARDIAN_LEAF");if(duplicate!=leaf||!FreeLibrary(duplicate))return 20;
    const auto value=reinterpret_cast<uint32_t(*)()>(GetProcAddress(leaf,reinterpret_cast<const char*>(7)));
    const auto exported=reinterpret_cast<const uint32_t*>(GetProcAddress(leaf,"ProbeData"));if(!value||value()!=0x43||!exported||*exported!=0x12345678)return 7;
    if(LoadLibraryA("..\\guardian_leaf.dll")||LoadLibraryA("D:\\host.dll")||LoadLibraryA("missing.dll"))return 8;
    for(unsigned i=0;i<2;++i)if(LoadLibraryA("guardian-reject.dll"))return 9;
    const auto bridge=GetModuleHandleA("guardian-bridge.dll");
    if(!bridge||GetModuleHandleA("guardian-late.dll"))return 17;
    const auto late=reinterpret_cast<uint32_t(*)()>(GetProcAddress(bridge,"ForwardLate"));
    if(!late||late()!=0x7777||RecordCount(101)!=1)return 18;
    if(GetProcAddress(bridge,"ForwardRejected")||GetModuleHandleA("guardian-reject.dll")||RecordCount(61)!=3||RecordCount(60)!=3)return 19;
    const auto thread=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);uint32_t code=1;
    if(!thread||WaitForSingleObject(thread,10000)||!GetExitCodeThread(thread,&code)||code||!CloseHandle(thread))return 10;
    ActCtxA context{};context.cb=sizeof(context);context.source="C:\\App\\alternate.manifest";
    const auto activation=CreateActCtxA(&context);uint64_t cookie=0;
    if(activation==reinterpret_cast<void*>(UINT64_MAX)||!ActivateActCtx(activation,&cookie))return 11;
    if(DeactivateActCtx(0,cookie+1))return 12;
    const auto module=LoadLibraryA("widget.dll");const auto widget=reinterpret_cast<uint32_t(*)()>(GetProcAddress(module,"WidgetValue"));
    if(!module||!widget||widget()!=2||!FreeLibrary(module)||!DeactivateActCtx(0,cookie))return 13;ReleaseActCtx(activation);
    if(WidgetValue()!=1)return 14;
    guardian::nt::Query query;query.manifest=UINT32_MAX;if(GuardianCompatibilityCall(&query)||query.reported.build!=query.target.build)return 15;
    constexpr char text[]="PE14.2 large external image, imports, TLS, threads, resources, contexts and rollback PASS\n";
    uint32_t wrote=0;if(!WriteFile(GetStdHandle(uint32_t(-11)),text,sizeof(text)-1,&wrote,nullptr)||wrote!=sizeof(text)-1)return 16;
    return 0;
}
