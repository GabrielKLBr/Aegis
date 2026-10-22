#include "PeTls.h"
#include "guardian/Runtime.h"
#include "PeEnvironmentCommon.h"
using namespace guardian::win;
__declspec(thread) volatile uint32_t environmentTls=0;
extern "C" void TlsCallback(void*,uint32_t reason,void*){if(reason==1)environmentTls=1;}
#define VERIFY(c,n) do{if(!(c))return guardian::envapp::Fail(n);}while(false)
namespace {
using namespace guardian;using namespace envapp;
bool Register(const Info& info){
    Handle key=nullptr;if(!Registry(key,true))return false;
    const auto account=environment::Account(info);const uint32_t seed=uint32_t(info.seed),version=1;
    const char app[]="guardian-docs.exe";
    const bool ok=SetValue(key,"User",account.bytes,account.size+1)&&SetValue(key,"UserId",&info.user,4,4)&&SetValue(key,"Seed",&seed,4,4)&&
        SetValue(key,"NT",&info.ntBuild,4,4)&&SetValue(key,"Version",&version,4,4)&&SetValue(key,"Docs",app,sizeof(app))&&
        SetValue(key,"TxtHandler",app,sizeof(app))&&SetValue(key,"CsvHandler",app,sizeof(app))&&SetValue(key,"CatalogHash",&info.catalogHash,8,11);
    return RegCloseKey(key)==0&&ok;
}
bool Registered(const Info& info){
    Handle key=nullptr;if(!Registry(key,false))return false;
    char user[64]{},app[64]{};uint32_t size=0,uid=0,seed=0,nt=0,version=0;uint64_t hash=0;
    const auto account=environment::Account(info);
    const bool ok=Value(key,"User",user,sizeof(user),size)&&size==account.size+1&&Same(user,account.bytes,size)&&
        Value(key,"Docs",app,sizeof(app),size)&&environment::Equal(app,"guardian-docs.exe")&&
        Value(key,"UserId",&uid,4,size,4)&&size==4&&uid==info.user&&Value(key,"Seed",&seed,4,size,4)&&size==4&&seed==info.seed&&
        Value(key,"NT",&nt,4,size,4)&&size==4&&nt==info.ntBuild&&Value(key,"Version",&version,4,size,4)&&version==1&&
        Value(key,"CatalogHash",&hash,8,size,11)&&size==8&&hash==info.catalogHash;
    return RegCloseKey(key)==0&&ok;
}
bool OpenDocument(const char* path){
    if(!environment::DocumentAllowed(path))return false;
    Handle key=nullptr;if(!Registry(key,false))return false;
    char app[64]{};uint32_t size=0;const auto* association=environment::Equal(path,environment::DocumentPath(1))?"CsvHandler":"TxtHandler";
    const bool associated=Value(key,association,app,sizeof(app),size)&&size==18&&environment::Equal(app,"guardian-docs.exe");
    if(RegCloseKey(key)||!associated){Fail(430);return false;}
    StartupInfoA startup{};startup.cb=sizeof(startup);ProcessInformation child{};char argument[128]{};
    for(uint32_t i=0;path[i];++i)argument[i]=path[i];
    if(!CreateProcessA(app,argument,nullptr,nullptr,0,0,nullptr,nullptr,&startup,&child)){Text error;error.Add("12E LAUNCH ERROR ");error.Number(GetLastError());error.Add("\n");Console(error.bytes);return false;}
    uint32_t code=UINT32_MAX;const bool done=WaitForSingleObject(child.process,UINT32_MAX)==0&&GetExitCodeProcess(child.process,&code)&&code==0;
    if(!done){Text error;error.Add("12E READER EXIT ");error.Number(code);error.Add("\n");Console(error.bytes);}
    const bool closed=CloseHandle(child.thread)&&CloseHandle(child.process);return done&&closed;
}
bool OpenShortcut(){Text shortcut;if(!ReadText("C:\\welcome.gsl",shortcut))return false;const auto expected=Shortcut();return shortcut.size==expected.size&&Same(shortcut.bytes,expected.bytes,expected.size)&&OpenDocument(environment::DocumentPath(0));}
}
extern "C" uint32_t GuardianEntry(){
    using namespace guardian;using namespace envapp;
    Info info{};VERIFY(environmentTls==1&&GuardianEnvironmentInfo(&info)&&environment::Valid(info)&&info.catalogHash,400);
    Text existing;const auto marker=Marker(info);const bool recovered=ReadText("C:\\profile.dat",existing);
    if(!recovered){
        VERIFY(GetLastError()==2,401);
        const auto date=environment::BaseUtc(info);
        for(uint32_t i=0;i<3;++i)VERIFY(WriteText(environment::DocumentPath(i),environment::Document(info,i),date-36000000000ull),402);
        const History history{};VERIFY(Write("C:\\recent.dat",&history,sizeof(history),date),403);
        VERIFY(WriteText("C:\\apps.gac",Catalog(info),date)&&WriteText("C:\\welcome.gsl",Shortcut(),date)&&Register(info),404);
        VERIFY(WriteText("C:\\profile.dat",marker,date,true),405);
    }else VERIFY(existing.size==marker.size&&Same(existing.bytes,marker.bytes,marker.size),406);
    VERIFY(Registered(info),407);
    VERIFY(Console("12E READY\n"),408);
    History history{};uint32_t bytes=0;VERIFY(Read("C:\\recent.dat",reinterpret_cast<uint8_t*>(&history),sizeof(history),bytes)&&bytes==sizeof(history)&&ValidHistory(history),409);
    VERIFY(history.visits<1000000,410);++history.visits;
    for(uint32_t i=0;i<3;++i){
        VERIFY(i==0?OpenShortcut():OpenDocument(environment::DocumentPath(i)),411+i);
        FileTime now{};GetSystemTimeAsFileTime(&now);const uint64_t utc=uint64_t(now.low)|(uint64_t(now.high)<<32);VERIFY(utc>storage::UnixEpoch100ns,414);
        history.events[history.next]={utc,++history.sequence,i,0};history.next=(history.next+1)%12;if(history.count<12)++history.count;
    }
    Text notes;VERIFY(ReadText(environment::DocumentPath(2),notes),415);notes.Add("Visit ");notes.Number(history.visits);notes.Add("\n");VERIFY(WriteText(environment::DocumentPath(2),notes),416);
    VERIFY(Write("C:\\recent.dat",&history,sizeof(history),0,true),417);
    Text done;done.Add("12E DONE visits=");done.Number(history.visits);done.Add(" opens=3\n");VERIFY(Console(done.bytes),418);
    const auto readonly=CreateFileA(environment::DocumentPath(0),0x80000000,3,nullptr,3,0,nullptr);uint32_t written=0;
    VERIFY(readonly!=Invalid()&&!WriteFile(readonly,"x",1,&written,nullptr)&&GetLastError()==5&&CloseHandle(readonly),419);
    const auto invalid=CreateFileA("Z:\\host-private.txt",0x80000000,3,nullptr,3,0,nullptr);VERIFY(invalid==Invalid(),419);
    return 0;
}
