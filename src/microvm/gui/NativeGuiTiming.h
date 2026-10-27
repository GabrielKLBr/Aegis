#pragma once
#include <Windows.h>
namespace av::microvm::native_gui {
inline void PipePause(bool precise){
    if(!precise){Sleep(1);return;}
    struct Timer {HANDLE handle=CreateWaitableTimerExW(nullptr,nullptr,0x2,TIMER_MODIFY_STATE|SYNCHRONIZE);~Timer(){if(handle)CloseHandle(handle);}};
    static thread_local Timer timer;
    if(timer.handle){LARGE_INTEGER due{};due.QuadPart=-10000;if(SetWaitableTimer(timer.handle,&due,0,nullptr,nullptr,FALSE)){WaitForSingleObject(timer.handle,5);return;}}
    Sleep(1);
}
}
