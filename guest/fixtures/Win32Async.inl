namespace {
struct Work {bool used=false;PoolCallback callback=nullptr;void* context=nullptr;Handle done=nullptr;uint32_t pending=0;};Work works[4]{};
struct Job {uint64_t sequence=0;Work* work=nullptr;Handle file=nullptr,issuer=nullptr;void* buffer=nullptr;Dword bytes=0;Overlapped* overlapped=nullptr;IoCallback completion=nullptr;bool write=false,cancelled=false;};
Job jobs[8]{};uint64_t nextJob=1;bool running=false;Handle activeFile=nullptr;__declspec(thread) Work* callbackWork=nullptr;
bool WorkValid(Work* p){for(auto& w:works)if(&w==p&&w.used)return true;return false;}
uint32_t Transfer(bool write,Handle file,void* data,Dword bytes,const int64_t* position,uint64_t& count){
    count=0;if(!bytes){native::IoStatus io;uint64_t size[3]{};return native::NtQueryInformationFile(file,&io,size,sizeof(size),5);}
    while(count<bytes){
#ifdef GUARDIAN_DESKTOP_RUNTIME
        const uint64_t limit=write?512u:16384u;
#else
        const uint64_t limit=512;
#endif
        const auto chunk=Dword(bytes-count>limit?limit:bytes-count);native::IoStatus io;int64_t offset=position?*position+int64_t(count):0;
        const auto s=write?native::NtWriteFile(file,nullptr,nullptr,nullptr,&io,static_cast<uint8_t*>(data)+count,chunk,position?&offset:nullptr,nullptr):native::NtReadFile(file,nullptr,nullptr,nullptr,&io,static_cast<uint8_t*>(data)+count,chunk,position?&offset:nullptr,nullptr);
        if(s)return s;count+=io.information;if(io.information<chunk)break;}return 0;
}
void Complete(Job& j,uint32_t status,uint64_t bytes){j.overlapped->transferred=bytes;_ReadWriteBarrier();j.overlapped->internal=status;
    if(j.completion){const auto queued=native::NtQueueApcThread(j.issuer,reinterpret_cast<void*>(j.completion),reinterpret_cast<void*>(uint64_t(Error(status))),reinterpret_cast<void*>(bytes),j.overlapped);if(queued){j.overlapped->transferred=0;j.overlapped->internal=queued;}native::NtClose(j.issuer);}
    else if(j.overlapped->event)native::NtSetEvent(j.overlapped->event,nullptr);
}
Dword PoolWorker(void*){for(;;){if(!Lock())return 1;Job* selected=nullptr;for(auto& j:jobs)if(j.sequence&&(!selected||j.sequence<selected->sequence))selected=&j;
    if(!selected){running=false;Unlock();return 0;}Job job=*selected;*selected={};activeFile=job.file;Unlock();
    if(job.work){callbackWork=job.work;job.work->callback(&job,job.work->context,job.work);callbackWork=nullptr;
        if(!Lock())return 2;if(!--job.work->pending)native::NtSetEvent(job.work->done,nullptr);Unlock();
    }else {int64_t offset=int64_t(uint64_t(job.overlapped->offset)|(uint64_t(job.overlapped->offsetHigh)<<32));uint32_t status=0xC0000120;uint64_t count=0;
        if(!job.cancelled)status=Transfer(job.write,job.file,job.buffer,job.bytes,&offset,count);
        if(!status&&!job.write&&job.bytes&&!count)status=0xC0000011;
        Complete(job,status,count);
    }
    if(!Lock())return 3;activeFile=nullptr;Unlock();}}
bool Enqueue(Job job){if(!Lock())return false;Job* slot=nullptr;for(auto& j:jobs)if(!j.sequence){slot=&j;break;}if(!slot||nextJob==UINT64_MAX){Unlock();return false;}
    job.sequence=nextJob++;*slot=job;if(!running){running=true;const auto worker=win::CreateThread(nullptr,0,PoolWorker,nullptr,0,nullptr);if(!worker){running=false;*slot={};Unlock();return false;}native::NtClose(worker);}Unlock();return true;}
bool PendingFile(Handle h){if(!Lock())return true;bool pending=activeFile==h;for(const auto& j:jobs)if(j.sequence&&j.file==h)pending=true;Unlock();return pending;}
void DrainFile(Handle h){while(PendingFile(h))win::Sleep(1);}
Bool FileIo(bool write,Handle file,void* data,Dword bytes,Dword* transferred,Overlapped* ov,IoCallback completion=nullptr){if(transferred)*transferred=0;
    if(!File(file)||bytes>(1u<<20)||(bytes&&!data)||(!ov&&!transferred))return Fail(bytes>(1u<<20)?50:87,0);
    if(!ov){uint64_t count=0;const auto s=Transfer(write,file,data,bytes,nullptr,count);*transferred=Dword(count);return Ok(s);}
    if(!File(file)->overlapped||ov->offsetHigh)return Fail(87,0);Job job;job.file=file;job.buffer=data;job.bytes=bytes;job.overlapped=ov;job.write=write;job.completion=completion;
    if(completion){memory::Information info{};uint64_t returned=0;const auto probe=native::NtQueryVirtualMemory(KbGetCurrentProcess(),reinterpret_cast<void*>(completion),0,&info,sizeof(info),&returned);if(probe||info.protect!=memory::ExecuteRead)return Fail(87,0);
        native::Attributes attrs;native::ClientId id{win::GetCurrentProcessId(),win::GetCurrentThreadId()};const auto s=native::NtOpenThread(&job.issuer,0x10,&attrs,&id);if(s)return Ok(s);}
    else if(ov->event){const auto s=native::NtResetEvent(ov->event,nullptr);if(s)return Ok(s);}
    ov->internal=0x103;ov->transferred=0;if(!Enqueue(job)){ov->internal=native::NoMemory;if(job.issuer)native::NtClose(job.issuer);return Fail(8,0);}return completion?1:Fail(997,0);
}
}
extern "C" Bool KbReadFile(Handle h,void* p,Dword n,Dword* got,void* ov){return FileIo(false,h,p,n,got,static_cast<Overlapped*>(ov));}
extern "C" Bool KbWriteFile(Handle h,const void* p,Dword n,Dword* got,void* ov){if(reinterpret_cast<uint64_t>(h)==runtime::Console)return win::WriteFile(h,p,n,got,ov);return FileIo(true,h,const_cast<void*>(p),n,got,static_cast<Overlapped*>(ov));}
extern "C" Bool KbReadFileEx(Handle h,void* p,Dword n,Overlapped* ov,IoCallback callback){return ov&&callback?FileIo(false,h,p,n,nullptr,ov,callback):Fail(87,0);}
extern "C" Bool KbWriteFileEx(Handle h,const void* p,Dword n,Overlapped* ov,IoCallback callback){return ov&&callback?FileIo(true,h,const_cast<void*>(p),n,nullptr,ov,callback):Fail(87,0);}
extern "C" Bool KbGetOverlappedResult(Handle h,Overlapped* ov,Dword* count,Bool wait){if(!File(h)||!ov||!count)return Fail(87,0);
    while(ov->internal==0x103){if(!wait)return Fail(996,0);if(ov->event){const auto s=native::NtWaitForSingleObject(ov->event,0,nullptr);if(s)return Ok(s);}else win::Sleep(1);}
    *count=Dword(ov->transferred);return Ok(Dword(ov->internal));}
