extern "C" Handle KbCreateFileW(const uint16_t* path,Dword access,Dword share,void* security,Dword disposition,Dword attributes,Handle templ){
    if(security||templ||!disposition||disposition>5||(attributes&~0x40000080u))return Fail(87,reinterpret_cast<Handle>(UINT64_MAX));
    uint16_t full[128]{};char narrow[128]{};if(!FullPath(path,full))return Fail(123,reinterpret_cast<Handle>(UINT64_MAX));if(!Ascii(full,narrow))return Fail(50,reinterpret_cast<Handle>(UINT64_MAX));
    native::Unicode text;native::RtlInitUnicodeString(&text,full);native::Attributes a;a.name=reinterpret_cast<uint64_t>(&text);a.attributes=0x40;native::IoStatus io;Handle h=nullptr;
    const uint32_t types[]{0,2,5,1,3,4};const auto s=native::NtCreateFile(&h,access,&a,&io,nullptr,0,share,types[disposition],0x60,nullptr,0);
    if(s)return Fail(s==0xC0000035&&disposition==1?80:Error(s),reinterpret_cast<Handle>(UINT64_MAX));if(!Lock()){native::NtClose(h);return Fail(8,reinterpret_cast<Handle>(UINT64_MAX));}
    FileState* state=nullptr;for(auto& f:files)if(!f.handle){state=&f;break;}if(!state){Unlock();native::NtClose(h);return Fail(8,reinterpret_cast<Handle>(UINT64_MAX));}*state={h,(attributes&0x40000000)!=0};Unlock();
    if(disposition==2||disposition==4)win::SetLastError(io.information==2?0:183);return h;
}
extern "C" Handle KbCreateFileA(const char* path,Dword access,Dword share,void* security,Dword disposition,Dword attributes,Handle templ){uint16_t text[128]{};return FromA(path,text)?KbCreateFileW(text,access,share,security,disposition,attributes,templ):Fail(1113,reinterpret_cast<Handle>(UINT64_MAX));}
extern "C" Bool KbGetFileSizeEx(Handle h,int64_t* out){if(!out)return Fail(87,0);native::IoStatus io;uint64_t info[3]{};const auto s=native::NtQueryInformationFile(h,&io,info,sizeof(info),5);if(s)return Ok(s);*out=int64_t(info[1]);return 1;}
extern "C" Bool KbSetFilePointerEx(Handle h,int64_t distance,int64_t* out,Dword method){win32::Request r;r.op=1;r.args[0]=reinterpret_cast<uint64_t>(h);r.args[1]=uint64_t(distance);r.args[2]=method;const auto s=GuardianWin32Call(&r);if(s)return Ok(s);if(out)*out=int64_t(r.args[3]);return 1;}
extern "C" Bool KbFlushFileBuffers(Handle h){win32::Request r;r.op=2;r.args[0]=reinterpret_cast<uint64_t>(h);return Ok(GuardianWin32Call(&r));}
extern "C" Bool KbCloseHandle(Handle h){if(File(h)&&PendingFile(h))DrainFile(h);const auto s=native::NtClose(h);if(!s&&Lock()){if(auto* f=File(h))*f={};Unlock();}return Ok(s);}
extern "C" Handle KbCreateFileMappingW(Handle file,SecurityAttributes* security,Dword protection,Dword high,Dword low,const uint16_t* name){if(security||name||high)return Fail(50,static_cast<Handle>(nullptr));int64_t size=low;
    if(reinterpret_cast<uint64_t>(file)==UINT64_MAX)file=nullptr;else if(!size&&!KbGetFileSizeEx(file,&size))return nullptr;
    if(!size)return Fail(1006,static_cast<Handle>(nullptr));Handle h=nullptr;const auto s=native::NtCreateSection(&h,0xF001F,nullptr,&size,protection,0x8000000,file);return s?Fail(Error(s),static_cast<Handle>(nullptr)):h;}
extern "C" Handle KbCreateFileMappingA(Handle file,SecurityAttributes* security,Dword protection,Dword high,Dword low,const char* name){if(name)return Fail(50,static_cast<Handle>(nullptr));return KbCreateFileMappingW(file,security,protection,high,low,nullptr);}
extern "C" void* KbMapViewOfFile(Handle h,Dword access,Dword high,Dword low,uint64_t bytes){if(high||(access!=2&&access!=4&&access!=0xF001F))return Fail(50,static_cast<void*>(nullptr));void* base=nullptr;int64_t offset=low;
    const auto s=native::NtMapViewOfSection(h,KbGetCurrentProcess(),&base,0,0,&offset,&bytes,2,0,access==4?2u:4u);return s?Fail(Error(s),static_cast<void*>(nullptr)):base;}
extern "C" Bool KbUnmapViewOfFile(const void* p){return Ok(native::NtUnmapViewOfSection(KbGetCurrentProcess(),const_cast<void*>(p)));}
extern "C" Bool KbFlushViewOfFile(const void* p,uint64_t n){win32::Request r;r.op=4;r.args[0]=reinterpret_cast<uint64_t>(p);r.args[1]=n;return Ok(GuardianWin32Call(&r));}
extern "C" Bool KbCreateDirectoryW(const uint16_t* path,SecurityAttributes* a){if(a)return Fail(50,0);return Ok(PathCall(5,path));}
extern "C" Bool KbCreateDirectoryA(const char* path,SecurityAttributes* a){uint16_t text[128]{};return FromA(path,text)?KbCreateDirectoryW(text,a):Fail(1113,0);}
extern "C" Bool KbDeleteFileW(const uint16_t* path){return Ok(PathCall(7,path));}extern "C" Bool KbDeleteFileA(const char* path){uint16_t text[128]{};return FromA(path,text)?KbDeleteFileW(text):Fail(1113,0);}
extern "C" Bool KbRemoveDirectoryW(const uint16_t* path){return Ok(PathCall(6,path));}extern "C" Bool KbRemoveDirectoryA(const char* path){uint16_t text[128]{};return FromA(path,text)?KbRemoveDirectoryW(text):Fail(1113,0);}
