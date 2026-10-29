#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <intrin.h>
#ifdef COMPAT32_INPUT
#define COMPAT32_GUI 1
#endif
#ifdef COMPAT32_GUI_CRASH
#define COMPAT32_GUI 1
#endif
namespace {
int failed=0,clicks=0,closed=0;DWORD owner=0;HWND inputEdit=nullptr;
void Check(bool condition){if(!condition)++failed;}
void Print(const char* text){DWORD n=0;while(text[n])++n;DWORD done=0;Check(WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),text,n,&done,nullptr)&&done==n);}
LRESULT CALLBACK Proc(HWND h,UINT message,WPARAM wp,LPARAM lp){
    Check(GetCurrentThreadId()==owner);
    if(message==WM_NCCREATE)return TRUE;
    if(message==WM_COMMAND&&HIWORD(wp)==BN_CLICKED){++clicks;
#ifdef COMPAT32_INPUT
        wchar_t text[8]{};Check(GetWindowTextW(inputEdit,text,8)==3&&text[0]=='a'&&text[1]=='b'&&text[2]=='c');
        DestroyWindow(h);
#endif
        return 0;}
    if(message==WM_DESTROY){++closed;PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,message,wp,lp);
}
}
extern "C" int __cdecl Compat32Main(){
    static_assert(sizeof(void*)==4);owner=GetCurrentThreadId();Check(owner&&GetCurrentProcessId());
    WORD selector=0;__asm mov ax,cs
    __asm mov selector,ax
    Check(selector==0x1B);
    BOOL wow=FALSE;Check(IsWow64Process(GetCurrentProcess(),&wow)&&wow);
    Check(__readfsdword(0x18)==0x620000u);
    const auto kernel=GetModuleHandleW(L"kernel32.dll");Check(kernel!=nullptr);
    Check(GetModuleHandleW(L"C:\\Windows\\System32\\kernel32.dll")==kernel);
    Check(GetModuleHandleW(L"C:\\Windows\\SysWOW64\\kernel32.dll")==kernel);
    Check(GetProcAddress(kernel,"GetCurrentThreadId")!=nullptr);
    Check(!GetProcAddress(kernel,"NoSuchApi1412")&&GetLastError()==127);
    auto* memory=static_cast<DWORD*>(HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,32));Check(memory&&memory[0]==0&&memory[7]==0);
    if(memory){memory[0]=42;Check(memory[0]==42&&HeapFree(GetProcessHeap(),0,memory));}
    void* regions[64]{};for(auto& region:regions){region=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);Check(region!=nullptr);}
    Check(!VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)&&GetLastError()==8);
    for(auto region:regions)if(region)Check(VirtualFree(region,0,MEM_RELEASE)!=0);
    Check(!VirtualFree(regions[0],0,MEM_RELEASE));
    Check(!VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(!VirtualAlloc(nullptr,0x100001,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(!VirtualFree(kernel,0,MEM_RELEASE));
    DWORD done=99;Check(!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),reinterpret_cast<void*>(1),16,&done,nullptr)&&done==0);
#ifdef COMPAT32_GUI
    WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.lpszClassName=L"Aegis.Compat32.Acceptance";Check(RegisterClassW(&wc)!=0);
    const auto window=CreateWindowExW(0,wc.lpszClassName,L"Aegis x86 acceptance",WS_OVERLAPPEDWINDOW,100,100,360,220,nullptr,nullptr,nullptr,nullptr);Check(window!=nullptr);
    const auto button=CreateWindowExW(0,L"BUTTON",L"x86 button",WS_CHILD|WS_VISIBLE,12,12,120,28,window,reinterpret_cast<HMENU>(101),nullptr,nullptr);
    const auto edit=CreateWindowExW(0,L"EDIT",L"x86 text",WS_CHILD|WS_VISIBLE|WS_BORDER,12,52,220,24,window,reinterpret_cast<HMENU>(102),nullptr,nullptr);Check(button&&edit);inputEdit=edit;
    ShowWindow(window,SW_SHOW);
#ifdef COMPAT32_GUI_CRASH
    *reinterpret_cast<volatile DWORD*>(1)=42;
#endif
#ifdef COMPAT32_INPUT
    const auto label=CreateWindowExW(0,L"STATIC",L"Input ready",WS_CHILD|WS_VISIBLE,12,90,220,24,window,reinterpret_cast<HMENU>(103),nullptr,nullptr);Check(label!=nullptr);SetWindowTextW(edit,L"");
#else
    SendMessageW(button,BM_CLICK,0,0);Check(clicks==1);
    wchar_t text[32]{};Check(GetWindowTextW(edit,text,32)==8&&text[0]=='x');
    Check(PostMessageW(window,WM_CLOSE,0,0)!=0);
#endif
    MSG message{};int received=0;
    while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);if(++received>100)break;}
    Check(closed==1&&!IsWindow(window));
#ifndef COMPAT32_INPUT
    const auto replacement=CreateWindowExW(0,wc.lpszClassName,L"replacement",WS_OVERLAPPEDWINDOW,100,100,200,120,nullptr,nullptr,nullptr,nullptr);
    Check(replacement&&replacement!=window&&!IsWindow(window));if(replacement)Check(DestroyWindow(replacement)!=0);
#endif
#endif
#ifdef COMPAT32_CRASH
    *reinterpret_cast<volatile DWORD*>(1)=5;
#endif
#ifdef COMPAT32_EXIT
    ExitProcess(17);
#else
    Print(failed?"X86_ACCEPTANCE_FAIL\n":"X86_ACCEPTANCE_PASS\n");return failed?1:0;
#endif
}
