extern "C" Dword KbGetEnvironmentVariableW(const uint16_t* name,uint16_t* out,Dword cap){const auto n=Length(name);if(!n||n==UINT32_MAX)return Fail(87,Dword(0));if(!Lock())return Fail(8,Dword(0));
    for(uint32_t i=0;i<n;++i)if(!nls::CaseSupported(name[i])){Unlock();return Fail(50,Dword(0));}
    const auto* env=reinterpret_cast<const uint16_t*>(Parameters()->environment);Dword result=0;bool found=false;
    for(uint32_t at=0;at<win32::EnvironmentCharacters&&env[at];){uint32_t end=at,equal=at;while(end<win32::EnvironmentCharacters&&env[end]){if(env[end]=='='&&equal==at)equal=end;++end;}
        if(end==win32::EnvironmentCharacters)break;if(equal-at==n&&Equal(env+at,name,n)){found=true;const auto count=end-equal-1;if(cap<=count)result=count+1;else if(out){Copy(out,env+equal+1,count*2);out[count]=0;result=count;}else win::SetLastError(87);break;}at=end+1;}
    Unlock();if(!found)win::SetLastError(203);return result;}
extern "C" Dword KbGetEnvironmentVariableA(const char* name,char* out,Dword cap){uint16_t wide[128]{},value[512]{};if(!FromA(name,wide))return Fail(87,Dword(0));const auto n=KbGetEnvironmentVariableW(wide,value,512);if(!n){if(out&&cap)out[0]=0;return 0;}
    Dword e=0;const auto required=nls::Wide(1252,0,value,-1,nullptr,0,nullptr,nullptr,e);if(cap<uint32_t(required))return Dword(required);if(!out)return Fail(87,Dword(0));nls::Wide(1252,0,value,-1,out,int(cap),nullptr,nullptr,e);return Dword(required-1);}
extern "C" Bool KbSetEnvironmentVariableW(const uint16_t* name,const uint16_t* value){const auto n=Length(name),vn=value?Length(value,512):0;if(!n||n==UINT32_MAX||vn==UINT32_MAX)return Fail(87,0);for(uint32_t i=0;i<n;++i){if(name[i]=='=')return Fail(87,0);if(!nls::CaseSupported(name[i]))return Fail(50,0);}
    if(!Lock())return Fail(8,0);auto* env=reinterpret_cast<uint16_t*>(Parameters()->environment);uint16_t next[win32::EnvironmentCharacters]{};uint32_t used=0;bool found=false;
    for(uint32_t at=0;at<win32::EnvironmentCharacters&&env[at];){uint32_t end=at,equal=at;while(end<win32::EnvironmentCharacters&&env[end]){if(env[end]=='='&&equal==at)equal=end;++end;}
        if(end==win32::EnvironmentCharacters){Unlock();return Fail(87,0);}const bool match=equal-at==n&&Equal(env+at,name,n);found=found||match;
        if(!match){if(end-at+1>win32::EnvironmentCharacters-used-1){Unlock();return Fail(203,0);}for(uint32_t i=at;i<=end;++i)next[used++]=env[i];}at=end+1;}
    if(value){if(n+vn+2>win32::EnvironmentCharacters-used-1){Unlock();return Fail(203,0);}for(uint32_t i=0;i<n;++i)next[used++]=name[i];next[used++]='=';for(uint32_t i=0;i<vn;++i)next[used++]=value[i];++used;}
    if(!value&&!found){Unlock();return Fail(203,0);}Copy(env,next,sizeof(next));Unlock();return 1;}
extern "C" Bool KbSetEnvironmentVariableA(const char* name,const char* value){uint16_t wide[128]{},data[512]{};if(!FromA(name,wide)||(value&&!FromA(value,data,512)))return Fail(1113,0);return KbSetEnvironmentVariableW(wide,value?data:nullptr);}
extern "C" uint16_t* KbGetEnvironmentStringsW(){if(!Lock())return Fail(8,static_cast<uint16_t*>(nullptr));const auto* env=reinterpret_cast<const uint16_t*>(Parameters()->environment);uint32_t count=1;
    while(count<win32::EnvironmentCharacters&&(env[count]||env[count-1]))++count;if(count==win32::EnvironmentCharacters){Unlock();return Fail(87,static_cast<uint16_t*>(nullptr));}++count;
    auto* result=static_cast<uint16_t*>(KbHeapAlloc(heap,0,count*2));if(result)Copy(result,env,count*2);Unlock();return result;}
extern "C" char* KbGetEnvironmentStrings(){const auto* env=KbGetEnvironmentStringsW();if(!env)return nullptr;uint32_t n=1;while(env[n]||env[n-1])++n;++n;Dword e=0;
    const auto bytes=nls::Wide(1252,0,env,int(n),nullptr,0,nullptr,nullptr,e);auto* out=static_cast<char*>(KbHeapAlloc(heap,0,uint64_t(bytes)));if(out)nls::Wide(1252,0,env,int(n),out,bytes,nullptr,nullptr,e);KbHeapFree(heap,0,const_cast<uint16_t*>(env));return out;}
extern "C" Bool KbFreeEnvironmentStringsW(uint16_t* p){return KbHeapFree(heap,0,p);}extern "C" Bool KbFreeEnvironmentStringsA(char* p){return KbHeapFree(heap,0,p);}
extern "C" Bool KbCreateProcessA(const char* application,char* command,void* ps,void* ts,Bool inherit,Dword flags,void* env,const char* directory,StartupInfoA* startup,ProcessInformation* info){
    if(info)*info={};if(ps||ts||inherit||env||directory||(flags&~0x404u))return Fail(50,0);char name[128]{},arguments[128]{};uint16_t wide[128]{},full[128]{};
    if(application){uint32_t i=0;for(;i<127&&application[i];++i)name[i]=application[i];if(application[i])return Fail(206,0);}
    else {if(!command||!command[0])return Fail(87,0);uint32_t at=command[0]=='"'?1u:0u,n=0;const char end=at?'"':' ';while(command[at]&&command[at]!=end){if(n==127)return Fail(206,0);name[n++]=command[at++];}if(end=='"'&&command[at]!='"')return Fail(87,0);}
    if(!FromA(name,wide)||!FullPath(wide,full)||!Ascii(full,name))return Fail(123,0);const auto* source=command?command:name;uint32_t n=0;for(;n<127&&source[n];++n)arguments[n]=source[n];if(source[n])return Fail(206,0);
    return win::CreateProcessA(name,arguments,nullptr,nullptr,0,flags&4,nullptr,nullptr,startup,info);
}
extern "C" Bool KbCreateProcessW(const uint16_t* application,uint16_t* command,void* ps,void* ts,Bool inherit,Dword flags,void* env,const uint16_t* directory,StartupInfoA* startup,ProcessInformation* info){char name[128]{},arguments[128]{};
    if((application&&!Ascii(application,name))||(command&&!Ascii(command,arguments))){if(info)*info={};return Fail(50,0);}if(directory)return Fail(50,0);
    return KbCreateProcessA(application?name:nullptr,command?arguments:nullptr,ps,ts,inherit,flags,env,nullptr,startup,info);}
