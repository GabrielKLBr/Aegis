#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <wininet.h>
#include <cstdio>
#include <cstring>
namespace {
UINT checks=0;
void Check(bool value,const char* name){++checks;if(!value){std::printf("WinINet FAIL %s error=%lu\n",name,GetLastError());ExitProcess(83);}}
void Read(HINTERNET request){char result[64]{};DWORD total=0;do{DWORD bytes=0;Check(InternetReadFile(request,result+total,7,&bytes),"response read");if(!bytes)break;total+=bytes;Check(total+7<sizeof(result),"body bound");}while(true);constexpr char expected[]="MiniNT HTTP works\n";Check(total==sizeof(expected)-1&&!std::memcmp(result,expected,total),"response body");}
}
int main(int argc,char** argv){wchar_t path[1024]=L"C:\\App\\network.cfg";
#ifdef INTERNET_REFERENCE
 Check(argc==2&&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[1],-1,path,1024)>0,"owned configuration");
#else
 (void)argc;(void)argv;
#endif
 char text[512]{};DWORD bytes=0;const auto file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);Check(file!=INVALID_HANDLE_VALUE&&ReadFile(file,text,511,&bytes,nullptr)&&CloseHandle(file),"test configuration read");UINT at=0;while(text[at]&&text[at]!=' ')++at;while(text[at]==' ')++at;UINT port=0;while(text[at]>='0'&&text[at]<='9')port=port*10+UINT(text[at++]-'0');Check(port>0&&port<=65535,"HTTP port");const auto session=InternetOpenW(L"MiniNT WinINet test",INTERNET_OPEN_TYPE_DIRECT,nullptr,nullptr,0);Check(session!=nullptr,"session create");const auto connection=InternetConnectW(session,L"localhost",USHORT(port),nullptr,nullptr,INTERNET_SERVICE_HTTP,0,0);Check(connection!=nullptr,"connection create");const auto request=HttpOpenRequestW(connection,L"GET",L"/",nullptr,nullptr,nullptr,INTERNET_FLAG_RELOAD|INTERNET_FLAG_NO_CACHE_WRITE,0);Check(request&&HttpSendRequestW(request,nullptr,0,nullptr,0),"request send");DWORD status=0,length=4;Check(HttpQueryInfoW(request,HTTP_QUERY_STATUS_CODE|HTTP_QUERY_FLAG_NUMBER,&status,&length,nullptr)&&status==200,"status response");DWORD available=0;Check(InternetQueryDataAvailable(request,&available,0,0)&&available>0,"available data");Read(request);Check(InternetCloseHandle(request)&&InternetCloseHandle(connection),"request handles closed");wchar_t url[128]=L"http://localhost:";UINT offset=17;wchar_t digits[8]{};UINT count=0;do{digits[count++]=wchar_t(L'0'+port%10);port/=10;}while(port);for(UINT i=0;i<count;++i)url[offset++]=digits[count-1-i];url[offset++]=L'/';url[offset]=0;const auto direct=InternetOpenUrlW(session,url,nullptr,0,INTERNET_FLAG_RELOAD|INTERNET_FLAG_NO_CACHE_WRITE,0);Check(direct!=nullptr,"open URL");Read(direct);Check(InternetCloseHandle(direct)&&InternetCloseHandle(session),"URL handles closed");std::printf("WinINet controlled HTTP PASS checks=%u\n",checks);return 0;}
