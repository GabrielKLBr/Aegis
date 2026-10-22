#include "Package.h"
#include "guardian/HardwareProjection.h"
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
namespace av::microvm::hardware {
namespace {
using state::Require;
std::string Lower(std::string s){for(auto& c:s)c=guardian::hardware::Fold(c);return s;}
std::string Trim(std::string s){const auto a=s.find_first_not_of(" \t\r");if(a==s.npos)return {};const auto b=s.find_last_not_of(" \t\r");return s.substr(a,b-a+1);}
std::string Unquote(std::string s){s=Trim(s);if(s.size()>=2&&s.front()=='"'&&s.back()=='"')return s.substr(1,s.size()-2);Require(s.find('"')==s.npos,"Malformed INF quoting");return s;}
std::vector<std::string> Csv(const std::string& text){std::vector<std::string> fields;bool quote=false;size_t start=0;
    for(size_t i=0;i<=text.size();++i){if(i<text.size()&&text[i]=='"')quote=!quote;if(i==text.size()||(!quote&&text[i]==',')){fields.push_back(Unquote(text.substr(start,i-start)));start=i+1;}}
    Require(!quote&&fields.size()<=32,"INF value list rejected");return fields;
}
bool Leaf(const std::string& name){return !name.empty()&&name.size()<=64&&name!="."&&name!=".."&&name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-")==name.npos;}
using Rows=std::vector<std::pair<std::string,std::string>>;
using Inf=std::map<std::string,Rows>;
std::string Field(const Inf& inf,const std::string& section,const std::string& key,bool required=true){
    const auto found=inf.find(Lower(section));if(found!=inf.end())for(const auto& [name,value]:found->second)if(name==Lower(key))return value;
    Require(!required,"Required INF directive is absent");return {};
}
Inf Parse(const std::vector<uint8_t>& bytes){
    Require(!bytes.empty()&&bytes.size()<=65536,"INF size rejected");std::string text;
    if(bytes.size()>=2&&bytes[0]==0xFF&&bytes[1]==0xFE){Require(!(bytes.size()&1),"Truncated UTF-16 INF");for(size_t n=2;n<bytes.size();n+=2){Require(!bytes[n+1],"Selected INF subset requires ASCII fields");text+=char(bytes[n]);}}
    else {size_t skip=bytes.size()>=3&&bytes[0]==0xEF&&bytes[1]==0xBB&&bytes[2]==0xBF?3:0;text.assign(bytes.begin()+skip,bytes.end());}
    Inf inf;std::string section;std::istringstream input(text);std::string line;size_t lines=0;
    while(std::getline(input,line)){
        Require(++lines<=2048&&line.size()<=1024,"INF line limit exceeded");bool quoted=false;
        for(size_t n=0;n<line.size();++n){const auto c=static_cast<unsigned char>(line[n]);Require(c==9||c==13||(c>=32&&c<=126),"INF encoding rejected");if(c=='"')quoted=!quoted;if(c==';'&&!quoted){line.resize(n);break;}}
        Require(!quoted,"Unterminated INF quote");line=Trim(line);if(line.empty())continue;
        if(line.front()=='['){Require(line.size()>2&&line.back()==']',"Malformed INF section");section=Lower(Trim(line.substr(1,line.size()-2)));Require(inf.size()<64&&!inf.contains(section),"Duplicate or excess INF section");inf[section]={};continue;}
        Require(!section.empty(),"INF directive without section");const auto equals=line.find('=');const auto key=equals==line.npos?Lower(Unquote(line)):Lower(Trim(line.substr(0,equals)));
        const auto value=equals==line.npos?std::string{}:Trim(line.substr(equals+1));auto& rows=inf[section];Require(rows.size()<128,"INF section size rejected");
        for(const auto& row:rows)Require(row.first!=key,"Duplicate INF directive");rows.emplace_back(key,value);
    }return inf;
}
std::string Expand(std::string s,const Inf& inf){
    for(unsigned depth=0;depth<8;++depth){const auto a=s.find('%');if(a==s.npos)return Unquote(s);const auto b=s.find('%',a+1);Require(b!=s.npos,"Unterminated INF string token");const auto token=s.substr(a+1,b-a-1);
        if(token=="12")return Unquote(s);const auto value=Unquote(Field(inf,"strings",token));s.replace(a,b-a+1,value);Require(s.size()<=1024,"INF string expansion too long");}
    throw std::runtime_error("Recursive INF strings rejected");
}
std::vector<uint8_t> Source(const std::filesystem::path& parent,const std::string& name,uint64_t limit){
    Require(Leaf(name),"Driver package path traversal rejected");std::filesystem::path file;size_t count=0;
    for(const auto& entry:std::filesystem::directory_iterator(parent)){Require(++count<=512,"Driver package directory limit exceeded");if(Lower(entry.path().filename().string())==Lower(name)){Require(file.empty(),"Ambiguous case-folded package filename");file=entry.path();}}
    Require(!file.empty(),"Driver package dependency is missing");return state::ReadFile(file,limit);
}
struct Pe {
    std::span<const uint8_t> bytes;uint32_t nt=0,opt=0,sections=0,table=0,headers=0,image=0,machine=0,magic=0;
    uint16_t U16(size_t at) const {Require(at<=bytes.size()&&bytes.size()-at>=2,"Truncated driver image");return uint16_t(bytes[at]|(uint16_t(bytes[at+1])<<8));}
    uint32_t U32(size_t at) const {return uint32_t(U16(at))|(uint32_t(U16(at+2))<<16);}
    size_t Rva(uint32_t at,uint32_t size) const {
        if(at<headers){Require(size<=headers-at&&at<=bytes.size()&&size<=bytes.size()-at,"Driver header RVA outside file");return at;}
        for(uint32_t n=0;n<sections;++n){const auto sh=table+n*40,start=U32(sh+12),raw=U32(sh+16),offset=U32(sh+20);
            if(at>=start&&at-start<=raw&&size<=raw-(at-start)){const uint64_t pos=uint64_t(offset)+(at-start);Require(pos<=bytes.size()&&size<=bytes.size()-pos,"Driver RVA outside raw data");return size_t(pos);}}
        throw std::runtime_error("Driver RVA is not backed by file data");
    }
    std::string String(uint32_t at) const {std::string s;for(uint32_t n=0;n<128;++n){Require(at<=UINT32_MAX-n,"Driver string RVA overflow");const auto c=bytes[Rva(at+n,1)];if(!c)return s;Require(c>=32&&c<=126,"Invalid driver import name");s+=char(c);}throw std::runtime_error("Driver import name limit exceeded");}
    explicit Pe(std::span<const uint8_t> source):bytes(source){
        Require(bytes.size()>=64&&U16(0)==0x5A4D,"Driver DOS signature rejected");nt=U32(60);Require(nt<=bytes.size()&&bytes.size()-nt>=24&&U32(nt)==0x4550,"Driver PE header rejected");
        machine=U16(nt+4);sections=U16(nt+6);const auto optional=U16(nt+20);opt=nt+24;
        Require(optional>=96&&optional<=bytes.size()-opt&&sections&&sections<=96,"Driver optional header rejected");magic=U16(opt);
        Require((machine==0x8664&&magic==0x20B&&optional>=240)||(machine==0x14C&&magic==0x10B&&optional>=224),"Driver machine/format rejected");
        Require(U16(opt+68)==1,"Driver must use the native subsystem");headers=U32(opt+60);image=U32(opt+56);table=opt+optional;
        Require(headers<=bytes.size()&&table<=headers&&sections*40<=headers-table&&image,"Driver section table rejected");
        for(uint32_t n=0;n<sections;++n){const auto sh=table+n*40;const uint64_t raw=U32(sh+16),offset=U32(sh+20);Require(offset<=bytes.size()&&raw<=bytes.size()-offset,"Driver section raw data rejected");}
    }
};
void Inspect(DriverPackage& p){
    const Pe pe(p.image);p.machine=pe.machine;p.imports.clear();
    const auto directories=pe.opt+(pe.magic==0x20B?112:96),imports=pe.U32(directories+8),importBytes=pe.U32(directories+12);
    if(pe.U32(directories+13*8))p.requirements.push_back("delay-imports-not-supported");
    if(pe.U32(directories+9*8))p.requirements.push_back("driver-tls-not-supported");
    if(pe.image>guardian::driver::MaxFile||p.image.size()>guardian::driver::MaxFile||pe.sections>16)p.requirements.push_back("driver-image-limit");
    for(uint32_t n=0;n<pe.sections;++n)if((pe.U32(pe.table+n*40+36)&0xA0000000u)==0xA0000000u)p.requirements.push_back("write-execute-driver-section");
    if(!imports)return;
    Require(importBytes>=20&&importBytes<=65536,"Driver import directory size rejected");bool terminated=false;
    for(uint32_t n=0;n<importBytes/20&&n<128;++n){Require(imports<=UINT32_MAX-n*20,"Driver import directory overflow");const auto d=pe.Rva(imports+n*20,20);const auto name=pe.U32(d+12),iat=pe.U32(d+16),lookup=pe.U32(d);
        if(!name&&!iat&&!lookup){terminated=true;break;}const auto dll=Lower(pe.String(name));Require(Leaf(dll),"Invalid driver module dependency");
        const auto thunk=lookup?lookup:iat;bool end=false;
        for(uint32_t k=0;k<128;++k){const auto stride=pe.magic==0x20B?8u:4u;Require(thunk<=UINT32_MAX-k*stride,"Driver thunk overflow");const auto at=pe.Rva(thunk+k*stride,stride);const uint64_t value=pe.U32(at)|(pe.magic==0x20B?uint64_t(pe.U32(at+4))<<32:0);
            if(!value){end=true;break;}Require(p.imports.size()<128,"Driver import limit exceeded");
            if(value&(pe.magic==0x20B?(1ull<<63):(1ull<<31)))p.imports.push_back(dll+"!#ordinal");
            else {Require(value<=UINT32_MAX-2,"Driver import name overflow");p.imports.push_back(dll+"!"+pe.String(static_cast<uint32_t>(value)+2));}}
        Require(end,"Unterminated driver thunk array");
    }Require(terminated,"Unterminated driver import directory");
}
void Metadata(DriverPackage& p){
    const auto inf=Parse(p.inf);p.requirements.clear();
    Require(Expand(Field(inf,"version","signature"),inf)=="$Windows NT$","INF signature directive rejected");
    if(Lower(Expand(Field(inf,"version","class"),inf))!="system")p.requirements.push_back("driver-class-not-supported");
    Require(inf.contains("manufacturer")&&inf.at("manufacturer").size()==1,"One manufacturer is supported per driver package");
    const auto manufacturer=Csv(inf.at("manufacturer")[0].second);Require(manufacturer.size()==2,"One architecture/NT decoration is required");
    const auto decoration=Lower(manufacturer[1]);std::string section=Lower(manufacturer[0]+"."+manufacturer[1]);
    if(decoration.rfind("ntamd64",0)!=0)p.requirements.push_back("inf-architecture-not-supported");
    if(decoration=="ntamd64.10.0"){p.minimumMajor=10;p.minimumMinor=0;}else p.requirements.push_back("inf-nt-decoration-not-supported");
    Require(inf.contains(section)&&inf.at(section).size()==1,"Exactly one device binding is supported");const auto model=Csv(inf.at(section)[0].second);Require(model.size()==2,"INF model binding rejected");
    p.hardwareId=Expand(model[1],inf);const auto install=Lower(model[0]);const auto copies=Lower(Expand(Field(inf,install,"copyfiles"),inf));
    Require(inf.contains(copies)&&inf.at(copies).size()==1,"Exactly one native driver binary is supported");p.binary=inf.at(copies)[0].first;Require(Leaf(p.binary),"INF binary path rejected");
    const auto service=Csv(Field(inf,install+".services","addservice"));Require(service.size()==3,"INF AddService directive rejected");p.service=Expand(service[0],inf);Require(Leaf(p.service),"Invalid driver service name");
    const auto serviceSection=Lower(service[2]);const auto binary=Expand(Field(inf,serviceSection,"servicebinary"),inf);
    Require(Lower(binary)=="%12%\\"+Lower(p.binary),"INF service binary differs from CopyFiles");
    Require(Field(inf,serviceSection,"servicetype")=="1"&&Field(inf,serviceSection,"starttype")=="3","Only demand-start kernel driver services are supported");
    for(const auto& [name,rows]:inf)for(const auto& [key,value]:rows){(void)name;
        if(key=="include"||key=="needs")p.requirements.push_back("external-inf-dependency:"+Unquote(value));
        if(key=="addreg"||key=="delreg"||key=="coinstallers32"||key=="registerdlls"||key=="copyinf")p.requirements.push_back("unsupported-inf-action:"+key);}
    p.catalog=Expand(Field(inf,"version","catalogfile",false),inf);
    if(p.catalog.empty())p.catalog=Expand(Field(inf,"version","catalogfile.ntamd64",false),inf);
    const auto unsupported=[&](const std::string& part,const std::set<std::string>& allowed){
        for(const auto& [key,value]:inf.at(part)){(void)value;if(!allowed.contains(key))p.requirements.push_back("unsupported-inf-action:"+key);}
    };
    unsupported(install,{"copyfiles"});unsupported(install+".services",{"addservice"});
    unsupported(serviceSection,{"displayname","servicetype","starttype","errorcontrol","servicebinary"});
    if(Lower(service[1])!="0x2"&&service[1]!="2")p.requirements.push_back("unsupported-addservice-flags");
}
bool CatalogEnvelope(const std::vector<uint8_t>& b){
    // Recognize a bounded DER SignedData envelope. This is deliberately not a
    // trust verdict; CAT-bearing packages are ineligible for the lab driver ABI.
    size_t at=0;
    const auto length=[&](size_t& pos)->size_t{Require(pos<b.size(),"Truncated CAT length");const auto first=b[pos++];if(!(first&128))return first;const unsigned n=first&127;Require(n&&n<=4&&n<=b.size()-pos,"CAT indefinite/oversized length rejected");size_t out=0;for(unsigned k=0;k<n;++k)out=(out<<8)|b[pos++];return out;};
    if(b.empty()||b[at++]!=0x30)return false;const auto outer=length(at);if(outer!=b.size()-at||at>=b.size()||b[at++]!=6)return false;const auto n=length(at);
    constexpr uint8_t oid[]{0x2A,0x86,0x48,0x86,0xF7,0x0D,1,7,2};
    if(n!=sizeof(oid)||n>b.size()-at||!std::equal(std::begin(oid),std::end(oid),b.begin()+at))return false;at+=n;
    if(at>=b.size()||b[at++]!=0xA0)return false;const auto content=length(at);if(content!=b.size()-at||at>=b.size()||b[at++]!=0x30)return false;
    const auto signedData=length(at);if(signedData!=b.size()-at)return false;
    // Check the mandatory SignedData fields and their bounds. Cryptographic
    // verification, catalog membership and certificate trust are unsupported.
    for(const uint8_t tag:{uint8_t(2),uint8_t(0x31),uint8_t(0x30)}){if(at>=b.size()||b[at++]!=tag)return false;const auto field=length(at);if(field>b.size()-at||(tag==2&&!field))return false;at+=field;}
    for(const uint8_t tag:{uint8_t(0xA0),uint8_t(0xA1)})if(at<b.size()&&b[at]==tag){++at;const auto field=length(at);if(field>b.size()-at)return false;at+=field;}
    if(at>=b.size()||b[at++]!=0x31)return false;const auto signers=length(at);return signers==b.size()-at;
}
std::string Quote(const std::string& s){std::string out="\"";for(char c:s){if(c=='\\'||c=='"')out+='\\';out+=c;}return out+'"';}
}
DriverPackage ReadPackage(const std::filesystem::path& file,const std::string& nt,const std::string& device){
    DriverPackage p;p.nt=nt;p.device=device;p.inf=state::ReadFile(file,65536);Metadata(p);
    if(!p.catalog.empty()){p.catalogBytes=Source(file.parent_path(),p.catalog,4ull<<20);Require(CatalogEnvelope(p.catalogBytes),"CAT SignedData envelope rejected");p.requirements.push_back("catalog-trust-not-supported-by-lab-policy");}
    p.image=Source(file.parent_path(),p.binary,16ull<<20);Inspect(p);return p;
}
std::string DriverPackage::Compatibility(const Profile& p) const {
    Validate(p);if(machine!=0x8664)return "guest-architecture-mismatch";if(nt!=p.nt)return "requested-nt-mismatch";
    if(!requirements.empty())return requirements[0];
    if(p.nt!="win10"&&p.nt!="win11")return "inf-minimum-nt-not-met";
    if(!guardian::hardware::Equal(hardwareId.c_str(),guardian::driver::HardwareId)||device!="board")return "virtual-device-binding-not-supported";
    const std::set<std::string> available{"ntoskrnl.exe!IoCreateDevice","ntoskrnl.exe!IoDeleteDevice","ntoskrnl.exe!ExAllocatePoolWithTag","ntoskrnl.exe!ExFreePoolWithTag"};
    for(const auto& symbol:imports)if(!available.contains(symbol))return "unsupported-driver-import:"+symbol;
    return {};
}
std::vector<uint8_t> DriverPackage::Encode() const {
    state::Writer w;w.Pod(uint32_t(2));w.Text(nt);w.Text(device);w.Text(hardwareId);w.Text(service);w.Text(binary);w.Text(catalog);w.Pod(minimumMajor);w.Pod(minimumMinor);w.Bytes(inf);w.Bytes(image);w.Bytes(catalogBytes);return state::Seal(w.data);
}
DriverPackage DriverPackage::Decode(std::span<const uint8_t> bytes){
    auto data=state::Open(bytes);state::Reader r(data);Require(r.Pod<uint32_t>()==2,"INF driver package version rejected");DriverPackage p;
    p.nt=r.Text(16);p.device=r.Text(16);p.hardwareId=r.Text(128);p.service=r.Text(64);p.binary=r.Text(64);p.catalog=r.Text(64);p.minimumMajor=r.Pod<uint32_t>();p.minimumMinor=r.Pod<uint32_t>();
    p.inf=r.Bytes(65536);p.image=r.Bytes(16ull<<20);p.catalogBytes=r.Bytes(4ull<<20);r.End();
    const auto hardware=p.hardwareId,service=p.service,binary=p.binary,catalog=p.catalog;
    const auto major=p.minimumMajor,minor=p.minimumMinor;
    Metadata(p);
    Require(hardware==p.hardwareId&&service==p.service&&binary==p.binary&&catalog==p.catalog&&major==p.minimumMajor&&minor==p.minimumMinor,
        "Stored driver metadata differs from preserved INF");
    if(!p.catalog.empty()){Require(Leaf(p.catalog)&&CatalogEnvelope(p.catalogBytes),"Stored CAT envelope rejected");p.requirements.push_back("catalog-trust-not-supported-by-lab-policy");}
    else Require(p.catalogBytes.empty(),"Unexpected CAT content");
    Inspect(p);return p;
}
bool IsPackage(std::span<const uint8_t> bytes){const auto data=state::Open(bytes);state::Reader r(data);return r.Pod<uint32_t>()==2;}
std::string PackageJson(const DriverPackage& p,const Profile& profile,const guardian::driver::Report* report){
    const auto reason=p.Compatibility(profile);std::string out="{\"state\":"+Quote(report?(report->functional?"validated-and-unloaded":"guest-load-failed"):"imported")+",\"compatibility\":"+Quote(reason.empty()?"eligible-for-guest-lab-abi":"incompatible")+
        ",\"installed\":"+(report&&report->installed?std::string("true"):std::string("false"))+",\"initialized\":"+(report&&report->initialized?std::string("true"):std::string("false"))+
        ",\"functional\":"+(report&&report->functional?std::string("true"):std::string("false"))+",\"unloaded\":"+(report&&report->unloaded?std::string("true"):std::string("false"))+
        ",\"observedBoot\":"+std::to_string(report?report->bootId:0)+",\"guestError\":"+std::to_string(report?report->error:0)+",\"signature\":"+Quote(p.catalog.empty()?"unsigned-lab-driver":"catalog-unverified")+
        ",\"hardwareId\":"+Quote(p.hardwareId)+",\"service\":"+Quote(p.service)+",\"machine\":"+std::to_string(p.machine)+",\"reason\":"+Quote(reason)+",\"imports\":[";
    for(size_t n=0;n<p.imports.size();++n){if(n)out+=',';out+=Quote(p.imports[n]);}return out+"]}";
}
}
