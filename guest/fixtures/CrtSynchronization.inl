// Extra synchronization is added here only after it is exercised by the corpus.
extern "C" __declspec(noreturn) void CrtUnsupportedExport(const char* name){Unsupported(name);win::ExitProcess(0xE0140500);}
// Recursive guest critical sections do not consume a kernel handle per CRT lock.
extern "C" Bool KbInitializeCriticalSectionEx(CriticalSection* cs,Dword spin,Dword flags){if(!cs||(flags&~0x05000000u))return Fail(87,0);*cs={};cs->lockCount=-1;cs->spin=spin;return 1;}
extern "C" Bool KbTryEnterCriticalSection(CriticalSection* cs){if(!cs)return Fail(87,0);const auto tid=int64_t(win::GetCurrentThreadId());auto* owner=reinterpret_cast<volatile int64_t*>(&cs->owner);const auto previous=_InterlockedCompareExchange64(owner,tid,0);if(previous&&previous!=tid)return 0;++cs->recursion;cs->lockCount=cs->recursion-1;return 1;}
extern "C" void KbEnterCriticalSection(CriticalSection* cs){if(!cs)__ud2();while(!KbTryEnterCriticalSection(cs))win::SwitchToThread();}
extern "C" void KbLeaveCriticalSection(CriticalSection* cs){if(!cs||reinterpret_cast<uint64_t>(cs->owner)!=win::GetCurrentThreadId()||cs->recursion<=0)__ud2();cs->lockCount=--cs->recursion-1;if(!cs->recursion)_InterlockedExchange64(reinterpret_cast<volatile int64_t*>(&cs->owner),0);}
extern "C" void KbDeleteCriticalSection(CriticalSection* cs){if(!cs||cs->recursion)__ud2();*cs={};}
extern "C" void KbAcquireSRWLockExclusive(volatile int64_t* lock){if(!lock||(reinterpret_cast<uint64_t>(lock)&7))__ud2();while(_InterlockedCompareExchange64(lock,1,0))win::SwitchToThread();}
extern "C" Bool KbTryAcquireSRWLockExclusive(volatile int64_t* lock){if(!lock||(reinterpret_cast<uint64_t>(lock)&7))__ud2();return !_InterlockedCompareExchange64(lock,1,0);}
extern "C" void KbReleaseSRWLockExclusive(volatile int64_t* lock){if(!lock||_InterlockedExchange64(lock,0)!=1)__ud2();}
extern "C" Bool KbInitOnceExecuteOnce(volatile int64_t* once,Bool (*callback)(void*,void*,void**),void* parameter,void** context){if(!once||!callback||(reinterpret_cast<uint64_t>(once)&7))return Fail(87,0);
    for(;;){const auto state=_InterlockedCompareExchange64(once,1,0);if(state&2){if(context)*context=reinterpret_cast<void*>(uint64_t(state)&~3ull);return 1;}
        if(!state){void* out=nullptr;const bool ok=callback(const_cast<int64_t*>(once),parameter,&out)!=0;if(!ok){_InterlockedExchange64(once,0);return 0;}if(reinterpret_cast<uint64_t>(out)&3){_InterlockedExchange64(once,0);return Fail(87,0);}_InterlockedExchange64(once,int64_t(reinterpret_cast<uint64_t>(out)|2));if(context)*context=out;return 1;}win::SwitchToThread();}}
extern "C" Bool KbSleepConditionVariableSRW(volatile int64_t* condition,volatile int64_t* lock,Dword timeout,Dword flags){if(!condition||!lock||flags||(reinterpret_cast<uint64_t>(condition)&7))return Fail(87,0);
    const auto observed=_InterlockedCompareExchange64(condition,0,0);const auto begin=win::GetTickCount64();KbReleaseSRWLockExclusive(lock);bool signaled=false;
    for(;;){if(_InterlockedCompareExchange64(condition,0,0)!=observed){signaled=true;break;}if(timeout!=UINT32_MAX&&win::GetTickCount64()-begin>=timeout)break;KbSleepEx(1,0);}KbAcquireSRWLockExclusive(lock);return signaled?1:Fail(1460,0);}
