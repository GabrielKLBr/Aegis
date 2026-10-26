#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <stdio.h>
#include "guardian/NativeGui.h"
namespace n=guardian::nativegui;
extern "C" __declspec(dllimport) DWORD GuardianNativeGuiCall(n::Call*);
static uint64_t Invoke(n::Command c){n::Call call;call.command=c;if(GuardianNativeGuiCall(&call)||call.status)ExitProcess(21);return call.value;}
int main(){n::Command c;c.op=n::Op::Window;c.rect={120,120,520,360};constexpr char title[]="Native GDI from MiniNT";for(uint32_t i=0;i<sizeof(title);++i)c.title[i]=title[i];const auto window=Invoke(c);
    c={};c.op=n::Op::Brush;c.color=0x174C92;const auto blue=Invoke(c);c={};c.op=n::Op::Brush;c.color=0x19AC52;const auto green=Invoke(c);c={};c.op=n::Op::Dc;c.target=window;const auto dc=Invoke(c);
    c={};c.op=n::Op::Fill;c.target=dc;c.object=blue;c.rect={0,0,400,240};Invoke(c);c.object=green;c.rect={40,40,200,160};Invoke(c);
    c={};c.op=n::Op::Pixel;c.target=dc;c.rect={10,10,11,11};if(Invoke(c)!=0x174C92)return 22;c.rect={60,60,61,61};if(Invoke(c)!=0x19AC52)return 23;
    n::Call invalid;invalid.command.op=n::Op::Fill;invalid.command.target=window;invalid.command.object=blue;invalid.command.rect={0,0,10,10};if(GuardianNativeGuiCall(&invalid)!=ERROR_INVALID_HANDLE)return 24;
    puts("Native GUI host FillRect/GetPixel PASS; no guest framebuffer");
#if NATIVE_GUI_MODE==1
    *static_cast<volatile int*>(nullptr)=1;
#elif NATIVE_GUI_MODE==2
    Sleep(20000);
#else
    Sleep(700);
#endif
    c={};c.op=n::Op::ReleaseDc;c.target=dc;Invoke(c);c={};c.op=n::Op::DeleteBrush;c.target=blue;Invoke(c);c.target=green;Invoke(c);c={};c.op=n::Op::DestroyWindow;c.target=window;Invoke(c);c={};c.op=n::Op::Finish;Invoke(c);return 0;}
