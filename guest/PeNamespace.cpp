#include "PeNamespace.h"
namespace guardian::pe {
namespace {
bool Equal(const char* a,const char* b) noexcept {return pevolume::Equal(a,b);}
const char* Leaf(const char* s) noexcept {const char* leaf=s;for(uint32_t i=0;s[i];++i)if(s[i]=='\\')leaf=s+i+1;return leaf;}
bool LowerName(const char* in,char* out) noexcept {
    if(!in)return false;uint32_t n=0;for(;n<127&&in[n];++n){auto c=in[n];if(c>='A'&&c<='Z')c=char(c+32);if(c=='/')c='\\';
        if(c<32||c>126)return false;out[n]=c;}out[n]=0;return n&&!(n==127&&in[n]);
}
bool Join(const char* directory,const char* file,char* out) noexcept {
    uint32_t n=0;for(;directory[n];++n){if(n>=126)return false;out[n]=directory[n];}
    for(uint32_t i=0;file[i];++i){if(n>=127)return false;out[n++]=file[i];}out[n]=0;return true;
}
bool Version(const char* text,uint32_t (&v)[4]) noexcept {
    uint32_t part=0,digits=0;for(uint32_t i=0;text[i];++i){const char c=text[i];if(c=='.'){if(!digits||part==3)return false;++part;digits=0;}
        else {if(c<'0'||c>'9'||v[part]>6553)return false;v[part]=v[part]*10+uint32_t(c-'0');if(v[part]>65535)return false;++digits;}}
    return part==3&&digits;
}
bool Space(char c) noexcept {return c==' '||c=='\t'||c=='\r'||c=='\n';}
bool Word(char c) noexcept {return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c==':'||c=='_'||c=='-'||c=='.';}
}
bool Catalog::Open(const uint8_t* data,uint32_t bytes,uint32_t profile,Source* sources,uint32_t& count,const char* root) noexcept {
    *this={};count=0;if(!pevolume::Valid(data,bytes)||!sources)return false;
    const auto* h=reinterpret_cast<const pevolume::Header*>(data);if(h->profile!=profile)return false;
#ifndef GUARDIAN_COMPONENT_NATIVE
    if(h->version!=1)return false;
#endif
    uint32_t peCount=0;for(uint32_t i=0;i<h->files;++i)if(pevolume::FileAt(*h,i).kind==pevolume::Kind::Pe)++peCount;
    if(peCount>MaxModules)return false;
    header_=h;data_=data;sources_=sources;
    const auto add=[&](uint32_t i) noexcept {const auto& f=pevolume::FileAt(*h,i);sources[count++]={f.path,data+f.offset,f.bytes};};
    uint32_t selected=h->root;if(root){char canonical[128]{};if(!pevolume::Canonical(root,canonical))return false;selected=UINT32_MAX;
        for(uint32_t i=0;i<h->files;++i)if(pevolume::FileAt(*h,i).kind==pevolume::Kind::Pe&&Equal(canonical,pevolume::FileAt(*h,i).path))selected=i;if(selected==UINT32_MAX)return false;}
    add(selected);for(uint32_t i=0;i<h->files;++i)if(i!=selected&&pevolume::FileAt(*h,i).kind==pevolume::Kind::Pe)add(i);
#if defined(GUARDIAN_WIN32_NATIVE) && !defined(GUARDIAN_COMPAT32)
    uint32_t schemaBytes=0;const auto* schema=File("c:\\windows\\system32\\apisetschema.dll",schemaBytes);
    if(!schema||!schema_.OpenPe(schema,schemaBytes,profile==2?2u:6u))return false;
#endif
    sourceCount_=count;return count<=MaxModules;
}
Namespace Catalog::Resolver() noexcept {
    return {this,[](void* p,const char* n,const char* i) noexcept {return static_cast<Catalog*>(p)->Find(n,i);},
        [](void* p,const uint8_t* data,uint32_t n) noexcept {return static_cast<Catalog*>(p)->DefaultManifest(data,n);}};
}
uint32_t Catalog::Exact(const char* path) const noexcept {for(uint32_t i=0;i<sourceCount_;++i)if(Equal(sources_[i].name,path))return i;return UINT32_MAX;}
uint32_t Catalog::ActiveMask() const noexcept {
    for(const auto& stack:stacks_)if(stack.tid==tid_&&stack.depth)for(const auto& c:contexts_)if(c.id==stack.handles[stack.depth-1])return c.mask;
    return defaultMask_;
}
uint32_t Catalog::Find(const char* input,const char* importer) const noexcept {return FindDepth(input,importer,0);}
uint32_t Catalog::FindDepth(const char* input,const char* importer,uint32_t depth) const noexcept {
    if(depth>=8)return UINT32_MAX;
#ifndef GUARDIAN_WIN32_NATIVE
    (void)importer;
#endif
    if(!header_)return UINT32_MAX;char name[128]{};if(!LowerName(input,name))return UINT32_MAX;
    const auto* requestedLeaf=Leaf(name);bool extension=false;for(uint32_t i=0;requestedLeaf[i];++i)if(requestedLeaf[i]=='.')extension=true;
    if(!extension){uint32_t n=0;while(name[n])++n;if(!requestedLeaf[0]||n>123)return UINT32_MAX;
        name[n++]='.';name[n++]='d';name[n++]='l';name[n++]='l';name[n]=0;}
    bool path=false;for(uint32_t i=0;name[i];++i)if(name[i]=='\\'||name[i]==':')path=true;
    #ifdef GUARDIAN_COMPAT32
    if(path&&pevolume::Prefix(name,"c:\\windows\\system32\\")){char redirected[128]{};if(!Join("c:\\windows\\syswow64\\",name+20,redirected))return UINT32_MAX;return Exact(redirected);}
#endif
    if(path){char canonical[128]{};return pevolume::Canonical(name,canonical)?Exact(canonical):UINT32_MAX;}
    for(uint32_t i=0;name[i];++i)if(!Word(name[i]))return UINT32_MAX;
    if(name[0]=='.')return UINT32_MAX;
    if(pevolume::Prefix(name,"api-ms-")||pevolume::Prefix(name,"ext-ms-")){
#ifdef GUARDIAN_WIN32_NATIVE
        char host[128]{};return schema_.Resolve(name,importer,host)?FindDepth(host,importer,depth+1):UINT32_MAX;
#else
        for(uint32_t i=0;i<header_->aliases;++i)if(Equal(name,header_->redirects[i].name))return Exact(header_->redirects[i].target);
        return UINT32_MAX;
#endif
    }
    // This contract's runtime is always selected from the guest system directory.
    if(Equal(name,"guardian-runtime-api.dll")||Equal(name,"user32.dll")
#ifdef GUARDIAN_NATIVE_NT
        ||Equal(name,"ntdll.dll")
#ifdef GUARDIAN_WIN32_NATIVE
        ||Equal(name,"kernel32.dll")||Equal(name,"kernelbase.dll")
#endif
#endif
        ){
        char full[128]{};return Join(
#ifdef GUARDIAN_COMPAT32
    "c:\\windows\\syswow64\\",
#else
    "c:\\windows\\system32\\",
#endif
    name,full)?Exact(full):UINT32_MAX;
    }
    const auto mask=ActiveMask();uint32_t selected=UINT32_MAX;
    for(uint32_t a=0;a<header_->assemblies;++a)if(mask&(1u<<a)){
        char full[128]{};if(!Join(header_->identities[a].directory,name,full))return UINT32_MAX;
        const auto candidate=Exact(full);if(candidate!=UINT32_MAX){if(selected!=UINT32_MAX)return UINT32_MAX;selected=candidate;}
    }if(selected!=UINT32_MAX)return selected;
    char directory[128]{},full[128]{};uint32_t length=0;
    const auto* root=RootName();const auto* leaf=Leaf(root);while(root+length<leaf){directory[length]=root[length];++length;}
    if(Join(directory,name,full)){selected=Exact(full);if(selected!=UINT32_MAX)return selected;}
    return Join(
#ifdef GUARDIAN_COMPAT32
    "c:\\windows\\syswow64\\",
#else
    "c:\\windows\\system32\\",
#endif
    name,full)?Exact(full):UINT32_MAX;
}
const uint8_t* Catalog::File(const char* path,uint32_t& bytes) const noexcept {
    bytes=0;if(!header_)return nullptr;char full[128]{};if(!pevolume::Canonical(path,full))return nullptr;
    for(uint32_t i=0;i<header_->files;++i){const auto& f=pevolume::FileAt(*header_,i);if(Equal(full,f.path)){bytes=f.bytes;return data_+f.offset;}}
    return nullptr;
}
bool Catalog::Manifest(const uint8_t* data,uint32_t bytes,uint32_t& mask,uint32_t& mode) const noexcept {
#ifdef GUARDIAN_FRAMEWORKS
    // The ordinary C# compiler embeds a UTF-8 BOM in its asInvoker manifest.
    if(bytes>=3&&data[0]==0xEF&&data[1]==0xBB&&data[2]==0xBF){data+=3;bytes-=3;}
#endif
    mask=mode=0;if(!header_||!data||!bytes||bytes>4096)return false;
    // A deliberately bounded XML subset: UTF-8 ASCII, no DTD/entities, nested
    // assembly identities and documented supportedOS GUIDs. Unknown security,
    // COM/window/class redirection requirements must not be silently enabled.
    char xml[4097]{};for(uint32_t i=0;i<bytes;++i){if(!data[i]||data[i]>126||(data[i]<32&&!Space(char(data[i]))))return false;xml[i]=char(data[i]);}
    uint32_t at=0,depth=0,dependent=0,tokens=0;char stack[16][64]{};bool assembly=false,declaration=false;
    while(at<bytes){if(Space(xml[at])){++at;continue;}if(xml[at]!='<')return false;++at;
        if(xml[at]=='?'){if(declaration||assembly||at+5>=bytes||xml[at+1]!='x'||xml[at+2]!='m'||xml[at+3]!='l'||!Space(xml[at+4]))return false;declaration=true;
            while(at+1<bytes&&!(xml[at]=='?'&&xml[at+1]=='>'))++at;if(at+1>=bytes)return false;at+=2;continue;}
        if(xml[at]=='!'){
#ifdef GUARDIAN_DESKTOP_NATIVE
            if(at+2>=bytes||xml[at+1]!='-'||xml[at+2]!='-')return false;at+=3;bool ended=false;while(at+2<bytes){if(xml[at]=='-'&&xml[at+1]=='-'){if(xml[at+2]!='>')return false;at+=3;ended=true;break;}++at;}if(!ended)return false;continue;
#else
            return false;
#endif
        }bool closing=false;if(xml[at]=='/'){closing=true;++at;}
        char tag[64]{};uint32_t n=0;while(at<bytes&&Word(xml[at])){if(n==63)return false;tag[n++]=xml[at++];}if(!n||++tokens>128)return false;
        if(closing){while(Space(xml[at]))++at;if(xml[at++]!='>'||!depth||!Equal(stack[depth-1],tag))return false;
            if(Equal(tag,"dependentAssembly"))dependent=0;--depth;continue;}
        const char* parent=depth?stack[depth-1]:"";
        const bool allowed=Equal(tag,"assembly")?(!depth&&!assembly):
            Equal(tag,"assemblyIdentity")?(Equal(parent,"assembly")||Equal(parent,"dependentAssembly")):
            Equal(tag,"dependency")?Equal(parent,"assembly"):
            Equal(tag,"dependentAssembly")?Equal(parent,"dependency"):
            Equal(tag,"compatibility")?Equal(parent,"assembly"):
            Equal(tag,"application")?Equal(parent,"compatibility"):
            Equal(tag,"supportedOS")?Equal(parent,"application"):
#ifdef GUARDIAN_DESKTOP_NATIVE
            Equal(tag,"trustInfo")?Equal(parent,"assembly"):
            Equal(tag,"security")?Equal(parent,"trustInfo"):
            Equal(tag,"requestedPrivileges")?Equal(parent,"security"):
            Equal(tag,"requestedExecutionLevel")?Equal(parent,"requestedPrivileges"):
#endif
            false;
        if(!allowed)return false;
        char identity[64]{},version[64]{},arch[64]{},type[64]{},guid[64]{},keys[16][64]{};bool self=false;
        for(uint32_t attrs=0;;++attrs){if(attrs>16)return false;while(Space(xml[at]))++at;
            if(xml[at]=='>'){++at;break;}if(xml[at]=='/'&&xml[at+1]=='>'){self=true;at+=2;break;}
            char key[64]{},value[128]{};n=0;while(at<bytes&&Word(xml[at])){if(n==63)return false;key[n++]=xml[at++];}if(!n)return false;
            if(attrs==16)return false;for(uint32_t i=0;i<attrs;++i)if(Equal(keys[i],key))return false;for(uint32_t i=0;i<64;++i)keys[attrs][i]=key[i];
            while(Space(xml[at]))++at;if(xml[at++]!='=')return false;while(Space(xml[at]))++at;
            const auto quote=xml[at++];if(quote!='\''&&quote!='"')return false;n=0;
            while(at<bytes&&xml[at]!=quote){if(n==127||xml[at]=='&'||xml[at]=='<')return false;value[n++]=xml[at++];}
            if(at>=bytes)return false;++at;
            char* target=nullptr;if(Equal(key,"name"))target=identity;else if(Equal(key,"version"))target=version;else if(Equal(key,"processorArchitecture"))target=arch;
            else if(Equal(key,"type"))target=type;else if(Equal(key,"Id"))target=guid;

#ifdef GUARDIAN_DESKTOP_NATIVE
            else if(Equal(key,"level")){if(!Equal(tag,"requestedExecutionLevel")||!Equal(value,"asInvoker"))return false;identity[0]='a';}
            else if(Equal(key,"uiAccess")){if(!Equal(tag,"requestedExecutionLevel")||!Equal(value,"false"))return false;}
#endif
            else if(!Equal(key,"xmlns")&&!Equal(key,"manifestVersion"))return false;
            if(Equal(key,"manifestVersion")&&!Equal(value,"1.0"))return false;
            if(target){if(n>=64||target[0])return false;for(uint32_t i=0;i<n;++i)target[i]=value[i];}
        }
        if(Equal(tag,"assembly"))assembly=true;
        if(Equal(tag,"dependentAssembly")){if(dependent)return false;dependent=depth+1;}
        if(Equal(tag,"assemblyIdentity")&&dependent){
            uint32_t v[4]{};if(!identity[0]||!Version(version,v)||!Equal(arch,"amd64")||!Equal(type,"win32"))return false;
            uint32_t selected=UINT32_MAX;for(uint32_t i=0;i<header_->assemblies;++i){const auto& candidate=header_->identities[i];bool same=Equal(identity,candidate.name);
                for(uint32_t j=0;j<4;++j)if(v[j]!=candidate.version[j])same=false;if(same)selected=i;}
            if(selected==UINT32_MAX)return false;
            for(uint32_t i=0;i<header_->assemblies;++i)if((mask&(1u<<i))&&Equal(header_->identities[i].name,identity))return false;
            mask|=1u<<selected;
        }
        if(Equal(tag,"supportedOS")){if(Equal(guid,"{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"))mode=2;
            else if(Equal(guid,"{1f676c76-80e1-4239-95bb-83d0f6d0da78}")&&mode<1)mode=1;}
        if(Equal(tag,"comClass")||Equal(tag,"windowClass")||Equal(tag,"file")
#ifndef GUARDIAN_DESKTOP_NATIVE
           ||Equal(tag,"trustInfo")||Equal(tag,"requestedPrivileges")
#endif
        )return false;
#ifdef GUARDIAN_DESKTOP_NATIVE
        if(Equal(tag,"requestedExecutionLevel")&&identity[0]!='a')return false;
#endif
        if(!self){if(depth==16)return false;for(uint32_t i=0;i<64;++i)stack[depth][i]=tag[i];++depth;}
        else if(Equal(tag,"dependentAssembly"))dependent=0;
    }return assembly&&!depth;
}
bool Catalog::DefaultManifest(const uint8_t* data,uint32_t bytes) noexcept {uint32_t mask=0,mode=0;if(!Manifest(data,bytes,mask,mode))return false;defaultMask_=mask;versionMode_=mode;return true;}
uint64_t Catalog::CreateContext(const char* path) noexcept {
    uint32_t bytes=0;const auto* data=File(path,bytes);uint32_t mask=0,mode=0;if(!data||!Manifest(data,bytes,mask,mode))return 0;
    for(auto& c:contexts_)if(!c.id){if(nextContext_==UINT64_MAX)return 0;c={nextContext_++,mask,0,false};return c.id;}return 0;
}
bool Catalog::Activate(uint32_t tid,uint64_t handle,uint64_t& cookie) noexcept {
    cookie=0;Context* context=nullptr;for(auto& c:contexts_)if(c.id==handle&&!c.released)context=&c;if(!context||!tid||nextCookie_==UINT64_MAX)return false;
    Stack* stack=nullptr;for(auto& s:stacks_)if(s.tid==tid)stack=&s;if(!stack)for(auto& s:stacks_)if(!s.tid){stack=&s;s.tid=tid;break;}
    if(!stack||stack->depth==8)return false;cookie=nextCookie_++;stack->handles[stack->depth]=handle;stack->cookies[stack->depth++]=cookie;++context->references;return true;
}
bool Catalog::Deactivate(uint32_t tid,uint64_t cookie) noexcept {
    for(auto& s:stacks_)if(s.tid==tid&&s.depth&&s.cookies[s.depth-1]==cookie){const auto id=s.handles[--s.depth];
        for(auto& c:contexts_)if(c.id==id){if(!c.references)return false;--c.references;if(!c.references&&c.released)c={};}
        if(!s.depth)s={};return true;}return false;
}
bool Catalog::ReleaseContext(uint64_t handle) noexcept {for(auto& c:contexts_)if(c.id==handle&&!c.released){c.released=true;if(!c.references)c={};return true;}return false;}
void Catalog::ThreadExit(uint32_t tid) noexcept {for(auto& s:stacks_)if(s.tid==tid)while(s.depth){const auto cookie=s.cookies[s.depth-1];if(!Deactivate(tid,cookie))break;}}
}
