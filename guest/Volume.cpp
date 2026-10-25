#include "Volume.h"
#include "guardian/Telemetry.h"
namespace guardian::storage {
namespace {
processes::Token Admin() noexcept {processes::Token t{};t.user=100;t.primaryGroup=10;t.integrity=2;return t;}
bool Zero(const void* p,uint32_t n) noexcept {const auto* b=static_cast<const uint8_t*>(p);for(uint32_t i=0;i<n;++i)if(b[i])return false;return true;}
uint64_t Digest(const Image& i) noexcept {return Hash(&i,sizeof(i),offsetof(Image,header)+offsetof(Header,checksum),sizeof(i.header.checksum));}
constexpr uint64_t FormatMagic=0x31544D46415647ull;
struct FormatIntent {uint64_t magic;uint32_t version,bytes;uint64_t checksum;uint8_t reserved[488];};
static_assert(sizeof(FormatIntent)==512);
bool ValidIntent(const FormatIntent& intent) noexcept {return intent.magic==FormatMagic && intent.version==1 && intent.bytes==sizeof(intent) && Zero(intent.reserved,sizeof(intent.reserved)) && intent.checksum==Hash(&intent,sizeof(intent),offsetof(FormatIntent,checksum),8);}
uint32_t TimeValue(const Header& header,uint32_t slot,uint32_t field) noexcept {
    const auto* b=reinterpret_cast<const uint8_t*>(header.reserved+1)+(slot*3+field)*4;
    uint32_t value=0;for(uint32_t i=0;i<4;++i)value|=uint32_t(b[i])<<(i*8);return value;
}
void TimeValue(Header& header,uint32_t slot,uint32_t field,uint32_t value) noexcept {
    auto* b=reinterpret_cast<uint8_t*>(header.reserved+1)+(slot*3+field)*4;
    for(uint32_t i=0;i<4;++i)b[i]=uint8_t(value>>(i*8));
}
void Reset(Image& image) noexcept {auto* bytes=reinterpret_cast<volatile uint8_t*>(&image);for(uint32_t i=0;i<sizeof(image);++i)bytes[i]=0;for(auto& n:image.nodes){n.security.integrity=1;n.security.daclPresent=1;n.security.noWriteUp=1;}}
}
void Volume::ClearTimes(uint32_t slot) noexcept {
    if(image_.header.reserved[0]!=TimesMagic || slot>=MaxNodes)return;
    for(uint32_t i=0;i<3;++i)TimeValue(image_.header,slot,i,0);
}
void Volume::Touch(Node& node,bool write,bool created) noexcept {
    if(!clock_ || image_.header.reserved[0]!=TimesMagic)return;
    uint32_t now=0;const auto raw=clock_(context_);if(raw<=UnixEpoch100ns || !FileSecond(raw-(raw-UnixEpoch100ns)%10000000,now))return;
    const auto slot=uint32_t(&node-image_.nodes);
    if(created)TimeValue(image_.header,slot,0,now);
    TimeValue(image_.header,slot,1,now);if(write)TimeValue(image_.header,slot,2,now);dirty_=true;
}
uint32_t Volume::Times(uint32_t owner,Command& c) noexcept {
    auto* h=Find(owner,c.handle);if(!h)return InvalidHandle;auto* node=Get(h->node);if(!node || node->kind!=Kind::File)return TypeMismatch;
    if(c.flags>7 || c.offset || (c.op==Op::FileTimesRead?c.flags!=0:c.length!=sizeof(FileTimes)))return Invalid;
    if(!(h->access&(c.op==Op::FileTimesRead?Read:Write)))return Denied;
    const auto slot=uint32_t(node-image_.nodes);FileTimes times{};uint32_t values[3]{};
    if(image_.header.reserved[0]==TimesMagic){for(uint32_t i=0;i<3;++i)values[i]=TimeValue(image_.header,slot,i);}
    if(c.op==Op::FileTimesRead){if(c.length<sizeof(times))return Small;times={FileUtc(values[0]),FileUtc(values[1]),FileUtc(values[2])};const auto* b=reinterpret_cast<const uint8_t*>(&times);for(uint32_t i=0;i<sizeof(times);++i)c.data[i]=b[i];c.transferred=sizeof(times);return Success;}
    auto* b=reinterpret_cast<uint8_t*>(&times);for(uint32_t i=0;i<sizeof(times);++i)b[i]=c.data[i];const uint64_t fields[3]={times.creation,times.access,times.write};
    for(uint32_t i=0;i<3;++i)if((c.flags&(1u<<i)) && !FileSecond(fields[i],values[i]))return Unsupported;
    if(!c.flags)return Success;
    image_.header.reserved[0]=TimesMagic;for(uint32_t i=0;i<3;++i)TimeValue(image_.header,slot,i,values[i]);dirty_=true;return Success;
}
bool Volume::Equal(const char* a,const char* b) noexcept {for(uint32_t i=0;i<NameBytes;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}return false;}
bool Volume::Name(const char* source,char* out,bool empty) noexcept {
    uint32_t i=0;for(;i<NameBytes-1 && source[i];++i){auto c=source[i];if(c<' ' || c>'~' || c=='\\' || c=='/' || c==':' || c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|')return false;out[i]=c>='A' && c<='Z'?static_cast<char>(c+32):c;}
    if(source[i] || (!i && !empty) || (i && (out[i-1]=='.' || out[i-1]==' ')) || (i==1 && out[0]=='.') || (i==2 && out[0]=='.' && out[1]=='.'))return false;out[i]=0;return true;
}
bool Volume::Parse(const char* text,Path& p) noexcept {
    if(!text)return false;uint32_t size=0;for(;size<PathBytes && text[size];++size){}if(size==PathBytes)return false;
    uint32_t at=0;
    if(size>=3 && (text[0]=='C' || text[0]=='c') && text[1]==':' && text[2]=='\\'){p.root=1;at=3;}
    else if(size>=4 && (text[0]=='H'||text[0]=='h') && (text[1]=='K'||text[1]=='k') && (text[2]=='L'||text[2]=='l') && (text[3]=='M'||text[3]=='m')){p.root=2;at=4;}
    else if(size>=4 && (text[0]=='H'||text[0]=='h') && (text[1]=='K'||text[1]=='k') && (text[2]=='C'||text[2]=='c') && (text[3]=='U'||text[3]=='u')){p.root=3;at=4;}
    else return false;
    if(p.root!=1 && at<size){if(text[at++]!='\\')return false;if(at==size)return false;}
    while(at<size){if(p.count==Depth)return false;char part[NameBytes]{};uint32_t n=0;while(at<size && text[at]!='\\'){if(n>=NameBytes-1)return false;part[n++]=text[at++];}if(!Name(part,p.parts[p.count++]))return false;if(at<size && ++at==size)return false;}return true;
}
bool Volume::Value(ValueType type,const uint8_t* data,uint32_t bytes) noexcept {
    if(bytes>DataBytes)return false;
    if(type==ValueType::None || type==ValueType::Binary)return true;
    if(type==ValueType::Dword)return bytes==4;if(type==ValueType::Qword)return bytes==8;
    if(type!=ValueType::String && type!=ValueType::ExpandString && type!=ValueType::MultiString)return false;
    if((bytes&1) || bytes<2 || data[bytes-1] || data[bytes-2])return false;
    if(type==ValueType::MultiString && (bytes<4 || data[bytes-3] || data[bytes-4]))return false;
    return true;
}
bool Volume::Access(const processes::Token& t,const Node& n,uint32_t rights) noexcept {return processes::Manager::Access(t,n.security,processes::Kind::Process,rights);}
Node* Volume::Get(uint32_t id) noexcept {const auto slot=id&255u;if(!slot || slot>MaxNodes)return nullptr;auto& n=image_.nodes[slot-1];return n.kind!=Kind::Empty && n.id==id?&n:nullptr;}
Node* Volume::Child(uint32_t parent,const char* name,Kind kind) noexcept {for(auto& n:image_.nodes)if(n.kind!=Kind::Empty && !(n.flags&1) && n.parent==parent && Equal(n.name,name) && (kind==Kind::Empty || n.kind==kind))return &n;return nullptr;}
bool Volume::ConfigureQuota(uint64_t bytes) noexcept {QuotaInfo q{};q.allocationLimit=bytes;if(!ValidQuota(q)||mounted_)return false;allocationLimit_=bytes;return true;}
bool Volume::BackingReady(uint64_t records) const noexcept {if(!backing_)return true;BackingSpace s{};return backing_(context_,s)&&records<=s.available/544;}
uint32_t Volume::BlockError() const noexcept {return BackingReady(1)?IoError:NoSpace;}
Space Volume::QuerySpace() const noexcept {Space s{};s.allocationLimit=allocationLimit_;for(const auto& n:image_.nodes)if(n.kind!=Kind::Empty)s.allocated+=DataBytes;s.free=s.allocated<s.capacity?s.capacity-s.allocated:0;const auto quota=s.allocated<allocationLimit_?allocationLimit_-s.allocated:0;s.available=s.free<quota?s.free:quota;if(!BackingReady(sizeof(Image)/512+1))s.available=0;return s;}
Node* Volume::New(const processes::Token& t,uint32_t parent,Kind kind,const char* name) noexcept {
    if(QuerySpace().available<DataBytes)return nullptr;
    for(uint32_t i=0;i<MaxNodes;++i)if(image_.nodes[i].kind==Kind::Empty && image_.nodes[i].generation<0xFFFFFE){auto& n=image_.nodes[i];const auto g=n.generation+1;n={};n.generation=g;n.id=(g<<8)|(i+1);n.parent=parent;n.kind=kind;ClearTimes(i);n.security=processes::Manager::DefaultSecurity(t,processes::Kind::Process);for(uint32_t a=0;a<NameBytes-1 && name[a];++a)n.name[a]=name[a];if(kind==Kind::File)Touch(n,true,true);dirty_=true;return &n;}return nullptr;
}
Node* Volume::Root(const processes::Token& t,uint32_t root,bool create) noexcept {
    if(root==1)return &image_.nodes[0];if(root==2)return &image_.nodes[1];
    for(auto& n:image_.nodes)if(n.kind==Kind::Key && !n.parent && Equal(n.name,"hkcu") && n.security.owner==t.user)return &n;
    return create?New(t,0,Kind::Key,"hkcu"):nullptr;
}
uint32_t Volume::Resolve(const processes::Token& t,const char* text,Node*& node,bool parent,char* leaf,bool follow) noexcept {
    if(!processes::Manager::ValidToken(t))return Denied;Path p{};if(!Parse(text,p) || (parent && !p.count))return Invalid;node=Root(t,p.root);if(!node)return NotFound;
    const auto count=parent?p.count-1:p.count;
    for(uint32_t i=0;i<count;++i){if((node->kind!=Kind::Directory && node->kind!=Kind::Key) || !Access(t,*node,List))return Denied;node=Child(node->id,p.parts[i],node->kind==Kind::Key?Kind::Key:Kind::Empty);if(!node)return NotFound;}
    if(parent){if(!leaf)return Invalid;for(uint32_t i=0;i<NameBytes;++i)leaf[i]=p.parts[p.count-1][i];}
    else if(follow && node->kind==Kind::Link){node=Get(node->target);if(!node || node->kind!=Kind::File)return Corrupt;}
    return Success;
}
void Volume::Clear(Node& n) noexcept {ClearTimes(uint32_t(&n-image_.nodes));const auto g=n.generation;n={};n.generation=g;dirty_=true;}
void Volume::Garbage() noexcept {
    for(uint32_t pass=0;pass<2;++pass)for(auto& n:image_.nodes)if(n.kind!=Kind::Empty && (n.flags&1)){bool used=false;for(const auto& h:handles_)if(h.id && h.node==n.id)used=true;for(const auto& link:image_.nodes)if(link.kind==Kind::Link && link.target==n.id)used=true;if(!used)Clear(n);}
}
bool Volume::Validate(const Image& i) noexcept {
    const auto& h=i.header;if(h.magic!=Magic || h.version!=Version || h.bytes!=sizeof(i) || h.nodeBytes!=sizeof(Node) || h.nodeCount!=MaxNodes || !h.sequence || h.checksum!=Digest(i) || !(Zero(h.reserved,sizeof(h.reserved)) || (h.reserved[0]==TimesMagic && Zero(h.reserved+49,10*sizeof(uint64_t)))))return false;
    for(uint32_t slot=0;slot<MaxNodes;++slot){const auto& n=i.nodes[slot];if(n.generation>0xFFFFFE)return false;
        if(n.kind==Kind::Empty){if(h.reserved[0]==TimesMagic)for(uint32_t field=0;field<3;++field)if(TimeValue(h,slot,field))return false;auto copy=n;copy.generation=0;const Node empty{};const auto* a=reinterpret_cast<const uint8_t*>(&copy);const auto* b=reinterpret_cast<const uint8_t*>(&empty);for(uint32_t at=0;at<sizeof(empty);++at)if(a[at]!=b[at])return false;continue;}
        if(n.kind>Kind::Link || n.id!=((n.generation<<8)|(slot+1)) || !n.generation || n.size>DataBytes || n.reserved || (n.flags&~1u) ||
            (n.flags && (!n.parent || (n.kind!=Kind::File && n.kind!=Kind::Directory && n.kind!=Kind::Link))) || (n.kind==Kind::File && n.type!=ValueType::None))return false;
        if(n.flags && n.kind==Kind::Directory)for(const auto& child:i.nodes)if(child.kind!=Kind::Empty && child.parent==n.id)return false;
        processes::Token t{};t.user=n.security.owner;t.primaryGroup=n.security.group;t.integrity=n.security.integrity;
        if(!processes::Manager::Access(t,n.security,processes::Kind::Process,0))return false;
        char name[NameBytes]{};if(!Name(n.name,name,n.kind==Kind::Value) || !Equal(n.name,name))return false;
        if(n.kind==Kind::Value && !Value(n.type,n.data,n.size))return false;
        if(n.kind!=Kind::File && n.kind!=Kind::Value && (n.size || n.type!=ValueType::None || !Zero(n.data,sizeof(n.data))))return false;
        if(n.parent){const auto index=n.parent&255u;if(!index || index>MaxNodes)return false;const auto& p=i.nodes[index-1];if(p.id!=n.parent || (p.kind!=Kind::Directory && p.kind!=Kind::Key))return false;
            if((p.kind==Kind::Directory && (n.kind==Kind::Key || n.kind==Kind::Value)) || (p.kind==Kind::Key && n.kind!=Kind::Key && n.kind!=Kind::Value))return false;
            auto parent=n.parent;uint32_t depth=0;while(parent){if(++depth>Depth)return false;const auto at=parent&255u;if(!at || at>MaxNodes || i.nodes[at-1].id!=parent)return false;parent=i.nodes[at-1].parent;}}
        else if(slot!=0 && slot!=1 && (n.kind!=Kind::Key || !Equal(n.name,"hkcu")))return false;
        if(n.kind==Kind::Link){const auto at=n.target&255u;if(!at || at>MaxNodes || i.nodes[at-1].id!=n.target || i.nodes[at-1].kind!=Kind::File)return false;}else if(n.target)return false;
        for(uint32_t j=0;j<slot;++j){const auto& a=i.nodes[j];if(a.kind!=Kind::Empty && !(n.flags&1) && !(a.flags&1) && a.parent==n.parent && (a.kind==Kind::Value)==(n.kind==Kind::Value) && Equal(a.name,n.name) && (n.parent || n.security.owner==a.security.owner))return false;}
    }
    return i.nodes[0].kind==Kind::Directory && !i.nodes[0].parent && Equal(i.nodes[0].name,"c") && i.nodes[1].kind==Kind::Key && !i.nodes[1].parent && Equal(i.nodes[1].name,"hklm");
}
void Volume::Format() noexcept {
    Reset(image_);const auto token=Admin();New(token,0,Kind::Directory,"c");New(token,0,Kind::Key,"hklm");New(token,0,Kind::Key,"hkcu");
    for(uint32_t n=0;n<2;++n){auto& sd=image_.nodes[n].security;sd.aceCount=2;sd.aces[1]={processes::Principal::Everyone,0,List|Read,0};}
    dirty_=true;
}
uint32_t Volume::Mount() noexcept {
    if(mounted_ || !block_ || !scheduler_)return Invalid;formatting_=false;
    auto* a=reinterpret_cast<uint8_t*>(&image_);auto* b=reinterpret_cast<uint8_t*>(&scratch_);
    for(uint32_t at=0;at<sizeof(Image);at+=512)if(!block_(context_,io::Op::Read,FirstSlot+at,a+at) || !block_(context_,io::Op::Read,SecondSlot+at,b+at))return BlockError();
    FormatIntent intent{};if(!block_(context_,io::Op::Read,FormatIntentOffset,reinterpret_cast<uint8_t*>(&intent)))return BlockError();
    const auto va=Validate(image_),vb=Validate(scratch_);const auto pending=ValidIntent(intent);
    if(!va && !vb){
        const bool blank=Zero(&image_,sizeof(image_)) && Zero(&scratch_,sizeof(scratch_));
        const bool interrupted=pending && Zero(&image_.header,sizeof(Header)) && Zero(&scratch_.header,sizeof(Header));
        if(!blank && !interrupted)return Corrupt;
        // The durable intent is written while both image slots are still blank.
        // It permits restarting an interrupted first commit, never erasing an
        // existing committed volume after arbitrary metadata corruption.
        if(!BackingReady(sizeof(Image)/512+2))return NoSpace;
        intent={};intent.magic=FormatMagic;intent.version=1;intent.bytes=sizeof(intent);intent.checksum=Hash(&intent,sizeof(intent),offsetof(FormatIntent,checksum),8);
        if(!block_(context_,io::Op::Write,FormatIntentOffset,reinterpret_cast<uint8_t*>(&intent)) || !block_(context_,io::Op::Flush,0,nullptr))return BlockError();
        Format();active_=1;formatting_=true;
    }else{
        if(va && vb && image_.header.sequence==scratch_.header.sequence && image_.header.checksum!=scratch_.header.checksum)return Corrupt;
        active_=!va || (vb && scratch_.header.sequence>image_.header.sequence)?1u:0u;if(active_)image_=scratch_;
        if(pending){intent={};if(!block_(context_,io::Op::Write,FormatIntentOffset,reinterpret_cast<uint8_t*>(&intent)) || !block_(context_,io::Op::Flush,0,nullptr))return BlockError();}
    }
    if(QuerySpace().allocated>allocationLimit_)return NoSpace;
    mounted_=true;Garbage();return Success;
}
uint32_t Volume::QueryKind(const processes::Token& token,const char* path,Kind& kind) noexcept {
    if(!mounted_||!processes::Manager::ValidToken(token))return Invalid;Node* node=nullptr;const auto status=Resolve(token,path,node);if(!status)kind=node->kind;return status;
}
uint32_t Volume::CheckAccess(const processes::Token& token,const char* path,uint32_t rights,uint32_t* identity) noexcept {if(!mounted_||rights&~All)return Invalid;Node* node=nullptr;const auto status=Resolve(token,path,node);if(status)return status;if(!Access(token,*node,rights))return Denied;if(identity)*identity=node->id;return 0;}
#ifdef GUARDIAN_SECURITY_NATIVE
uint32_t Volume::PersistRegistry(const processes::Token& token,const char* path,const char* name,const void* data,uint32_t bytes,bool remove) noexcept {
    if(!mounted_||bytes>DataBytes||(!data&&bytes))return Invalid;transaction_=image_;const bool dirty=dirty_;
    Command c;for(uint32_t i=0;i<PathBytes&&path[i];++i)c.path[i]=path[i];for(uint32_t i=0;i<NameBytes&&name[i];++i)c.name[i]=name[i];
    c.op=remove?Op::RegDeleteKey:Op::RegCreate;auto status=Execute(1,token,c);
    if(!status&&!remove){c.op=Op::RegSet;c.status=c.transferred=0;c.type=3;c.length=bytes;for(uint32_t i=0;i<bytes;++i)c.data[i]=static_cast<const uint8_t*>(data)[i];status=Execute(1,token,c);}
    if(status==NotFound&&remove)status=0;if(!status)status=Flush();if(status){image_=transaction_;dirty_=dirty;}return status;
}
uint32_t Volume::ProvisionUser(const processes::Token& token) noexcept {
    if(!mounted_||!processes::Manager::ValidToken(token)||!((token.user==100&&token.primaryGroup==10)||(token.user==200&&token.primaryGroup==20)))return Denied;
    image_.nodes[1].security.aces[1]={processes::Principal::Everyone,0,List|Read|ReadAcl,0};image_.nodes[1].security.aceCount=2;
    if(!Root(token,3,true))return NoSpace;const auto admin=Admin();auto* users=Child(image_.nodes[0].id,"users");
    if(!users)users=New(admin,image_.nodes[0].id,Kind::Directory,"users");if(!users||users->kind!=Kind::Directory||users->security.owner!=100)return Corrupt;
    users->security.aceCount=2;users->security.aces[1]={processes::Principal::Everyone,0,List|Read|ReadAcl,0};dirty_=true;
    const char* name=token.user==100?"minint":"localservice";auto* profile=Child(users->id,name);
    if(!profile)profile=New(token,users->id,Kind::Directory,name);if(!profile)return NoSpace;if(profile->kind!=Kind::Directory||profile->security.owner!=token.user)return Corrupt;return 0;
}
#endif
uint32_t Volume::Flush() noexcept {
    if(!mounted_)return Invalid;if(!dirty_)return Success;if(image_.header.sequence==UINT64_MAX)return Quota;
    scratch_=image_;auto& h=scratch_.header;uint64_t savedTimes[59]{};for(uint32_t i=0;i<59;++i)savedTimes[i]=h.reserved[i];h={};for(uint32_t i=0;i<59;++i)h.reserved[i]=savedTimes[i];h.magic=Magic;h.sequence=image_.header.sequence+1;h.version=Version;h.bytes=sizeof(Image);h.nodeBytes=sizeof(Node);h.nodeCount=MaxNodes;h.checksum=Digest(scratch_);
    if(!Validate(scratch_))return Corrupt;
    if(!BackingReady(sizeof(Image)/512+(formatting_?1u:0u)))return NoSpace;
    auto* data=reinterpret_cast<uint8_t*>(&scratch_);const auto base=active_?FirstSlot:SecondSlot;
    for(uint32_t at=512;at<sizeof(Image);at+=512)if(!block_(context_,io::Op::Write,base+at,data+at))return BlockError();
    if(!block_(context_,io::Op::Flush,0,nullptr) || !block_(context_,io::Op::Write,base,data) || !block_(context_,io::Op::Flush,0,nullptr))return BlockError();
    if(formatting_){FormatIntent clear{};if(!block_(context_,io::Op::Write,FormatIntentOffset,reinterpret_cast<uint8_t*>(&clear)) || !block_(context_,io::Op::Flush,0,nullptr))return BlockError();formatting_=false;}
    image_.header=h;active_=1-active_;dirty_=false;return Success;
}
uint32_t Volume::Unmount() noexcept {if(Handles() || Watches())return Sharing;mounted_=false;dirty_=false;Reset(image_);Reset(scratch_);return Success;}
Handle* Volume::Find(uint32_t owner,uint64_t id) noexcept {for(auto& h:handles_)if(h.id==id && id && h.owner==owner)return &h;return nullptr;}
bool Volume::Shared(uint32_t node,uint32_t access,uint32_t share) const noexcept {
    const auto required=[](uint32_t a) noexcept {return (a&Read?1u:0u)|(a&Write?2u:0u)|(a&Delete?4u:0u);};
    for(const auto& h:handles_)if(h.id && h.node==node && ((required(access)&~h.share) || (required(h.access)&~share)))return false;return true;
}
uint32_t Volume::Open(uint32_t owner,const processes::Token& t,Command& c) noexcept {
    if(!owner || !c.access || (c.access&~All) || (c.share&~7u) || c.disposition>2 || c.flags>1)return Invalid;
    uint32_t count=0;Handle* slot=nullptr;for(auto& h:handles_){if(h.id && h.owner==owner)++count;if(!h.id)slot=&h;}if(count==PerOwnerHandles || !slot || nextHandle_>UINT64_MAX-4)return Quota;
    Node* n=nullptr;auto status=Resolve(t,c.path,n);bool created=false;
    if(status==NotFound && c.disposition){Node* parent=nullptr;char leaf[NameBytes]{};status=Resolve(t,c.path,parent,true,leaf);if(status)return status;if(!parent || parent->kind!=Kind::Directory)return TypeMismatch;if(!Access(t,*parent,CreateChild))return Denied;
        n=New(t,parent->id,c.flags?Kind::Directory:Kind::File,leaf);if(!n)return NoSpace;created=true;
    }else if(status)return status;else if(c.disposition==1)return Collision;
    if(n->kind!=(c.flags?Kind::Directory:Kind::File))return TypeMismatch;if(!Access(t,*n,c.access))return Denied;if(!Shared(n->id,c.access,c.share))return Sharing;
    *slot={nextHandle_,owner,n->id,c.access,c.share};nextHandle_+=4;c.handle=slot->id;if(created)Notify(n->parent,1);return Success;
}
uint32_t Volume::Close(uint32_t owner,uint64_t id) noexcept {auto* h=Find(owner,id);if(!h)return InvalidHandle;*h={};Garbage();return Success;}
uint32_t Volume::FileIo(uint32_t owner,Command& c) noexcept {
    auto* h=Find(owner,c.handle);if(!h)return InvalidHandle;auto* n=Get(h->node);if(!n || n->kind!=Kind::File)return TypeMismatch;
    if(c.length>DataBytes || c.offset>DataBytes || c.length>DataBytes-c.offset)return NoSpace;
    if(c.op==Op::Write){if(!(h->access&Write))return Denied;if(!c.length)return Success;for(uint32_t i=0;i<c.length;++i)n->data[c.offset+i]=c.data[i];const auto end=static_cast<uint32_t>(c.offset)+c.length;if(end>n->size)n->size=end;++n->revision;Touch(*n,true);dirty_=true;c.transferred=c.length;Notify(n->parent,4);}
    else{if(!(h->access&Read))return Denied;const auto count=c.offset>=n->size?0u:n->size-static_cast<uint32_t>(c.offset);c.transferred=c.length<count?c.length:count;for(uint32_t i=0;i<c.transferred;++i)c.data[i]=n->data[c.offset+i];if(c.transferred)Touch(*n,false);}return Success;
}
uint32_t Volume::Unlink(const processes::Token& t,const char* path) noexcept {
    Node* n=nullptr;auto status=Resolve(t,path,n,false,nullptr,false);if(status)return status;if(!n->parent)return Denied;if(!Access(t,*n,Delete))return Denied;if(!Shared(n->id,Delete,7))return Sharing;
    if(n->kind==Kind::Directory)for(const auto& child:image_.nodes)if(child.kind!=Kind::Empty && child.parent==n->id)return NotEmpty;
    if(n->kind!=Kind::File && n->kind!=Kind::Directory && n->kind!=Kind::Link)return TypeMismatch;Notify(n->parent,1);n->flags|=1;dirty_=true;Garbage();return Success;
}
void Volume::Notify(uint32_t key,uint32_t mask) noexcept {
    for(auto& w:watches_)if(w.id && (w.mask&mask)){auto at=key;for(uint32_t d=0;at && d<=Depth;++d){if(at==w.key){w.changes|=mask;scheduler_->Signal(w.event);break;}if(!w.subtree)break;auto* n=Get(at);at=n?n->parent:0;}}
}
uint32_t Volume::WatchKey(uint32_t owner,const processes::Token& t,Command& c) noexcept {
    if(!c.access || (c.access&~15u) || c.flags>1)return Invalid;Node* n=nullptr;auto status=Resolve(t,c.path,n);if(status)return status;if(n->kind!=Kind::Key)return TypeMismatch;if(!Access(t,*n,List))return Denied;
    for(uint32_t i=0;i<MaxWatches;++i)if(!watches_[i].id && watches_[i].generation<0xFFFFFE){const auto event=scheduler_->CreateObject(threads::Kind::ManualEvent);if(!event)return Quota;auto& w=watches_[i];++w.generation;w.id=(w.generation<<8)|(i+1);w.owner=owner;w.key=n->id;w.mask=c.access;w.event=event;w.subtree=c.flags!=0;c.watch=w.id;return Success;}return Quota;
}
uint32_t Volume::WatchResult(uint32_t owner,Command& c,bool close) noexcept {
    if(c.watch>UINT32_MAX)return InvalidHandle;for(auto& w:watches_)if(w.id==c.watch && w.id && w.owner==owner){if(!close && !w.changes)return Pending;c.transferred=w.changes;scheduler_->Signal(w.event);if(scheduler_->Close(w.event)!=threads::Success)return Sharing;const auto g=w.generation;w={};w.generation=g;return Success;}return InvalidHandle;
}
uint32_t Volume::Wait(uint32_t owner,uint32_t id,uint32_t tid) noexcept {for(const auto& w:watches_)if(w.id==id && w.owner==owner)return scheduler_->Wait(tid,&w.event,1,false,threads::Never);return InvalidHandle;}
uint32_t Volume::Registry(uint32_t,const processes::Token& t,Command& c) noexcept {
    if(c.op==Op::RegCreate){Path path{};if(!Parse(c.path,path) || path.root==1)return Invalid;scratch_=image_;const bool dirty=dirty_;auto* n=Root(t,path.root,true);if(!n)return NoSpace;
        uint32_t changed[Depth]{},count=0;
        for(uint32_t i=0;i<path.count;++i){if(!Access(t,*n,List)){image_=scratch_;dirty_=dirty;return Denied;}auto* child=Child(n->id,path.parts[i],Kind::Key);if(!child){if(!Access(t,*n,CreateChild)){image_=scratch_;dirty_=dirty;return Denied;}changed[count++]=n->id;child=New(t,n->id,Kind::Key,path.parts[i]);if(!child){image_=scratch_;dirty_=dirty;return NoSpace;}}n=child;}
        for(uint32_t i=0;i<count;++i)Notify(changed[i],1);return Success;
    }
    Node* key=nullptr;auto status=Resolve(t,c.path,key);if(status)return status;if(key->kind!=Kind::Key)return TypeMismatch;
    const auto rights=c.op==Op::RegGet?Read:(c.op==Op::RegEnum?List:(c.op==Op::RegDeleteKey?Delete:Write));if(!Access(t,*key,rights))return Denied;
    if(c.op==Op::RegEnum){if(c.flags>1)return Invalid;uint32_t index=0;for(const auto& n:image_.nodes)if(n.kind!=Kind::Empty && n.parent==key->id && ((c.flags==0 && n.kind==Kind::Key) || (c.flags==1 && n.kind==Kind::Value))){if(index++!=c.index)continue;for(uint32_t i=0;i<NameBytes;++i)c.name[i]=n.name[i];c.type=static_cast<uint32_t>(n.type);c.transferred=n.size;return Success;}return NotFound;}
    if(c.op==Op::RegDeleteKey){if(!key->parent)return Denied;for(const auto& n:image_.nodes)if(n.kind==Kind::Key && n.parent==key->id)return NotEmpty;Notify(key->id,1);Notify(key->parent,1);for(auto& n:image_.nodes)if(n.kind==Kind::Value && n.parent==key->id)Clear(n);Clear(*key);return Success;}
    char name[NameBytes]{};if(!Name(c.name,name,true))return Invalid;auto* value=Child(key->id,name,Kind::Value);
    if(c.op==Op::RegSet){const auto type=static_cast<ValueType>(c.type);if(!Value(type,c.data,c.length))return Invalid;if(!value){value=New(t,key->id,Kind::Value,name);if(!value)return NoSpace;}
        for(uint32_t i=0;i<DataBytes;++i)value->data[i]=i<c.length?c.data[i]:0;value->size=c.length;value->type=type;++value->revision;dirty_=true;Notify(key->id,4);return Success;
    }
    if(!value)return NotFound;
    if(c.op==Op::RegDeleteValue){Clear(*value);Notify(key->id,4);return Success;}
    if(c.length<value->size){c.transferred=value->size;return Small;}for(uint32_t i=0;i<value->size;++i)c.data[i]=value->data[i];c.transferred=value->size;c.type=static_cast<uint32_t>(value->type);return Success;
}
uint32_t Volume::ExecuteRaw(uint32_t owner,const processes::Token& t,Command& c) noexcept {
    if(!mounted_ || !owner || !processes::Manager::ValidToken(t) || c.version!=Version || c.bytes!=sizeof(c) || c.reserved || c.status || c.transferred || c.length>DataBytes)return Invalid;
    const bool mutation=(c.op==Op::Write&&c.length)||c.op==Op::Rename||c.op==Op::Unlink||c.op==Op::Link||c.op==Op::SetAcl||c.op==Op::RegCreate||c.op==Op::RegSet||c.op==Op::RegDeleteValue||c.op==Op::RegDeleteKey||c.op==Op::Truncate||(c.op==Op::FileTimesWrite&&c.flags);
    if(mutation&&!BackingReady(sizeof(Image)/512+(formatting_?1u:0u)))return c.status=NoSpace;
    uint32_t status=Unsupported;
    if(c.op==Op::QuerySpace){Path path{};if(!Parse(c.path,path)||path.root!=1)return c.status=Invalid;const auto space=QuerySpace();const auto* bytes=reinterpret_cast<const uint8_t*>(&space);for(uint32_t i=0;i<sizeof(space);++i)c.data[i]=bytes[i];c.transferred=sizeof(space);status=Success;}
    else if(c.op==Op::Open)status=Open(owner,t,c);
    else if(c.op==Op::Mkdir){c.flags=1;c.access=List;c.share=7;c.disposition=1;status=Open(owner,t,c);if(!status)status=Close(owner,c.handle);c.handle=0;}
    else if(c.op==Op::Read || c.op==Op::Write)status=FileIo(owner,c);
    else if(c.op==Op::Close)status=Close(owner,c.handle);
    else if(c.op==Op::Unlink)status=Unlink(t,c.path);
    else if(c.op==Op::Flush)status=Flush();
    else if(c.op>=Op::RegCreate && c.op<=Op::RegDeleteKey)status=Registry(owner,t,c);
    else if(c.op==Op::RegWatch)status=WatchKey(owner,t,c);
    else if(c.op==Op::RegPoll || c.op==Op::CloseWatch)status=WatchResult(owner,c,c.op==Op::CloseWatch);
    else if(c.op==Op::Rename || c.op==Op::Link){
        Node* source=nullptr;Node* parent=nullptr;char leaf[NameBytes]{};status=Resolve(t,c.path,source,false,nullptr,c.op==Op::Link);
        if(!status)status=Resolve(t,c.other,parent,true,leaf);
        if(!status){if(!source->parent || source->kind==Kind::Key || source->kind==Kind::Value || parent->kind!=Kind::Directory)status=TypeMismatch;
            else if(!Access(t,*parent,CreateChild) || !Access(t,*source,c.op==Op::Link?Read:Delete))status=Denied;
            else if(Child(parent->id,leaf))status=Collision;
            else if(c.op==Op::Link){if(source->kind!=Kind::File)status=Unsupported;else if(auto* link=New(t,parent->id,Kind::Link,leaf)){link->target=source->id;Notify(parent->id,1);}else status=NoSpace;}
            else if(!Shared(source->id,Delete,7))status=Sharing;
            else{auto at=parent->id;uint32_t parentDepth=0;for(;at && parentDepth<=Depth;++parentDepth){if(at==source->id){status=Invalid;break;}auto* n=Get(at);at=n?n->parent:0;}
                for(const auto& child:image_.nodes)if(child.kind!=Kind::Empty){auto ancestor=child.parent;uint32_t distance=1;while(ancestor && distance<=Depth){if(ancestor==source->id){if(parentDepth+distance>Depth)status=Invalid;break;}auto* n=Get(ancestor);ancestor=n?n->parent:0;++distance;}}
                if(parentDepth>Depth)status=Invalid;if(!status){const auto old=source->parent;source->parent=parent->id;for(uint32_t i=0;i<NameBytes;++i)source->name[i]=leaf[i];dirty_=true;Notify(old,1);Notify(parent->id,1);}}
        }
    }else if(c.op==Op::SetAcl || c.op==Op::QueryAcl){Node* node=nullptr;status=Resolve(t,c.path,node);if(!status){if(!Access(t,*node,c.op==Op::SetAcl?WriteAcl:ReadAcl))status=Denied;
        else if(c.op==Op::QueryAcl){if(c.length<sizeof(processes::Security))status=Small;else{const auto* bytes=reinterpret_cast<const uint8_t*>(&node->security);for(uint32_t i=0;i<sizeof(node->security);++i)c.data[i]=bytes[i];c.transferred=sizeof(node->security);}}
        else if(c.length!=sizeof(processes::Security))status=Invalid;
        else{processes::Security sd{};auto* p=reinterpret_cast<uint8_t*>(&sd);for(uint32_t i=0;i<sizeof(sd);++i)p[i]=c.data[i];processes::Token ownerToken{};ownerToken.user=sd.owner;ownerToken.primaryGroup=sd.group;ownerToken.integrity=sd.integrity;
            if(sd.owner!=node->security.owner || sd.group!=node->security.group || sd.integrity!=node->security.integrity || sd.noWriteUp!=node->security.noWriteUp || !processes::Manager::Access(ownerToken,sd,processes::Kind::Process,0))status=Invalid;
            else{node->security=sd;dirty_=true;Notify(node->id,8);}}}}
    else if(c.op==Op::Enumerate){Node* node=nullptr;status=Resolve(t,c.path,node);if(!status){if(node->kind!=Kind::Directory)status=TypeMismatch;else if(!Access(t,*node,List))status=Denied;else{status=NotFound;uint32_t at=0;for(const auto& n:image_.nodes)if(n.kind!=Kind::Empty && !(n.flags&1) && n.parent==node->id){if(at++!=c.index)continue;for(uint32_t i=0;i<NameBytes;++i)c.name[i]=n.name[i];c.type=static_cast<uint32_t>(n.kind);c.transferred=n.size;status=Success;break;}}}}
    else if(c.op==Op::Truncate){auto* h=Find(owner,c.handle);if(!h)status=InvalidHandle;else if(!(h->access&Write))status=Denied;else if(c.offset>DataBytes)status=NoSpace;else{auto* n=Get(h->node);if(!n || n->kind!=Kind::File)status=TypeMismatch;else{const auto start=c.offset<n->size?static_cast<uint32_t>(c.offset):n->size;for(uint32_t i=start;i<DataBytes;++i)n->data[i]=0;n->size=static_cast<uint32_t>(c.offset);Touch(*n,true);dirty_=true;Notify(n->parent,4);status=Success;}}}
    else if(c.op==Op::FileTimesRead || c.op==Op::FileTimesWrite)status=Times(owner,c);
    else if(c.op==Op::FileSize){const auto* h=Find(owner,c.handle);const auto* n=h?Get(h->node):nullptr;if(!h)status=InvalidHandle;else if(!n || n->kind!=Kind::File)status=TypeMismatch;else {c.transferred=n->size;status=Success;}}
    c.status=status;return status;
}

void Volume::Describe(uint32_t owner,const processes::Token& token,const Command& c,char* path,Node*& node) noexcept {
    if(c.path[0]){if(!telemetry::Text(path,c.path))return;Resolve(token,c.path,node);}
    else if(auto* h=Find(owner,c.handle)){
        node=Get(h->node);const Node* chain[Depth+1]{};const Node* current=node;uint32_t depth=0;
        while(current&&depth<=Depth){chain[depth++]=current;if(!current->parent)break;current=Get(current->parent);}
        if(!depth||depth>Depth+1||!chain[depth-1]||chain[depth-1]->parent)return;
        uint32_t at=0;for(uint32_t index=depth;index;--index){const auto* part=chain[index-1];if(index==depth&&part->kind==Kind::Directory&&!part->parent){path[at++]='C';path[at++]=':';continue;}if(index!=depth&&at&&path[at-1]!='\\'&&at<127)path[at++]='\\';
            for(uint32_t i=0;i<NameBytes&&part->name[i]&&at<127;++i)path[at++]=part->name[i];}
        if(at==1&&path[0]=='c'&&at<125){path[at++]=':';path[at++]='\\';}
        if(at==2&&path[1]==':')path[at++]='\\';
        path[at]=0;
    }
    if(node&&c.op>=Op::RegCreate&&c.op<=Op::RegDeleteKey&&c.name[0]){char name[NameBytes]{};node=Name(c.name,name,true)?Child(node->id,name,Kind::Value):nullptr;}
}
uint32_t Volume::Execute(uint32_t owner,const processes::Token& token,Command& c) noexcept {
    using namespace telemetry;
    if(!exchange)return ExecuteRaw(owner,token,c);
    Path parsed{};const bool registryPath=c.path[0]&&Parse(c.path,parsed)&&parsed.root!=1;
    Domain domain=((c.op>=Op::RegCreate&&c.op<=Op::RegDeleteKey)||registryPath)?Domain::Registry:Domain::File;
    Operation op=Operation::Query;
    switch(c.op){case Op::Open:op=Operation::Open;break;case Op::Mkdir:case Op::RegCreate:op=Operation::Create;break;
    case Op::Read:case Op::RegGet:op=Operation::Read;break;case Op::Write:case Op::Truncate:case Op::RegSet:op=Operation::Write;break;
    case Op::Rename:op=Operation::Rename;break;case Op::Unlink:case Op::RegDeleteValue:case Op::RegDeleteKey:op=Operation::Delete;break;
    case Op::SetAcl:case Op::FileTimesWrite:op=Operation::Protect;break;case Op::Link:op=Operation::Create;break;case Op::Flush:op=Operation::Flush;break;default:break;}
    char path[128]{};Node* before=nullptr;Describe(owner,token,c,path,before);
    if(c.op==Op::Open&&c.disposition&&!before)op=Operation::Create;
    const char* other=c.op==Op::Rename||c.op==Op::Link?c.other:(domain==Domain::Registry?c.name:"");
    auto event=Begin(domain,op,owner,token.user,path,other,c.handle);
    if(before){event.flags|=ExistedBefore;event.oldHash=Hash(before->data,before->size);event.beforeBytes=before->size;}
    if(!Before(event)){event.newHash=event.oldHash;event.afterBytes=event.beforeBytes;if(event.flags&ExistedBefore)event.flags|=ExistsAfter;return c.status=End(event,Denied);}
    const auto status=ExecuteRaw(owner,token,c);
    Node* after=nullptr;char finalPath[128]{};
    if(c.op==Op::Rename&&!status)Resolve(token,c.other,after);else Describe(owner,token,c,finalPath,after);
    if(after){event.flags|=ExistsAfter;event.newHash=Hash(after->data,after->size);event.afterBytes=after->size;}
    return End(event,status);
}

uint32_t Volume::Handles() const noexcept {uint32_t n=0;for(const auto& h:handles_)if(h.id)++n;return n;}
uint32_t Volume::Watches() const noexcept {uint32_t n=0;for(const auto& w:watches_)if(w.id)++n;return n;}
void Volume::Exit(uint32_t owner) noexcept {for(auto& h:handles_)if(h.owner==owner)h={};for(auto& w:watches_)if(w.id && w.owner==owner){Command c{};c.watch=w.id;WatchResult(owner,c,true);}Garbage();}
}
