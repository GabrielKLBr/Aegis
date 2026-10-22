#include "Profile.h"
#include "Package.h"
#include "guardian/HardwareProjection.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <random>
#ifdef _WIN32
#include <Windows.h>
#include <setupapi.h>
#else
#include <cpuid.h>
#endif
namespace av::microvm::hardware {
namespace {
using state::Require;
struct Nt { const char* name; uint32_t major, minor, build; };
constexpr Nt targets[]{{"xp",5,1,2600},{"vista",6,0,6002},{"win7",6,1,7601},
    {"win8",6,2,9200},{"win81",6,3,9600},{"win10",10,0,19045},{"win11",10,0,22631}};
const Nt& Target(const std::string& text) {
    for (const auto& t : targets) if (text == t.name) return t;
    throw std::runtime_error("Unsupported NT target name");
}
bool Printable(const std::string& s, size_t limit) {
    return !s.empty() && s.size() <= limit && std::all_of(s.begin(), s.end(), [](unsigned char c){return c >= 32 && c <= 126;});
}
std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) { if(c == '\\' || c == '"') out += '\\'; out += c; }
    return out + '"';
}
std::string Clean(std::string s) {
    for (char& c : s) if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) c = '?';
    while (!s.empty() && s.back() == ' ') s.pop_back();
    const auto first = s.find_first_not_of(' ');
    if (first != s.npos) s.erase(0, first);
    s.resize(std::min<size_t>(s.size(),95)); return s;
}
#ifdef _WIN32
std::string RegistryModel(const wchar_t* key, const wchar_t* name) {
    wchar_t data[256]{}; DWORD bytes = sizeof(data);
    if (RegGetValueW(HKEY_LOCAL_MACHINE,key,name,RRF_RT_REG_SZ,nullptr,data,&bytes) != ERROR_SUCCESS) return {};
    std::string s;
    for (wchar_t c : data) { if (!c) break; s += c >= 32 && c <= 126 ? char(c) : '?'; }
    return Clean(s);
}
#else
std::string FirstLine(const std::filesystem::path& file) {
    std::ifstream stream(file); std::string s; std::getline(stream,s); return Clean(s);
}
#endif
template<size_t N> void Copy(char (&to)[N], const std::string& from) {
    Require(from.size() < N, "Hardware field too long");
    std::memcpy(to, from.data(), from.size());
}
uint16_t U16(std::span<const uint8_t> b, size_t p) {
    Require(p <= b.size() && b.size()-p >= 2,"Truncated driver PE");
    return uint16_t(b[p] | (uint16_t(b[p+1]) << 8));
}
uint32_t U32(std::span<const uint8_t> b, size_t p) {
    return uint32_t(U16(b,p)) | (uint32_t(U16(b,p+2)) << 16);
}
uint32_t DriverMachine(std::span<const uint8_t> b) {
    Require(b.size() >= 64 && U16(b,0) == 0x5A4D,"Driver is not a PE file");
    const auto offset = size_t(U32(b,0x3C));
    Require(offset <= b.size() && b.size()-offset >= 96,"Driver PE header outside file");
    Require(U32(b,offset) == 0x4550,"Driver PE signature rejected");
    const auto machine = U16(b,offset+4), magic = U16(b,offset+24);
    Require((machine == 0x8664 && magic == 0x20B) || (machine == 0x14C && magic == 0x10B),"Driver architecture unsupported");
    const auto opt = U16(b,offset+20);
    Require(opt >= 72 && opt <= b.size()-offset-24,"Driver optional header rejected");
    Require(U16(b,offset+24+68) == 1,"Expected a native-subsystem Windows driver");
    const auto sections = U16(b,offset+6);
    Require(sections && sections <= 96 && size_t(sections)*40 <= b.size()-offset-24-opt,"Driver section table rejected");
    for (size_t n = 0; n < sections; ++n) {
        const auto section = offset+24+opt+n*40;
        const size_t size = U32(b,section+16), start = U32(b,section+20);
        Require(start <= b.size() && size <= b.size()-start,"Driver section outside file");
    }
    return machine;
}
bool DeviceName(const std::string& s) {return s=="cpu"||s=="graphics"||s=="disk"||s=="bios"||s=="board";}
}
void Validate(const Profile& p) {
    (void)Target(p.nt);
    Require(p.identity.size() == 32 && p.identity.find_first_not_of("0123456789abcdef") == p.identity.npos,
        "Hardware identity must be a generated 128-bit token");
    for (const auto& s : p.models) Require(Printable(s,95),"Hardware model must contain 1..95 printable ASCII characters");
}
Profile HostReference() {
    Profile p; std::random_device random; constexpr char hex[]="0123456789abcdef";
    for(unsigned n=0;n<16;++n){const auto v=static_cast<unsigned char>(random());p.identity+=hex[v>>4];p.identity+=hex[v&15];}
#ifdef _WIN32
    const auto cpu = RegistryModel(L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",L"ProcessorNameString");
    const auto bios = RegistryModel(L"HARDWARE\\DESCRIPTION\\System\\BIOS",L"BIOSVersion");
    const auto board = RegistryModel(L"HARDWARE\\DESCRIPTION\\System\\BIOS",L"BaseBoardProduct");
    if(!cpu.empty()) p.models[0]=cpu;
    if(!bios.empty()) p.models[3]=bios;
    if(!board.empty()) p.models[4]=board;
    DISPLAY_DEVICEW display{}; display.cb=sizeof(display);
    if(EnumDisplayDevicesW(nullptr,0,&display,0)) {
        std::string s; for(wchar_t c:display.DeviceString){if(!c)break;s+=c>=32&&c<=126?char(c):'?';}
        if(!s.empty())p.models[1]=Clean(s);
    }
    // Only the class description is read. Device instance paths, serials and
    // firmware UUIDs are intentionally never collected.
    const GUID diskClass{0x4d36e967,0xe325,0x11ce,{0xbf,0xc1,0x08,0x00,0x2b,0xe1,0x03,0x18}};
    const auto devices=SetupDiGetClassDevsW(&diskClass,nullptr,nullptr,DIGCF_PRESENT);
    if(devices!=INVALID_HANDLE_VALUE){
        SP_DEVINFO_DATA device{};device.cbSize=sizeof(device);wchar_t name[256]{};
        if(SetupDiEnumDeviceInfo(devices,0,&device)&&SetupDiGetDeviceRegistryPropertyW(devices,&device,SPDRP_FRIENDLYNAME,
            nullptr,reinterpret_cast<BYTE*>(name),sizeof(name),nullptr)){
            std::string model;for(wchar_t c:name){if(!c)break;model+=c>=32&&c<=126?char(c):'?';}
            if(!model.empty())p.models[2]=Clean(model);
        }
        SetupDiDestroyDeviceInfoList(devices);
    }
#else
    std::ifstream cpu("/proc/cpuinfo"); std::string line;
    while(std::getline(cpu,line)) if(line.rfind("model name",0)==0){const auto colon=line.find(':');if(colon!=line.npos)p.models[0]=Clean(line.substr(colon+1));break;}
    const auto bios=FirstLine("/sys/class/dmi/id/bios_version"), board=FirstLine("/sys/class/dmi/id/board_name");
    if(!bios.empty())p.models[3]=bios;
    if(!board.empty())p.models[4]=board;
    std::error_code error;
    size_t count=0;
    for(const auto& block:std::filesystem::directory_iterator("/sys/block",error)){
        if(++count>64)break;
        const auto model=FirstLine(block.path()/"device/model");
        if(!model.empty()){p.models[2]=model;break;}
    }
#endif
    Validate(p); return p;
}
std::vector<uint8_t> Encode(const Profile& p) {
    Validate(p); state::Writer w; w.Pod(uint32_t(1));w.Text(p.nt);w.Text(p.identity);
    for(const auto& s:p.models)w.Text(s);
    return state::Seal(w.data);
}
Profile Decode(std::span<const uint8_t> bytes) {
    auto data=state::Open(bytes);state::Reader r(data);Require(r.Pod<uint32_t>()==1,"Hardware profile version rejected");
    Profile p;p.nt=r.Text(16);p.identity=r.Text(32);for(auto& s:p.models)s=r.Text(95);r.End();Validate(p);return p;
}
guardian::hardware::Info Inventory(const Profile& p,uint64_t ram,const std::string& contract) {
    Validate(p);Require(ram >= (16ull<<20) && ram <= (1ull<<30),"Hardware RAM outside supported limits");
    using namespace guardian::hardware;
    Info out{};out.magic=Magic;out.version=Version;out.bytes=sizeof(out);const auto& nt=Target(p.nt);
    out.ntMajor=nt.major;out.ntMinor=nt.minor;out.ntBuild=nt.build;out.count=Count;out.ramBytes=ram;out.vcpus=1;
    const char* effective[]{"Host x64 instruction subset; one virtual CPU", "Guardian BGRA software compositor",
        "Guardian 64 KiB virtual block device", "Guardian direct boot v1; no PC BIOS ABI", "Guardian paravirtual board v1"};
    for(uint32_t n=0;n<Count;++n){auto& d=out.devices[n];d.kind=static_cast<Kind>(n+1);d.support=Support::MetadataOnly;
        if(n==0)d.support=Support::Partial;
        if((n==1&&contract=="graphics")||(n==2&&contract=="storage"))d.support=Support::Functional;
        Copy(d.requestedModel,p.models[n]);Copy(d.effectiveModel,effective[n]);Copy(d.syntheticId,"gv-"+p.identity+"-"+std::to_string(n+1));}
    std::memset(out.devices[0].effectiveModel,0,sizeof(out.devices[0].effectiveModel));Copy(out.devices[0].effectiveModel,p.models[0].substr(0,48));
    for(uint32_t n=3;n<5;++n){std::memset(out.devices[n].effectiveModel,0,sizeof(out.devices[n].effectiveModel));Copy(out.devices[n].effectiveModel,p.models[n]);out.devices[n].support=Support::Partial;}
    if(contract=="hardware")out.devices[4].support=Support::Functional;
    Require(Valid(out,ram),"Generated hardware inventory rejected");return out;
}
std::string Json(const guardian::hardware::Info& info) {
    Require(guardian::hardware::Valid(info,info.ramBytes),"Invalid hardware inventory");
    std::string out="{\"schema\":1,\"targetNt\":"+Quote(std::to_string(info.ntMajor)+"."+std::to_string(info.ntMinor)+"."+std::to_string(info.ntBuild))+
        ",\"windowsCompatibility\":\"unsupported\",\"ramBytes\":"+std::to_string(info.ramBytes)+",\"vcpus\":1,\"devices\":[";
    for(uint32_t n=0;n<info.count;++n){const auto& d=info.devices[n];if(n)out+=',';
        const char* support=d.support==guardian::hardware::Support::Functional?"functional":d.support==guardian::hardware::Support::Partial?"partial":"metadata-only";
        out+="{\"kind\":"+std::to_string(static_cast<uint32_t>(d.kind))+",\"requestedModel\":"+Quote(d.requestedModel)+
            ",\"effectiveModel\":"+Quote(d.effectiveModel)+",\"syntheticId\":"+Quote(d.syntheticId)+",\"support\":"+Quote(support)+"}";}
    return out+"]}";
}
std::vector<uint8_t> ImportDriver(const Profile&,const std::filesystem::path& file,const std::string& nt,const std::string& device) {
    (void)Target(nt);Require(DeviceName(device),"Unknown driver device class");
    auto extension=file.extension().string();for(auto& c:extension)c=guardian::hardware::Fold(c);
    if(extension==".inf")return ReadPackage(file,nt,device).Encode();
    const auto image=state::ReadFile(file,16ull<<20);const auto machine=DriverMachine(image);
    state::Writer w;w.Pod(uint32_t(1));w.Text(nt);w.Text(device);w.Pod(machine);w.Bytes(image);return state::Seal(w.data);
}
std::string DriverJson(std::span<const uint8_t> bytes,const Profile& p) {
    if(IsPackage(bytes))return PackageJson(DriverPackage::Decode(bytes),p);
    Validate(p);auto data=state::Open(bytes);state::Reader r(data);Require(r.Pod<uint32_t>()==1,"Driver bundle version rejected");
    const auto nt=r.Text(16),device=r.Text(16);const auto machine=r.Pod<uint32_t>();const auto image=r.Bytes(16ull<<20);r.End();
    (void)Target(nt);Require(DeviceName(device)&&machine==DriverMachine(image),"Driver metadata/image mismatch");
    const char* reason=machine!=0x8664?"guest-architecture-mismatch":nt!=p.nt?"requested-nt-mismatch":"windows-kernel-driver-abi-not-implemented";
    return "{\"state\":\"imported\",\"compatibility\":\"incompatible\",\"installed\":false,\"initialized\":false,\"functional\":false,\"targetNt\":"+
        Quote(nt)+",\"device\":"+Quote(device)+",\"machine\":"+std::to_string(machine)+",\"imageBytes\":"+std::to_string(image.size())+",\"reason\":"+Quote(reason)+"}";
}
}
