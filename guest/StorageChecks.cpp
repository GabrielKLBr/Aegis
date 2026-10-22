#include "Volume.h"
namespace guardian::storage {
namespace {
struct MemoryDisk {uint8_t bytes[io::BlockBytes]{};int64_t writes=-1,flushes=-1;};
MemoryDisk disk;
bool BlockIo(void* context,io::Op op,uint64_t offset,uint8_t* data) noexcept {
    auto& d=*static_cast<MemoryDisk*>(context);
    if(op==io::Op::Flush){if(d.flushes==0)return false;if(d.flushes>0)--d.flushes;return true;}
    if((offset&511) || offset>=sizeof(d.bytes) || !data)return false;
    if(op==io::Op::Write){if(d.writes==0)return false;if(d.writes>0)--d.writes;}
    for(uint32_t i=0;i<512;++i){if(op==io::Op::Write)d.bytes[offset+i]=data[i];else data[i]=d.bytes[offset+i];}return true;
}
void Text(char* out,const char* text,uint32_t limit) noexcept {for(uint32_t i=0;i<limit-1 && text[i];++i)out[i]=text[i];}
void Make(Command& c,Op op,const char* path="",const char* other="") noexcept {c={};c.op=op;Text(c.path,path,PathBytes);Text(c.other,other,PathBytes);}
uint32_t Run(Volume& v,uint32_t owner,const processes::Token& t,Command& c) noexcept {c.status=c.transferred=0;return v.Execute(owner,t,c);}
}
uint64_t StorageChecks(Volume& v,Volume& reopened,threads::Scheduler& scheduler,void (*check)(bool,uint32_t) noexcept) noexcept {
    uint32_t n=0;
#define VERIFY(x) check((x),++n)
    for(auto& b:disk.bytes)b=0;disk.writes=disk.flushes=-1;v.Configure(&disk,BlockIo,scheduler);reopened.Configure(&disk,BlockIo,scheduler);
    processes::Token t{};t.user=100;t.primaryGroup=10;t.integrity=2;auto other=t;other.user=200;auto low=t;low.integrity=1;
    VERIFY(v.Mount()==Success && !v.Sequence());Command c{};Make(c,Op::Mkdir,"C:\\Data");VERIFY(Run(v,1,t,c)==Success);
    Make(c,Op::Open,"c:\\data\\Example.bin");c.access=All;c.disposition=1;VERIFY(Run(v,1,t,c)==Success);const auto handle=c.handle;
    Make(c,Op::Write);c.handle=handle;c.length=4;c.data[0]=42;VERIFY(Run(v,1,t,c)==Success && c.transferred==4);
    Make(c,Op::Read);c.handle=handle;c.length=512;VERIFY(Run(v,1,t,c)==Success && c.transferred==4 && c.data[0]==42);
    VERIFY(Run(v,2,t,c)==InvalidHandle);Make(c,Op::Write);c.handle=handle;c.offset=511;c.length=2;VERIFY(Run(v,1,t,c)==NoSpace);
    Make(c,Op::Open,"C:\\DATA\\EXAMPLE.BIN");c.access=Read;c.share=1;VERIFY(Run(v,2,t,c)==Sharing);
    Make(c,Op::Close);c.handle=handle;VERIFY(Run(v,1,t,c)==Success && Run(v,1,t,c)==InvalidHandle);
    Make(c,Op::Open,"C:\\Data\\Example.bin");c.access=Read;c.share=1;VERIFY(Run(v,1,t,c)==Success);const auto readOnly=c.handle;
    Make(c,Op::Open,"C:\\Data\\Example.bin");c.access=Write;VERIFY(Run(v,2,t,c)==Sharing);VERIFY(Run(v,2,other,c)==Denied);VERIFY(Run(v,2,low,c)==Denied);
    Make(c,Op::Write);c.handle=readOnly;c.length=1;VERIFY(Run(v,1,t,c)==Denied);
    Make(c,Op::Unlink,"C:\\Data\\Example.bin");VERIFY(Run(v,1,t,c)==Sharing);
    Make(c,Op::QueryAcl,"C:\\Data\\Example.bin");c.length=512;VERIFY(Run(v,1,t,c)==Success && c.transferred==sizeof(processes::Security));
    auto acl=processes::Manager::DefaultSecurity(t,processes::Kind::Process);auto empty=acl;empty.aceCount=0;
    Make(c,Op::SetAcl,"C:\\Data\\Example.bin");c.length=sizeof(empty);const auto* raw=reinterpret_cast<const uint8_t*>(&empty);for(uint32_t i=0;i<c.length;++i)c.data[i]=raw[i];VERIFY(Run(v,1,t,c)==Success);
    Make(c,Op::Open,"C:\\Data\\Example.bin");c.access=Read;VERIFY(Run(v,2,t,c)==Denied);
    Make(c,Op::Read);c.handle=readOnly;c.length=4;VERIFY(Run(v,1,t,c)==Success && c.data[0]==42);
    Make(c,Op::SetAcl,"C:\\Data\\Example.bin");c.length=sizeof(acl);raw=reinterpret_cast<const uint8_t*>(&acl);for(uint32_t i=0;i<c.length;++i)c.data[i]=raw[i];VERIFY(Run(v,1,t,c)==Success);
    Make(c,Op::Close);c.handle=readOnly;VERIFY(Run(v,1,t,c)==Success);
    Make(c,Op::Open,"C:\\Data\\Example.bin");c.access=All;VERIFY(Run(v,1,t,c)==Success);const auto original=c.handle;
    Make(c,Op::Link,"C:\\Data\\Example.bin","C:\\Alias");VERIFY(Run(v,1,t,c)==Success);
    Make(c,Op::Unlink,"C:\\Data\\Example.bin");VERIFY(Run(v,1,t,c)==Success);
    Make(c,Op::Open,"C:\\Data\\Example.bin");c.access=Read;VERIFY(Run(v,1,t,c)==NotFound);
    Make(c,Op::Read);c.handle=original;c.length=4;VERIFY(Run(v,1,t,c)==Success && c.data[0]==42);
    Make(c,Op::Close);c.handle=original;VERIFY(Run(v,1,t,c)==Success);
    Make(c,Op::Open,"C:\\Alias");c.access=All;VERIFY(Run(v,1,t,c)==Success);const auto alias=c.handle;
    Make(c,Op::Truncate);c.handle=alias;c.offset=1;VERIFY(Run(v,1,t,c)==Success);c.offset=8;VERIFY(Run(v,1,t,c)==Success);
    Make(c,Op::Read);c.handle=alias;c.length=8;VERIFY(Run(v,1,t,c)==Success && c.data[0]==42 && !c.data[1] && !c.data[7]);
    Make(c,Op::Close);c.handle=alias;VERIFY(Run(v,1,t,c)==Success);
    Make(c,Op::Mkdir,"C:\\Data\\Sub");VERIFY(Run(v,1,t,c)==Success);
    Make(c,Op::Rename,"C:\\Data","C:\\Data\\Sub\\Cycle");VERIFY(Run(v,1,t,c)==Invalid);
    Make(c,Op::Unlink,"C:\\Data");VERIFY(Run(v,1,t,c)==NotEmpty);
    Make(c,Op::Enumerate,"C:\\Data");VERIFY(Run(v,1,t,c)==Success && c.type==static_cast<uint32_t>(Kind::Directory));
    const char* badPaths[]{"C:\\..\\Host","C:\\Data\\bad.","C:\\Data\\bad ","C:\\Data\\file:stream","D:\\Outside","\\\\server\\share","\\\\?\\C:\\Host","C:/Data/x","C:\\Data\\\\x","HKU\\100"};
    for(const auto* path:badPaths){Make(c,Op::Open,path);c.access=Read;VERIFY(Run(v,1,t,c)==Invalid);}
    Make(c,Op::RegCreate,"HKLM\\Software\\Guardian");VERIFY(Run(v,1,t,c)==Success);
    Make(c,Op::RegSet,"HKLM\\Software\\Guardian");Text(c.name,"Counter",NameBytes);c.type=4;c.length=4;c.data[0]=7;VERIFY(Run(v,1,t,c)==Success);
    Make(c,Op::RegGet,"hklm\\software\\guardian");Text(c.name,"counter",NameBytes);c.length=2;VERIFY(Run(v,1,t,c)==Small && c.transferred==4);c.length=512;VERIFY(Run(v,1,t,c)==Success && c.type==4 && c.data[0]==7);
    Make(c,Op::RegSet,"HKLM\\Software\\Guardian");c.type=4;c.length=3;VERIFY(Run(v,1,t,c)==Invalid);
    const uint32_t types[]{0,1,2,3,7,11};for(auto type:types){Make(c,Op::RegSet,"HKLM\\Software\\Guardian");c.type=type;c.length=type==11?8u:4u;VERIFY(Run(v,1,t,c)==Success);}
    Make(c,Op::RegCreate,"HKLM\\Software\\Guardian\\Counter");VERIFY(Run(v,1,t,c)==Success);
    Make(c,Op::RegEnum,"HKLM\\Software\\Guardian");VERIFY(Run(v,1,t,c)==Success);c.flags=1;VERIFY(Run(v,1,t,c)==Success);
    Make(c,Op::RegWatch,"HKLM\\Software");c.flags=1;c.access=4;VERIFY(Run(v,1,t,c)==Success);const auto watch=static_cast<uint32_t>(c.watch);
    const auto tid=scheduler.CreateThread();VERIFY(tid && v.Wait(1,watch,tid)==threads::Pending);
    Make(c,Op::RegSet,"HKLM\\Software\\Guardian");Text(c.name,"Counter",NameBytes);c.type=4;c.length=4;c.data[0]=9;VERIFY(Run(v,1,t,c)==Success && scheduler.Get(tid)->state==threads::State::Ready);
    Make(c,Op::RegPoll);c.watch=watch;VERIFY(Run(v,2,t,c)==InvalidHandle);VERIFY(Run(v,1,t,c)==Success && c.transferred==4);VERIFY(Run(v,1,t,c)==InvalidHandle);
    Make(c,Op::RegDeleteKey,"HKLM\\Software\\Guardian");VERIFY(Run(v,1,t,c)==NotEmpty);
    Make(c,Op::RegDeleteKey,"HKLM\\Software\\Guardian\\Counter");VERIFY(Run(v,1,t,c)==Success);
    Make(c,Op::RegDeleteValue,"HKLM\\Software\\Guardian");Text(c.name,"Counter",NameBytes);VERIFY(Run(v,1,t,c)==Success);
    Make(c,Op::RegCreate,"HKCU\\Settings");VERIFY(Run(v,2,other,c)==Success);
    Make(c,Op::RegSet,"HKCU\\Settings");c.type=4;c.length=4;c.data[0]=23;VERIFY(Run(v,2,other,c)==Success);
    Make(c,Op::RegGet,"HKCU\\Settings");c.length=512;VERIFY(Run(v,1,t,c)==NotFound);VERIFY(Run(v,2,other,c)==Success && c.data[0]==23);
    Make(c,Op::Open,"C:\\Alias");c.access=Read;
    for(uint32_t i=0;i<PerOwnerHandles;++i)VERIFY(Run(v,1,t,c)==Success);VERIFY(Run(v,1,t,c)==Quota);v.Exit(1);VERIFY(!v.Handles());
    VERIFY(scheduler.Terminate(tid,0)==threads::Success && scheduler.Reap(tid));
    VERIFY(v.Flush()==Success && v.Sequence()==1);
    Make(c,Op::Open,"C:\\Alias");c.access=Write;VERIFY(Run(v,1,t,c)==Success);const auto dirtyHandle=c.handle;Make(c,Op::Write);c.handle=dirtyHandle;c.length=1;c.data[0]=99;VERIFY(Run(v,1,t,c)==Success);v.Exit(1);
    for(int64_t prefix=0;prefix<47;++prefix){disk.writes=prefix;VERIFY(v.Flush()==IoError);disk.writes=-1;VERIFY(reopened.Mount()==Success && reopened.Sequence()==1);
        Make(c,Op::Open,"C:\\Alias");c.access=Read;VERIFY(Run(reopened,1,t,c)==Success);const auto h=c.handle;Make(c,Op::Read);c.handle=h;c.length=1;VERIFY(Run(reopened,1,t,c)==Success && c.data[0]==42);reopened.Exit(1);VERIFY(reopened.Unmount()==Success);}
    disk.flushes=1;VERIFY(v.Flush()==IoError);disk.flushes=-1;VERIFY(reopened.Mount()==Success && reopened.Sequence()==2);reopened.Unmount();
    VERIFY(v.Flush()==Success && v.Sequence()==2);VERIFY(reopened.Mount()==Success && reopened.Sequence()==2);reopened.Unmount();
    auto* latest=reinterpret_cast<Image*>(disk.bytes+SecondSlot);latest->nodes[0].reserved=1;VERIFY(!Volume::Validate(*latest));VERIFY(reopened.Mount()==Success && reopened.Sequence()==1);reopened.Unmount();latest->nodes[0].reserved=0;
    // Validate structure even when an attacker recomputes the non-secret checksum.
    latest->nodes[0].parent=latest->nodes[0].id;latest->header.checksum=Hash(latest,sizeof(*latest),offsetof(Header,checksum),8);VERIFY(!Volume::Validate(*latest));latest->nodes[0].parent=0;
    latest->nodes[0].flags=1;latest->header.checksum=Hash(latest,sizeof(*latest),offsetof(Header,checksum),8);VERIFY(!Volume::Validate(*latest));latest->nodes[0].flags=0;latest->header.checksum=Hash(latest,sizeof(*latest),offsetof(Header,checksum),8);
    VERIFY(v.Unmount()==Success);
    for(auto& b:disk.bytes)b=0;VERIFY(v.Mount()==Success);
    for(uint32_t i=0;i<128;++i){Make(c,Op::Open,"C:\\Cycle");c.access=Read|Write|Delete;c.disposition=1;VERIFY(Run(v,1,t,c)==Success);const auto old=c.handle;Make(c,Op::Unlink,"C:\\Cycle");VERIFY(Run(v,1,t,c)==Success);v.Exit(1);Make(c,Op::Read);c.handle=old;c.length=1;VERIFY(Run(v,1,t,c)==InvalidHandle && !v.Handles() && !v.Watches());}
    Make(c,Op::Open,"C:\\FullFile");c.access=All;c.disposition=1;VERIFY(Run(v,1,t,c)==Success);v.Exit(1);
    Make(c,Op::Open,"C:\\FullFile");c.access=Read;for(uint32_t owner=1;owner<=4;++owner)for(uint32_t at=0;at<PerOwnerHandles;++at)VERIFY(Run(v,owner,t,c)==Success);
    VERIFY(Run(v,5,t,c)==Quota);for(uint32_t owner=1;owner<=4;++owner)v.Exit(owner);VERIFY(!v.Handles());
    const char digits[]="0123456789abcdef";uint32_t filled=0;
    for(uint32_t at=0;at<MaxNodes;++at){Make(c,Op::Open,"C:\\n00");c.path[4]=digits[at>>4];c.path[5]=digits[at&15];c.access=All;c.disposition=1;const auto result=Run(v,1,t,c);if(result==NoSpace)break;VERIFY(result==Success);++filled;v.Exit(1);}
    VERIFY(filled==MaxNodes-4);Make(c,Op::Unlink,"C:\\FullFile");VERIFY(Run(v,1,t,c)==Success);
    Make(c,Op::RegCreate,"HKLM\\Incomplete\\Nested");VERIFY(Run(v,1,t,c)==NoSpace);Make(c,Op::RegEnum,"HKLM");VERIFY(Run(v,1,t,c)==NotFound);
    VERIFY(v.Flush()==Success && v.Unmount()==Success && !v.Handles() && !v.Watches());
#undef VERIFY
    return n;
}
}
