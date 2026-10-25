extern "C" __declspec(noreturn) void CrtUnsupportedExport(const char*);
namespace {
Dword errorMode=0;void* topFilter=nullptr;Dword outputMode=3;
Bool Unsupported(const char* name){char text[128]="CRT unsupported API: ";uint32_t at=21;for(uint32_t i=0;name[i]&&at<125;++i)text[at++]=name[i];text[at++]='\n';Dword n=0;win::WriteFile(win::GetStdHandle(Dword(-11)),text,at,&n,nullptr);win::SetLastError(50);return 0;}
uint64_t PointerKey(){uint64_t key=*reinterpret_cast<const uint64_t*>(__readgsqword(0x60)+0x10);return key^uint64_t(win::GetCurrentProcessId())*0x9E3779B97F4A7C15ull;}
}
extern "C" void* KbEncodePointer(void* p){return reinterpret_cast<void*>(reinterpret_cast<uint64_t>(p)^PointerKey());}
extern "C" void* KbDecodePointer(void* p){return KbEncodePointer(p);}
extern "C" Dword KbSetErrorMode(Dword value){const auto old=errorMode;errorMode=value;return old;}
extern "C" void* KbSetUnhandledExceptionFilter(void* filter){const auto old=topFilter;topFilter=filter;return old;}
extern "C" int32_t KbUnhandledExceptionFilter(exceptions::Pointers* p){return topFilter?reinterpret_cast<int32_t(*)(exceptions::Pointers*)>(topFilter)(p):1;}
extern "C" Bool KbIsDebuggerPresent(){return *reinterpret_cast<const uint8_t*>(__readgsqword(0x60)+2)!=0;}
extern "C" Bool KbTerminateProcess(Handle p,Dword status){return Ok(native::NtTerminateProcess(p,status));}
extern "C" Bool KbInitializeCriticalSectionEx(CriticalSection*,Dword,Dword);
extern "C" Bool KbInitializeCriticalSectionAndSpinCount(CriticalSection* cs,Dword spin){return KbInitializeCriticalSectionEx(cs,spin,0);}
extern "C" void KbGetStartupInfoW(StartupInfoA* out){if(!out){win::SetLastError(87);return;}*out={};out->cb=sizeof(*out);out->input=reinterpret_cast<Handle>(UINT64_MAX);out->output=out->error=win::GetStdHandle(Dword(-11));}
extern "C" Bool KbIsProcessorFeaturePresent(Dword feature){int cpu[4]{};__cpuid(cpu,1);switch(feature){case 3:return (cpu[3]&(1<<23))!=0;case 6:return (cpu[3]&(1<<25))!=0;case 8:return (cpu[3]&(1<<4))!=0;case 10:return (cpu[3]&(1<<26))!=0;case 11:return 0;case 13:return (cpu[2]&1)!=0;case 36:return (cpu[2]&(1<<9))!=0;case 37:return (cpu[2]&(1<<19))!=0;case 38:return (cpu[2]&(1<<20))!=0;default:return 0;}}
extern "C" void KbOutputDebugStringA(const char* text){if(!text)return;Dword n=0;while(n<512&&text[n])++n;Dword done=0;win::WriteFile(win::GetStdHandle(Dword(-11)),text,n,&done,nullptr);}
extern "C" void KbOutputDebugStringW(const uint16_t* text){if(!text)return;char data[512]{};Dword e=0;const auto n=nls::Wide(65001,0,text,-1,data,sizeof(data),nullptr,nullptr,e);if(n)KbOutputDebugStringA(data);else win::SetLastError(e);}
extern "C" void KbDebugBreak(){__debugbreak();}
extern "C" Bool KbBeep(Dword,Dword){return Unsupported("Beep");}
extern "C" Bool KbGetModuleHandleExW(Dword flags,const uint16_t* name,Handle* out){if(!out||(flags&~7u)||(flags&1))return Fail(50,0);if(flags&4){win32::Request r;r.op=22;r.args[0]=reinterpret_cast<uint64_t>(name);const auto s=GuardianWin32Call(&r);if(s){*out=nullptr;return Ok(s);}*out=reinterpret_cast<Handle>(r.args[1]);if(flags&2)return 1;char path[128]{};if(!KbGetModuleFileNameA(*out,path,128))return 0;*out=win::LoadLibraryA(path);return *out!=nullptr;}
    *out=flags&2?KbGetModuleHandleW(name):KbLoadLibraryW(name);return *out!=nullptr;}
