#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
namespace {int failed=0,clicked=0;HWND edit=nullptr;constexpr DWORD token=0x14130001;const DWORD* volatile relocationAnchor=&token;
void Check(bool value){if(!value)++failed;}
void State(bool write){DWORD value=token,done=0;const auto f=CreateFileW(L"C:\\App\\phase1413.dat",GENERIC_READ|GENERIC_WRITE,0,nullptr,write?CREATE_ALWAYS:OPEN_EXISTING,0,nullptr);Check(f!=INVALID_HANDLE_VALUE);if(f!=INVALID_HANDLE_VALUE){Check(write?WriteFile(f,&value,4,&done,nullptr)!=0:ReadFile(f,&value,4,&done,nullptr)!=0);Check(done==4&&value==token);CloseHandle(f);}HKEY key=nullptr;Check(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Aegis1413",0,nullptr,0,KEY_ALL_ACCESS,nullptr,&key,nullptr)==0);if(key){DWORD type=0,size=4;value=token;Check(write?RegSetValueExW(key,L"Token",0,REG_DWORD,reinterpret_cast<const BYTE*>(&value),4)==0:RegQueryValueExW(key,L"Token",nullptr,&type,reinterpret_cast<BYTE*>(&value),&size)==0);Check(value==token);RegCloseKey(key);}}
LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM wp,LPARAM lp){if(message==WM_NCCREATE)return TRUE;if(message==WM_COMMAND&&LOWORD(wp)==101&&HIWORD(wp)==BN_CLICKED){State(false);wchar_t text[8]{};Check(GetWindowTextW(edit,text,8)==3&&text[0]=='a'&&text[1]=='b'&&text[2]=='c');++clicked;DestroyWindow(window);return 0;}if(message==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(window,message,wp,lp);}
}
extern "C" void UnifiedProbeEntry(){Check(relocationAnchor&&*relocationAnchor==token);State(true);WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.lpszClassName=L"Aegis.Unified1413";Check(RegisterClassW(&wc)!=0);auto root=CreateWindowExW(0,wc.lpszClassName,L"Unified recovery",WS_OVERLAPPEDWINDOW|WS_VISIBLE,120,120,360,220,nullptr,nullptr,nullptr,nullptr);Check(root!=nullptr);
    Check(CreateWindowExW(0,L"BUTTON",L"Resume check",WS_CHILD|WS_VISIBLE,10,10,140,26,root,reinterpret_cast<HMENU>(101),nullptr,nullptr)!=nullptr);
    edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER,10,45,240,26,root,reinterpret_cast<HMENU>(102),nullptr,nullptr);Check(edit!=nullptr);
    Check(CreateWindowExW(0,L"STATIC",L"Input ready",WS_CHILD|WS_VISIBLE,10,85,240,26,root,reinterpret_cast<HMENU>(103),nullptr,nullptr)!=nullptr);
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}Check(clicked==1);const char* text=failed?"UNIFIED_RECOVERY_FAIL\n":"UNIFIED_RECOVERY_PASS\n";DWORD bytes=0;while(text[bytes])++bytes;DWORD done=0;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),text,bytes,&done,nullptr);ExitProcess(failed?1:0);
}
