#pragma once
#include <stdint.h>
#include "PeProfile.h"
namespace guardian::runtime {
inline constexpr uint32_t Version=1,MaxHandles=16;
inline constexpr uint64_t FirstHandle=0x4756000000000000ull,CurrentUser=0xFFFFFFFF80000001ull,LocalMachine=0xFFFFFFFF80000002ull,Console=0x4757000000000001ull;
inline constexpr uint32_t NegativeSeek=0xE1000001;
enum class Op:uint32_t {Open=1,Read,Write,Seek,Size,Close,Flush,RegOpen,RegSet,RegGet,RegClose,ConsoleWrite,StackCapture=16,FirmwareRead=17,DiskSpace=18,ClockRead=19,MemoryAllocate=20,MemoryFree,MemoryProtect,MemoryQuery,Delay=25,FileTimesRead=26,FileTimesWrite,EnvironmentRead,FileRename,FileDelete,MutexCreate=32,Wait,Unlock,CloseSync,ThreadCreate,ThreadResume,ThreadQuery,Yield,ProcessCreate,ProcessQuery,ThreadExit,ProcessExit,LibraryLoad=48,LibraryFinish,LibraryFree,LibraryFinishFree,LibraryGetProc,LibraryNext,LibraryFinishGetProc,ServiceCreate=64,ServiceStart,ServiceStop,ServiceQuery,AnalysisInfo,NetworkEcho,ResourceFind=80,ResourceInfo,ModuleHandle,ContextCreate=88,ContextActivate,ContextDeactivate,ContextRelease};
struct Command {
    uint32_t version=Version,bytes=sizeof(Command);Op op=Op::Open;uint32_t flags=0;
    uint64_t handle=0,argument=0;
    uint32_t access=0,share=0,disposition=0,length=0,type=0,status=0,transferred=0,reserved=0;
    char path[128]{},name[32]{};uint8_t data[peprofile::RuntimeData]{};
};
static_assert(sizeof(Command)==224+peprofile::RuntimeData);
}