extern "C" Handle KbLoadLibraryExW(const uint16_t* name,Handle file,Dword flags){if(file||(flags&&flags!=0x800))return Fail(50,static_cast<Handle>(nullptr));return KbLoadLibraryW(name);}
extern "C" __declspec(noreturn) void KbFreeLibraryAndExitThread(Handle h,Dword code){win::FreeLibrary(h);win::ExitThread(code);}
extern "C" Dword KbGetFileType(Handle h){if(reinterpret_cast<uint64_t>(h)==runtime::Console)return 2;int64_t size=0;return KbGetFileSizeEx(h,&size)?1u:0u;}
extern "C" Bool KbGetConsoleMode(Handle h,Dword* out){if(reinterpret_cast<uint64_t>(h)!=runtime::Console)return Fail(6,0);if(!out)return Fail(87,0);*out=outputMode;return 1;}
extern "C" Bool KbSetConsoleMode(Handle h,Dword mode){if(reinterpret_cast<uint64_t>(h)!=runtime::Console)return Fail(6,0);if(mode&~3u)return Fail(50,0);outputMode=mode;return 1;}
extern "C" Dword KbGetConsoleCP(){return 1252;}extern "C" Dword KbGetConsoleOutputCP(){return 65001;}extern "C" Dword KbGetOEMCP(){return 850;}
extern "C" Bool KbWriteConsoleW(Handle h,const uint16_t* text,Dword characters,Dword* count,void* reserved){if(count)*count=0;if(reserved||!text||characters>128||reinterpret_cast<uint64_t>(h)!=runtime::Console)return Fail(87,0);if(!characters)return 1;
    char data[512]{};Dword e=0;const auto bytes=nls::Wide(65001,0x80,text,int(characters),data,sizeof(data),nullptr,nullptr,e);if(!bytes)return Fail(e,0);Dword done=0;if(!win::WriteFile(h,data,Dword(bytes),&done,nullptr))return 0;if(count)*count=characters;return 1;}
