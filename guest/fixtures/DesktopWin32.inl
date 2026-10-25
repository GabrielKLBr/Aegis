extern "C" void KbInitializeCriticalSection(CriticalSection* section){if(!KbInitializeCriticalSectionEx(section,0,0))win::RaiseException(0xC0000017,0,0,nullptr);}
extern "C" void KbGetStartupInfoA(StartupInfoA* out){KbGetStartupInfoW(out);}
extern "C" Handle KbCreateSemaphoreA(SecurityAttributes* attributes,int32_t initial,int32_t maximum,const char* name){if(name)return Fail(50,static_cast<Handle>(nullptr));return KbCreateSemaphoreExW(attributes,initial,maximum,nullptr,0,0x1F0003);}
extern "C" Handle KbCreateSemaphoreW(SecurityAttributes* attributes,int32_t initial,int32_t maximum,const uint16_t* name){return KbCreateSemaphoreExW(attributes,initial,maximum,name,0,0x1F0003);}
extern "C" Bool KbReleaseSemaphore(Handle h,int32_t amount,int32_t* previous){return Ok(native::NtReleaseSemaphore(h,amount,previous));}
extern "C" Handle KbOpenProcess(Dword access,Bool inherit,Dword pid){if(inherit)return Fail(50,static_cast<Handle>(nullptr));native::Attributes attrs;native::ClientId client{pid,0};Handle out=nullptr;const auto status=native::NtOpenProcess(&out,access,&attrs,&client);return status?Fail(Error(status),static_cast<Handle>(nullptr)):out;}
extern "C" Handle KbOpenThread(Dword access,Bool inherit,Dword tid){if(inherit)return Fail(50,static_cast<Handle>(nullptr));native::Attributes attrs;native::ClientId client{0,tid};Handle out=nullptr;const auto status=native::NtOpenThread(&out,access,&attrs,&client);return status?Fail(Error(status),static_cast<Handle>(nullptr)):out;}
namespace {uint32_t DesktopPolicy(uint32_t op,Handle h,uint64_t& value,uint64_t address=0){win32::Request r;r.op=24;r.args[0]=op;r.args[1]=uint64_t(h);r.args[2]=address?address:value;const auto status=GuardianWin32Call(&r);value=r.args[2];return status;}}
extern "C" Dword KbSuspendThread(Handle thread){uint64_t value=0;const auto status=DesktopPolicy(1,thread,value);return status?Fail(Error(status),UINT32_MAX):Dword(value);}
extern "C" Bool KbSetThreadPriority(Handle thread,int priority){uint64_t value=uint64_t(int64_t(priority));return Ok(DesktopPolicy(2,thread,value));}
extern "C" int KbGetThreadPriority(Handle thread){uint64_t value=0;const auto status=DesktopPolicy(3,thread,value);return status?Fail(Error(status),0x7FFFFFFF):int(int64_t(value));}
extern "C" Bool KbGetThreadContext(Handle thread,exceptions::Context* context){if(!context)return Fail(87,0);uint64_t value=0;return Ok(DesktopPolicy(4,thread,value,uint64_t(context)));}
extern "C" Bool KbSetThreadContext(Handle thread,const exceptions::Context* context){if(!context)return Fail(87,0);uint64_t value=0;return Ok(DesktopPolicy(5,thread,value,uint64_t(context)));}
extern "C" Bool KbGetProcessAffinityMask(Handle process,uint64_t* mask,uint64_t* system){if(!mask||!system)return Fail(87,0);uint64_t value=0;const auto status=DesktopPolicy(8,process,value);if(status)return Ok(status);*mask=*system=value;return 1;}
extern "C" Bool KbSetProcessAffinityMask(Handle process,uint64_t mask){return Ok(DesktopPolicy(9,process,mask));}
extern "C" Bool KbIsDBCSLeadByteEx(Dword page,uint8_t byte){(void)byte;if(page==0||page==1252||page==65001||page==437||page==850)return 0;return Fail(87,0);}