// Windows condition variables permit spurious/stolen wakes. Both operations
// invalidate the generation; callers must recheck their predicate under lock.
extern "C" void KbWakeAllConditionVariable(volatile int64_t* condition){if(!condition||(reinterpret_cast<uint64_t>(condition)&7))__ud2();_InterlockedIncrement64(condition);}
extern "C" void KbWakeConditionVariable(volatile int64_t* condition){KbWakeAllConditionVariable(condition);}
extern "C" Handle KbCreateEventExW(SecurityAttributes* a,const uint16_t* name,Dword flags,Dword access){if(flags&~3u||access!=0x1F0003)return Fail(50,static_cast<Handle>(nullptr));return KbCreateEventW(a,(flags&1)!=0,(flags&2)!=0,name);}
extern "C" Handle KbCreateSemaphoreExW(SecurityAttributes* a,int32_t initial,int32_t maximum,const uint16_t* name,Dword flags,Dword access){if(a||name||flags||access!=0x1F0003)return Fail(50,static_cast<Handle>(nullptr));Handle h=nullptr;const auto status=native::NtCreateSemaphore(&h,access,nullptr,initial,maximum);return status?Fail(Error(status),static_cast<Handle>(nullptr)):h;}
extern "C" Dword KbWaitForSingleObjectEx(Handle h,Dword ms,Bool alertable){int64_t ticks=-int64_t(ms)*10000;const auto s=native::NtWaitForSingleObject(h,uint8_t(alertable!=0),ms==UINT32_MAX?nullptr:&ticks);return s<0x80000000?s:Fail(Error(s),UINT32_MAX);}
extern "C" Bool KbDisableThreadLibraryCalls(Handle){return Fail(50,0);}
extern "C" Bool KbAreFileApisANSI(){return 1;}
extern "C" Bool KbGetHandleInformation(Handle h,Dword* flags){if(!flags)return Fail(87,0);if(auto* f=File(h)){*flags=f->inherit?1u:0u;return 1;}return Fail(6,0);}
extern "C" Bool KbSetHandleInformation(Handle h,Dword mask,Dword flags){if((mask|flags)&~1u)return Fail(50,0);if(auto* f=File(h)){if(mask&1)f->inherit=(flags&1)!=0;return 1;}return Fail(6,0);}
extern "C" Dword KbGetCurrentProcessorNumber(){return 0;}
extern "C" void KbFlushProcessWriteBuffers(){_mm_mfence();}
extern "C" Handle KbLocalFree(Handle p){if(!p)return nullptr;return KbHeapFree(heap,0,p)?nullptr:p;}
namespace {Dword LocaleId(const uint16_t* name){if(!name)return 0x416;if(!name[0])return 0x7f;const uint16_t pt[]{'p','t','-','B','R',0},en[]{'e','n','-','U','S',0};if(Length(name,6)!=5)return 0;if(Equal(name,pt,5))return 0x416;if(Equal(name,en,5))return 0x409;return 0;}}
extern "C" int KbGetLocaleInfoEx(const uint16_t* locale,Dword type,uint16_t* out,int cap){const auto id=LocaleId(locale);return id?KbGetLocaleInfoW(id,type,out,cap):Fail(87,0);}
extern "C" int KbCompareStringEx(const uint16_t* locale,Dword flags,const uint16_t* a,int an,const uint16_t* b,int bn,void* version,void* reserved,int64_t sort){const auto id=LocaleId(locale);if(!id||version||reserved||sort)return Fail(87,0);return KbCompareStringW(id,flags,a,an,b,bn);}
extern "C" int KbGetUserDefaultLocaleName(uint16_t* out,int cap){return KbGetLocaleInfoW(0x416,0x5C,out,cap);}
extern "C" int KbGetSystemDefaultLocaleName(uint16_t* out,int cap){return KbGetUserDefaultLocaleName(out,cap);}
extern "C" Dword KbGetSystemDefaultLCID(){return 0x416;}
extern "C" Dword KbLocaleNameToLCID(const uint16_t* name,Dword flags){return flags?Fail(87,Dword(0)):LocaleId(name);}
extern "C" int KbLCIDToLocaleName(Dword locale,uint16_t* out,int cap,Dword flags){return flags?Fail(87,0):KbGetLocaleInfoW(locale,0x5C,out,cap);}