extern "C" Bool KbGetNumberOfConsoleInputEvents(Handle,Dword*){return Fail(6,0);}
extern "C" Bool KbSetConsoleCtrlHandler(void*,Bool){return Unsupported("SetConsoleCtrlHandler");}
extern "C" Bool KbReadConsoleW(Handle,void*,Dword,Dword*,void*){return Fail(6,0);}
extern "C" Bool KbReadConsoleInputW(Handle,void*,Dword,Dword*){return Fail(6,0);}
extern "C" Bool KbReadConsoleInputA(Handle,void*,Dword,Dword*){return Fail(6,0);}
extern "C" Bool KbPeekConsoleInputA(Handle,void*,Dword,Dword*){return Fail(6,0);}
extern "C" Bool KbSetStdHandle(Dword,Handle){return Unsupported("SetStdHandle");}
struct CrtCPInfo {Dword maximum;uint8_t replacement[2],lead[12];};
extern "C" Bool KbIsValidCodePage(Dword page){return page==0||page==1252||page==65001||page==850||page==437;}
extern "C" Bool KbGetCPInfo(Dword page,CrtCPInfo* out){if(!out||!KbIsValidCodePage(page))return Fail(87,0);*out={};out->maximum=page==65001?4u:1u;out->replacement[0]='?';return 1;}
extern "C" Dword KbGetUserDefaultLCID(){return 0x416;}extern "C" Bool KbIsValidLocale(Dword locale,Dword flags){return !(flags&~3u)&&(locale==0x416||locale==0x409||locale==0x7f||locale==0x400||locale==0x800);}
extern "C" int KbGetLocaleInfoW(Dword locale,Dword type,uint16_t* out,int cap){if(!KbIsValidLocale(locale,0)||cap<0||(cap&&!out))return Fail(87,0);if(locale==0x7f)return Fail(50,0);if(locale==0x400||locale==0x800)locale=0x416;const bool numeric=(type&0x20000000)!=0;type&=~0xA0000000u;const wchar_t* text=nullptr;
    switch(type){case 1:text=locale==0x409?L"0409":L"0416";break;case 2:text=locale==0x409?L"English":L"Português (Brasil)";break;case 3:text=locale==0x409?L"ENU":L"PTB";break;case 5:text=locale==0x409?L"1":L"55";break;case 6:text=locale==0x409?L"United States":L"Brasil";break;case 0x1001:text=locale==0x409?L"English":L"Portuguese";break;case 7:text=locale==0x409?L"USA":L"BRA";break;case 8:text=locale==0x409?L"United States":L"Brasil";break;case 0x1002:text=locale==0x409?L"United States":L"Brazil";break;case 9:text=locale==0x409?L"0409":L"0416";break;case 0xA:text=locale==0x409?L"1":L"55";break;case 0xB:text=locale==0x409?L"437":L"850";break;case 0xE:text=locale==0x409?L".":L",";break;case 0xF:text=locale==0x409?L",":L".";break;case 0x10:text=L"3;0";break;case 0x11:text=L"2";break;case 0x12:text=L"1";break;case 0x13:text=L"0123456789";break;case 0x14:text=locale==0x409?L"$":L"R$";break;case 0x15:text=locale==0x409?L"USD":L"BRL";break;case 0x16:text=locale==0x409?L".":L",";break;case 0x17:text=locale==0x409?L",":L".";break;case 0x18:text=L"3;0";break;case 0x19:case 0x1A:text=L"2";break;case 0x1004:text=L"1252";break;case 0x1009:text=L"+";break;case 0x100A:text=L"-";break;case 0x5C:text=locale==0x409?L"en-US":L"pt-BR";break;default:Unsupported("GetLocaleInfoW field");return 0;}
    uint32_t n=0;while(text[n])++n;if(numeric){Dword number=0;for(uint32_t i=0;i<n;++i){if(text[i]<'0'||text[i]>'9')return Fail(87,0);number=number*((type==1||type==9)?16u:10u)+Dword(text[i]-'0');}if(!cap)return 2;if(cap<2)return Fail(122,0);Copy(out,&number,4);return 2;}
    if(!cap)return int(n+1);if(uint32_t(cap)<n+1)return Fail(122,0);for(uint32_t i=0;i<=n;++i)out[i]=uint16_t(text[i]);return int(n+1);}
extern "C" int KbLCMapStringW(Dword locale,Dword flags,const uint16_t* in,int n,uint16_t* out,int cap){if(!KbIsValidLocale(locale,0))return Fail(87,0);return KbLCMapStringEx(nullptr,flags,in,n,out,cap,nullptr,nullptr,0);}
extern "C" int KbCompareStringW(Dword locale,Dword flags,const uint16_t* a,int an,const uint16_t* b,int bn){if(!KbIsValidLocale(locale,0)||(flags&~0x1001u))return Fail(50,0);return KbCompareStringOrdinal(a,an,b,bn,(flags&1)!=0);}
extern "C" Bool KbGetStringTypeW(Dword type,const uint16_t* in,int n,uint16_t* out){if(type!=1||!in||!out||!n||n< -1)return Fail(87,0);if(n==-1){const auto length=Length(in,1<<20);if(length==UINT32_MAX)return Fail(87,0);n=int(length)+1;}if(n>(1<<20))return Fail(87,0);
    for(int i=0;i<n;++i){const auto c=in[i];if(!nls::CaseSupported(c))return Fail(50,0);uint16_t flags=0;if(c<32||c==127)flags|=0x20;if(c==' '||c=='\t')flags|=0x40;if(c==' '||(c>=9&&c<=13))flags|=8;
        if(c>='0'&&c<='9')flags|=4|0x80;else if((c>='a'&&c<='z')||(c>=0xE0&&c<=0xFF&&c!=0xF7))flags|=2|0x100;else if((c>='A'&&c<='Z')||(c>=0xC0&&c<=0xDE&&c!=0xD7))flags|=1|0x100;
        if((c>='a'&&c<='f')||(c>='A'&&c<='F'))flags|=0x80;if(!flags&&c)flags|=0x10;out[i]=flags;}return 1;}