extern "C" Bool KbCancelIoEx(Handle h,Overlapped* ov){if(!File(h))return Fail(6,0);if(!Lock())return Fail(8,0);bool found=false;for(auto& j:jobs)if(j.sequence&&j.file==h&&(!ov||j.overlapped==ov)){j.cancelled=true;found=true;}Unlock();return found?1:Fail(1168,0);}
extern "C" void* KbCreateThreadpoolWork(PoolCallback callback,void* context,void* environment){if(!callback||environment)return Fail(87,static_cast<void*>(nullptr));if(!Lock())return Fail(8,static_cast<void*>(nullptr));Work* selected=nullptr;for(auto& w:works)if(!w.used){selected=&w;break;}if(!selected){Unlock();return Fail(8,static_cast<void*>(nullptr));}
    Handle done=nullptr;const auto s=native::NtCreateEvent(&done,0x1F0003,nullptr,0,1);if(s){Unlock();return Fail(Error(s),static_cast<void*>(nullptr));}*selected={true,callback,context,done,0};Unlock();return selected;}
extern "C" void KbSubmitThreadpoolWork(void* raw){auto* w=static_cast<Work*>(raw);if(!Lock())return;if(!WorkValid(w)||w->pending==UINT32_MAX){Unlock();win::SetLastError(87);return;}native::NtResetEvent(w->done,nullptr);++w->pending;Unlock();Job j;j.work=w;
    if(!Enqueue(j)){if(Lock()){if(!--w->pending)native::NtSetEvent(w->done,nullptr);Unlock();}win::SetLastError(8);}}
extern "C" void KbWaitForThreadpoolWorkCallbacks(void* raw,Bool cancel){auto* w=static_cast<Work*>(raw);if(callbackWork==w){win::SetLastError(50);return;}if(!Lock())return;if(!WorkValid(w)){Unlock();win::SetLastError(87);return;}
    if(cancel)for(auto& j:jobs)if(j.sequence&&j.work==w){j={};--w->pending;}if(!w->pending)native::NtSetEvent(w->done,nullptr);const auto done=w->done;Unlock();native::NtWaitForSingleObject(done,0,nullptr);}
extern "C" void KbCloseThreadpoolWork(void* raw){auto* w=static_cast<Work*>(raw);if(callbackWork==w){win::SetLastError(50);return;}KbWaitForThreadpoolWorkCallbacks(raw,0);if(!Lock())return;if(WorkValid(w)){native::NtClose(w->done);*w={};}else win::SetLastError(87);Unlock();}
