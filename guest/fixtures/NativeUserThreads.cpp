#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cstdio>
namespace {
HWND mainWindow=nullptr,workerWindow=nullptr;volatile LONG ready=0,stop=0;DWORD mainTid=0,workerTid=0;bool wrongThread=false;LONG received=0;
LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_NCCREATE)return TRUE;
    if(m==WM_APP+1){if(GetCurrentThreadId()!=workerTid)wrongThread=true;++received;return SendMessageW(mainWindow,WM_APP+2,0,0)+123;}
    if(m==WM_APP+2){if(GetCurrentThreadId()!=mainTid)wrongThread=true;return 77;}
    return DefWindowProcW(h,m,w,l);
}
DWORD WINAPI Worker(void*){workerTid=GetCurrentThreadId();workerWindow=CreateWindowExW(0,L"Guardian.NativeUser.Threads",L"Receiver",WS_OVERLAPPEDWINDOW|WS_VISIBLE,300,260,250,150,nullptr,nullptr,nullptr,nullptr);InterlockedExchange(&ready,workerWindow?1:-1);MSG msg{};
    while(!stop){while(PeekMessageW(&msg,nullptr,WM_APP,WM_APP+10,PM_REMOVE))DispatchMessageW(&msg);Sleep(1);}
    // A live guest thread that does not dispatch messages must not hang the host UI.
    // Verify canceled application sends, then let thread teardown remove native
    // input that arrived meanwhile. Draining arbitrary input can starve exit.
    InterlockedExchange(&ready,2);Sleep(2800);while(PeekMessageW(&msg,nullptr,WM_APP,WM_APP+10,PM_REMOVE))DispatchMessageW(&msg);return 0;
}
}
int main(){mainTid=GetCurrentThreadId();WNDCLASSW c{};c.lpfnWndProc=Proc;c.lpszClassName=L"Guardian.NativeUser.Threads";if(!RegisterClassW(&c))return 1;mainWindow=CreateWindowExW(0,c.lpszClassName,L"Sender",WS_OVERLAPPEDWINDOW|WS_VISIBLE,160,160,250,150,nullptr,nullptr,nullptr,nullptr);if(!mainWindow)return 2;
    const auto thread=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);if(!thread)return 3;while(!ready)Sleep(1);if(ready!=1||SendMessageW(workerWindow,WM_APP+1,0,0)!=200||received!=1||wrongThread)return 4;
    InterlockedExchange(&stop,1);while(ready!=2)Sleep(1);SetLastError(0);const auto started=GetTickCount64();const auto value=SendMessageW(workerWindow,WM_APP+1,0,0);const auto error=GetLastError();
    if(value||error!=ERROR_TIMEOUT||GetTickCount64()-started>2600)return 5;
    if(WaitForSingleObject(thread,4000)!=WAIT_OBJECT_0||received!=1||wrongThread||IsWindow(workerWindow))return 6;CloseHandle(thread);DestroyWindow(mainWindow);
    std::puts("Native USER32 interthread return, nested send, timeout cancellation PASS");return 0;
}