extern "C" Bool KbEnumSystemLocalesW(Bool (*callback)(uint16_t*),Dword flags){if(!callback||(flags&~3u))return Fail(87,0);uint16_t text[]{'0','0','0','0','0','4','1','6',0};callback(text);return 1;}
extern "C" Bool KbHeapQueryInformation(Handle,Dword,void*,uint64_t,uint64_t*){return Unsupported("HeapQueryInformation");}
extern "C" Bool KbHeapValidate(Handle h,Dword flags,const void* p){if(flags)return Fail(87,0);if(!p)return Unsupported("HeapValidate whole heap");return native::RtlSizeHeap(h,0,const_cast<void*>(p))==UINT64_MAX?Fail(87,0):1;}
extern "C" Bool KbHeapSetInformation(Handle,Dword,void*,uint64_t){return Unsupported("HeapSetInformation");}
extern "C" uint64_t KbHeapCompact(Handle,Dword){Unsupported("HeapCompact");return 0;}
extern "C" Bool KbHeapWalk(Handle,void*){return Unsupported("HeapWalk");}
extern "C" void* KbInterlockedFlushSList(void* raw){auto* p=static_cast<volatile int64_t*>(raw);alignas(16) int64_t old[2]{};void* head=nullptr;do{old[0]=p[0];old[1]=p[1];head=reinterpret_cast<void*>(uint64_t(old[1])&~15ull);}while(!_InterlockedCompareExchange128(p,0,int64_t((uint64_t(old[0])+(1ull<<16))&~0xffffull),old));return head;}
extern "C" void* KbInterlockedPushEntrySList(void* raw,void* entry){if((reinterpret_cast<uint64_t>(raw)|reinterpret_cast<uint64_t>(entry))&15)__ud2();auto* p=static_cast<volatile int64_t*>(raw);alignas(16) int64_t old[2]{};void* head=nullptr;
    do{old[0]=p[0];old[1]=p[1];head=reinterpret_cast<void*>(uint64_t(old[1])&~15ull);*static_cast<void**>(entry)=head;}while(!_InterlockedCompareExchange128(p,int64_t(reinterpret_cast<uint64_t>(entry)),int64_t(uint64_t(old[0])+(1ull<<16)+1),old));return head;}
