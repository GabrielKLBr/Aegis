#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <cwchar>
#include "guardian/NativeUser32.h"
namespace {
int clicks=0,changes=0,positions=0,created=0;DWORD owner=0;bool callbackError=false;HWND label=nullptr;bool tick=false;
LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){if(GetCurrentThreadId()!=owner)callbackError=true;
    if(m==WM_NCCREATE)return TRUE;if(m==WM_CREATE){++created;return 0;}
    if(m==WM_COMMAND){if(LOWORD(w)==101&&HIWORD(w)==BN_CLICKED){++clicks;if(label&&!SetWindowTextW(label,L"Clicked in native USER32"))callbackError=true;}if(LOWORD(w)==102&&HIWORD(w)==EN_CHANGE)++changes;return 0;}
    if(m==WM_WINDOWPOSCHANGED){const auto* p=reinterpret_cast<const WINDOWPOS*>(l);if(!p||p->hwnd!=h)callbackError=true;++positions;return 0;}
    if(m==WM_APP+7)return LRESULT(w)+SendMessageW(h,WM_APP+8,0,0);if(m==WM_APP+8)return 40;
    return DefWindowProcW(h,m,w,l);
}
void CALLBACK Timer(HWND h,UINT,UINT_PTR id,DWORD){if(GetCurrentThreadId()!=owner||!KillTimer(h,id))callbackError=true;tick=true;}
bool Check(bool condition,const char* message){if(!condition)std::printf("FAIL %s error=%lu\n",message,GetLastError());return condition;}
}
int main(){owner=GetCurrentThreadId();WNDCLASSW c{};c.lpfnWndProc=Proc;c.lpszClassName=L"Guardian.NativeUser.Demo";if(!Check(RegisterClassW(&c)!=0,"register"))return 1;
    using NativeCall=DWORD(*)(guardian::nativeuser::Call*) noexcept;const auto gateway=reinterpret_cast<NativeCall>(GetProcAddress(GetModuleHandleW(L"win32u.dll"),"GuardianNativeUserCall"));if(!Check(gateway!=nullptr,"owned native gateway"))return 20;
    const guardian::nativeuser::Op forbidden[]{guardian::nativeuser::Op::Hello,guardian::nativeuser::Op::Finish,guardian::nativeuser::Op::ThreadExit};for(auto op:forbidden){guardian::nativeuser::Call request;request.command.op=op;if(!Check(gateway(&request)==ERROR_INVALID_PARAMETER,"kernel-only lifetime control rejected"))return 21;}
    if(!Check(gateway(reinterpret_cast<guardian::nativeuser::Call*>(1))==ERROR_NOACCESS,"guest request pointer probed"))return 22;
    const auto root=CreateWindowExW(0,c.lpszClassName,L"SDK native controls",WS_OVERLAPPEDWINDOW|WS_VISIBLE,160,160,460,240,nullptr,nullptr,nullptr,nullptr);if(!Check(root!=nullptr&&created==1,"root and guest callback"))return 2;
volatile int mode=NATIVE_USER_MODE;if(mode==1){*static_cast<volatile int*>(nullptr)=1;return 90;}if(mode==2){for(;;)Sleep(1);}
    const auto button=CreateWindowExW(0,L"BUTTON",L"Native check",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,10,12,160,28,root,reinterpret_cast<HMENU>(101),nullptr,nullptr);
    const auto edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,10,48,260,28,root,reinterpret_cast<HMENU>(102),nullptr,nullptr);
    label=CreateWindowExW(0,L"STATIC",L"Waiting",WS_CHILD|WS_VISIBLE,10,86,280,28,root,reinterpret_cast<HMENU>(103),nullptr,nullptr);
    if(!Check(button&&edit&&label&&GetParent(edit)==root&&GetDlgItem(root,102)==edit&&GetDlgCtrlID(button)==101,"real child hierarchy"))return 3;
    if(!Check(GetDpiForWindow(root)==96&&SendMessageW(root,WM_APP+7,2,0)==42,"logical DPI and reentrancy"))return 4;
    DWORD pid=0;if(!Check(GetWindowThreadProcessId(root,&pid)==owner&&pid==GetCurrentProcessId(),"guest ownership"))return 5;
    POINT point{3,4};if(!Check(ClientToScreen(edit,&point)&&ScreenToClient(edit,&point)&&point.x==3&&point.y==4,"native client coordinate roundtrip"))return 16;
    SetFocus(edit);if(!Check(GetFocus()==edit,"native focus mapping"))return 17;
    const auto style=GetWindowLongPtrW(root,GWL_STYLE);SetWindowLongPtrW(root,GWL_STYLE,style|WS_CLIPCHILDREN);if(!Check((GetWindowLongPtrW(root,GWL_STYLE)&WS_CLIPCHILDREN)!=0,"native style update"))return 18;
    SendMessageW(button,BM_CLICK,0,0);if(!Check(clicks==1&&SendMessageW(button,BM_GETCHECK,0,0)==BST_CHECKED,"native click and synchronous guest notification"))return 6;
    if(!Check(SetWindowTextW(edit,L"MiniNT native edit")!=FALSE,"native edit text"))return 7;wchar_t text[256]{};
    if(!Check(GetWindowTextW(edit,text,256)==18&&!wcscmp(text,L"MiniNT native edit")&&changes>=1,"edit text and EN_CHANGE"))return 8;
    SendMessageW(edit,EM_SETSEL,1,5);DWORD start=0,end=0;const auto selection=SendMessageW(edit,EM_GETSEL,WPARAM(&start),LPARAM(&end));if(!Check(start==1&&end==5&&LOWORD(selection)==1&&HIWORD(selection)==5,"guest selection pointers marshalled"))return 9;
    if(!Check(SetWindowPos(root,nullptr,170,180,480,260,SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE&&positions>0,"native geometry and guest WINDOWPOS"))return 10;
    EnableWindow(root,FALSE);const auto popup=CreateWindowExW(0,c.lpszClassName,L"Owned modal subset",WS_OVERLAPPEDWINDOW|WS_VISIBLE,220,220,250,150,root,nullptr,nullptr,nullptr);
    if(!Check(popup&&!IsWindowEnabled(root)&&GetParent(popup)==root,"owned window and owner disabled"))return 11;
    if(!Check(SetParent(label,popup)==root&&GetParent(label)==popup&&SetParent(label,root)==popup,"native child reparenting"))return 19;DestroyWindow(popup);EnableWindow(root,TRUE);
    if(!Check(SetTimer(root,5,10,Timer)==5,"native timer"))return 12;const auto deadline=GetTickCount64()+1000;MSG msg{};while(!tick&&GetTickCount64()<deadline){while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(1);}if(!Check(tick&&!callbackError,"timer callback in guest"))return 13;
#if NATIVE_USER_MODE==3
    SetWindowTextW(edit,L"");clicks=0;SetWindowTextW(label,L"Input ready");const auto until=GetTickCount64()+4000;bool typed=false;
    while(GetTickCount64()<until&&(!typed||!clicks)){while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE))DispatchMessageW(&msg);GetWindowTextW(edit,text,256);typed=!wcscmp(text,L"abc");Sleep(1);}
    if(!Check(typed&&clicks,"host native keyboard and mouse input"))return 14;
#endif
    if(!Check(!callbackError&&DestroyWindow(root)&&!IsWindow(edit)&&!IsWindow(button),"callback thread and recursive cleanup"))return 15;
    std::puts("Native USER32 SDK controls, guest callbacks, messages and timers PASS");return 0;
}
