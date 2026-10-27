#pragma once
#include <Windows.h>
#include "NativeGuiTiming.h"
namespace av::microvm::native_gui {
// A synchronous ReadFile on an empty pipe holds the read-end lock, including
// duplicated handles used by the supervisor's queue monitor. Wait for a full
// bounded packet before reading, so that monitor can never deadlock the writer.
template<class P> bool ReadPacket(HANDLE input,P& packet){
    static_assert(sizeof(P)<4096);
    for(;;){DWORD available=0;if(!PeekNamedPipe(input,nullptr,0,nullptr,&available,nullptr)||available>sizeof(P))return false;
        if(available==sizeof(P)){DWORD read=0;return ReadFile(input,&packet,sizeof(P),&read,nullptr)&&read==sizeof(P);}
#ifdef GUARDIAN_NATIVE_CONTROLS
        PipePause(true);
#else
        PipePause(false);
#endif
    }
}
}
