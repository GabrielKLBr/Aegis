#include "Win32Api.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
extern "C" int _fltused=0;
extern "C" __declspec(dllimport) void RtlCaptureContext(guardian::exceptions::Context*);
extern "C" __declspec(dllimport) void* RtlLookupFunctionEntry(uint64_t,uint64_t*,void*);
extern "C" __declspec(dllimport) void* RtlPcToFileHeader(void*,void**);
extern "C" __declspec(dllimport) void* RtlVirtualUnwind(uint32_t,uint64_t,uint64_t,void*,guardian::exceptions::Context*,void**,uint64_t*,void*);
#include <intrin.h>
__declspec(noinline) static bool UnwindContext(){guardian::exceptions::Context c{};RtlCaptureContext(&c);uint64_t base=0;auto* function=RtlLookupFunctionEntry(c.rip,&base,nullptr);void* module=nullptr;if(!function||!RtlPcToFileHeader(reinterpret_cast<void*>(c.rip),&module)||reinterpret_cast<uint64_t>(module)!=base)return false;
    const auto before=c.gpr[4];void* data=nullptr;uint64_t frame=0;const auto handler=RtlVirtualUnwind(0,base,c.rip,function,&c,&data,&frame,nullptr);return !handler&&frame>=before&&c.gpr[4]>before&&c.rip==reinterpret_cast<uint64_t>(_ReturnAddress());}
extern "C" uint32_t GuardianEntry();
extern "C" void* volatile CrtRelocationAnchor=reinterpret_cast<void*>(&GuardianEntry);
extern "C" uint32_t GuardianEntry(){using namespace guardian::win;
    const char text[]="CRT original entry\n";Dword n=0;WriteFile(GetStdHandle(Dword(-11)),text,sizeof(text)-1,&n,nullptr);
    if(!UnwindContext())return 4;
    if(*reinterpret_cast<void**>(__readgsqword(0x60)+0x30)!=GetProcessHeap())return 5;
    auto* p=static_cast<uint32_t*>(malloc(100));if(!p)return 1;*p=42;WriteFile(GetStdHandle(Dword(-11)),"malloc OK\n",10,&n,nullptr);if(pow(2.,8.)!=256.)return 2;free(p);WriteFile(GetStdHandle(Dword(-11)),"free OK\n",8,&n,nullptr);
    if(puts("CRT 14.5 original UCRT allocation math stdio")<0||fflush(nullptr))return 3;WriteFile(GetStdHandle(Dword(-11)),"puts OK\n",8,&n,nullptr);return 0;}
