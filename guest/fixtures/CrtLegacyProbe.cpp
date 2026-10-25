#include "Win32Api.h"
using namespace guardian::win;
extern "C" uint32_t GuardianEntry();
extern "C" void* volatile CrtLegacyAnchor=reinterpret_cast<void*>(&GuardianEntry);
extern "C" uint32_t GuardianEntry(){const auto library=LoadLibraryA("msvcrt.dll");if(!library)return 1;
    const auto allocate=reinterpret_cast<void*(*)(uint64_t)>(GetProcAddress(library,"malloc"));
    const auto release=reinterpret_cast<void(*)(void*)>(GetProcAddress(library,"free"));
    const auto length=reinterpret_cast<uint64_t(*)(const char*)>(GetProcAddress(library,"strlen"));
    const auto number=reinterpret_cast<int(*)(const char*)>(GetProcAddress(library,"atoi"));
    if(!allocate||!release||!length||!number)return 2;auto* bytes=static_cast<char*>(allocate(64));if(!bytes)return 3;
    bytes[0]='4';bytes[1]='2';bytes[2]=0;if(number(bytes)!=42||length(bytes)!=2)return 4;release(bytes);
    if(!FreeLibrary(library))return 5;const char text[]="CRT 14.5 original MSVCRT PASS\n";Dword done=0;return WriteFile(GetStdHandle(Dword(-11)),text,sizeof(text)-1,&done,nullptr)?0u:6u;
}