extern "C" void KbInitializeSListHead(void* raw){if(!raw||(reinterpret_cast<uint64_t>(raw)&15))__ud2();static_cast<uint64_t*>(raw)[0]=static_cast<uint64_t*>(raw)[1]=0;}
extern "C" Bool KbDuplicateHandle(Handle source,Handle h,Handle target,Handle* out,Dword access,Bool inherit,Dword options){if(inherit)return Fail(50,0);return Ok(native::NtDuplicateObject(source,h,target,out,access,0,options));}
extern "C" Dword KbGetTempPathW(Dword cap,uint16_t* out){const uint16_t temp[]{'C',':','\\','A','p','p','\\',0};if(cap<8)return 8;if(!out)return Fail(87,Dword(0));Copy(out,temp,sizeof(temp));return 7;}
extern "C" Dword KbGetLogicalDrives(){return 4;}extern "C" Dword KbGetDriveTypeW(const uint16_t* p){return !p||((p[0]=='C'||p[0]=='c')&&p[1]==':'&&p[2]=='\\'&&!p[3])?3u:1u;}
extern "C" Bool KbSetThreadStackGuarantee(Dword* bytes){if(!bytes)return Fail(87,0);if(*bytes)return Fail(50,0);*bytes=0;return 1;}
// These imports can be present without being used by the accepted corpus.
// Their failure is explicit and observable; they never claim an operation ran.
extern "C" Handle KbFindFirstFileExW(const uint16_t*,Dword,void*,Dword,void*,Dword){Unsupported("FindFirstFileExW");return reinterpret_cast<Handle>(UINT64_MAX);}
extern "C" Bool KbFindNextFileW(Handle,void*){return Unsupported("FindNextFileW");}extern "C" Bool KbFindClose(Handle){return Fail(6,0);}
extern "C" Bool KbGetFileInformationByHandle(Handle,void*){return Unsupported("GetFileInformationByHandle");}
extern "C" Bool KbGetFileAttributesExW(const uint16_t*,Dword,void*){return Unsupported("GetFileAttributesExW");}
extern "C" Bool KbGetDiskFreeSpaceW(const uint16_t*,Dword*,Dword*,Dword*,Dword*){return Unsupported("GetDiskFreeSpaceW");}
extern "C" Bool KbSetFileAttributesW(const uint16_t*,Dword){return Unsupported("SetFileAttributesW");}
extern "C" Bool KbSetFileTime(Handle,const FileTime*,const FileTime*,const FileTime*){return Unsupported("SetFileTime");}
extern "C" Bool KbLockFileEx(Handle,Dword,Dword,Dword,Dword,Overlapped*){return Unsupported("LockFileEx");}
extern "C" Bool KbUnlockFileEx(Handle,Dword,Dword,Dword,Overlapped*){return Unsupported("UnlockFileEx");}
extern "C" Bool KbSetEndOfFile(Handle){return Unsupported("SetEndOfFile");}
extern "C" Bool KbPeekNamedPipe(Handle,void*,Dword,Dword*,Dword*,Dword*){return Unsupported("PeekNamedPipe");}
extern "C" Bool KbCreatePipe(Handle*,Handle*,SecurityAttributes*,Dword){return Unsupported("CreatePipe");}
extern "C" int KbGetTimeFormatW(Dword,Dword,const void*,const uint16_t*,uint16_t*,int){return Unsupported("GetTimeFormatW");}
extern "C" int KbGetDateFormatW(Dword,Dword,const void*,const uint16_t*,uint16_t*,int){return Unsupported("GetDateFormatW");}
extern "C" void KbGetLocalTime(void*){CrtUnsupportedExport("GetLocalTime");}
extern "C" Bool KbSetLocalTime(const void*){return Fail(1314,0);}
extern "C" Bool KbSystemTimeToFileTime(const void*,FileTime*){return Unsupported("SystemTimeToFileTime");}
extern "C" Bool KbFileTimeToSystemTime(const FileTime*,void*){return Unsupported("FileTimeToSystemTime");}
extern "C" Dword KbGetTimeZoneInformation(void*){Unsupported("GetTimeZoneInformation");return UINT32_MAX;}
extern "C" Bool KbTzSpecificLocalTimeToSystemTime(const void*,const void*,void*){return Unsupported("TzSpecificLocalTimeToSystemTime");}
extern "C" Bool KbSystemTimeToTzSpecificLocalTime(const void*,const void*,void*){return Unsupported("SystemTimeToTzSpecificLocalTime");}
extern "C" Bool KbMoveFileExW(const uint16_t*,const uint16_t*,Dword){return Unsupported("MoveFileExW");}
