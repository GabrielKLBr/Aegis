#include "common/BoundedJson.h"
#include "HashLookup.h"
#include <bcrypt.h>
#include <shlobj.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <set>
#include <memory>
namespace av::services {
namespace {
void Ordinary(const fs::path& path) {
    Check(path.is_absolute()&&path.wstring().find(L'\0')==std::wstring::npos,"Absolute local file required");
    const auto root=path.root_name().wstring();Check(root.size()==2&&root[1]==L':',"Network and device paths are not supported");
    const auto drive=GetDriveTypeW(path.root_path().c_str());Check(drive==DRIVE_FIXED||drive==DRIVE_REMOVABLE||drive==DRIVE_RAMDISK,"Local drive required");
    fs::path at;
    for(const auto& part:path){at/=part;const auto a=GetFileAttributesW(at.c_str());Check(a!=INVALID_FILE_ATTRIBUTES&&!(a&FILE_ATTRIBUTE_REPARSE_POINT),"Missing path or reparse point rejected");}
}
std::string Read(const fs::path& path) {
    Ordinary(path);Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    Check(file.value!=INVALID_HANDLE_VALUE,"Open lookup request");BY_HANDLE_FILE_INFORMATION info{};
    Check(GetFileInformationByHandle(file.value,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&info.nFileSizeHigh==0&&info.nFileSizeLow<=65536,"Lookup request exceeds 64 KiB");
    std::string data(info.nFileSizeLow,'\0');DWORD n=0;Check(ReadFile(file.value,data.data(),DWORD(data.size()),&n,nullptr)&&n==data.size(),"Read lookup request");return data;
}
std::string Normalize(std::string hash) {
    for(auto& c:hash)if(c>='A'&&c<='F')c=char(c-'A'+'a');
    Check(hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==hash.npos,"Enter a SHA-256 hash with 64 hexadecimal characters");return hash;
}
}
std::string BoundedFileHash(const fs::path& path) {
    Ordinary(path);Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
    Check(file.value!=INVALID_HANDLE_VALUE,"Open file for hashing");BY_HANDLE_FILE_INFORMATION before{};
    Check(GetFileInformationByHandle(file.value,&before)&&!(before.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&before.nFileSizeHigh==0&&before.nFileSizeLow<=1u<<30,"Hashing supports regular local files up to 1 GiB");
    struct Algorithm {BCRYPT_ALG_HANDLE h=nullptr;~Algorithm(){if(h)BCryptCloseAlgorithmProvider(h,0);}} algorithm;
    struct Hash {BCRYPT_HASH_HANDLE h=nullptr;~Hash(){if(h)BCryptDestroyHash(h);}} hash;
    Check(BCryptOpenAlgorithmProvider(&algorithm.h,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"Open SHA-256");
    Check(BCryptCreateHash(algorithm.h,&hash.h,nullptr,0,nullptr,0,0)>=0,"Create SHA-256");
    const auto deadline=GetTickCount64()+30000;std::array<BYTE,65536> buffer{};uint64_t total=0;
    for(;;){Check(GetTickCount64()<deadline,"Hashing deadline exceeded");DWORD n=0;Check(ReadFile(file.value,buffer.data(),DWORD(buffer.size()),&n,nullptr),"Read file for hashing");if(!n)break;total+=n;Check(total<=1u<<30,"Hash size limit");Check(BCryptHashData(hash.h,buffer.data(),n,0)>=0,"Hash file bytes");}
    Check(total==before.nFileSizeLow,"File changed during hashing");std::array<BYTE,32> digest{};Check(BCryptFinishHash(hash.h,digest.data(),DWORD(digest.size()),0)>=0,"Finish SHA-256");
    std::string text;for(const auto b:digest){text+="0123456789abcdef"[b>>4];text+="0123456789abcdef"[b&15];}return text;
}
Json LookupHash(const Json& request,const fs::path& cache,const scan::Transport& transport,std::string key) {
    struct Wipe {std::string& key;~Wipe(){if(!key.empty())SecureZeroMemory(key.data(),key.size());}} wipe{key};
    Check(request.is_object()&&request.value("schema",0)==1,"Hash request schema rejected");
    for(const auto& [k,v]:request.items()){(void)v;Check(k=="schema"||k=="sha256"||k=="source"||k=="online"||k=="providers","Unsupported hash lookup parameter");}
    Check(request.contains("sha256")!=request.contains("source"),"Choose a file or enter a SHA-256 hash");
    const auto hash=request.contains("sha256")?Normalize(request.at("sha256").get<std::string>()):BoundedFileHash(Wide(request.at("source").get<std::string>()));
    const auto providers=request.value("providers",Json::array({"circl","malwarebazaar"}));
    Check(providers.is_array()&&!providers.empty()&&providers.size()<=2,"Select one or two supported providers");std::set<std::string> selected;
    for(const auto& p:providers){const auto id=p.get<std::string>();Check((id=="circl"||id=="malwarebazaar")&&selected.insert(id).second,"Provider selection rejected");}
    const bool online=request.value("online",false);Json rows=Json::array();bool available=true;
    // A cache hit is evidence from a provider, never a local clean verdict.
    std::unique_ptr<scan::Store> store;if(online){Ordinary(cache.parent_path());store=std::make_unique<scan::Store>(cache);}
    const auto now=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for(const auto& id:selected){Json row{{"provider",id},{"sha256",hash},{"status","disabled"},{"classification","unknown"}};
        if(online){
            if(id=="malwarebazaar"&&key.empty()){row["status"]="authentication_required";available=false;}
            else try {
                row=store->Cached("ui-reputation-"+id,hash,now);
                if(!row.is_null()&&!row.empty())row["cached"]=true;
                else if(!store->Admit("ui-"+id,now,6,100)){row={{"provider",id},{"sha256",hash},{"status","rate_limited"},{"classification","unknown"}};available=false;}
                else {
                    const auto response=transport(scan::ProviderRequest(id,hash,id=="malwarebazaar"?key:std::string()));
                    if(response.status==429){store->Backoff("ui-"+id,now+60);throw std::runtime_error("Provider rate limited");}
                    row=scan::ProviderResponse(id,hash,response);if(row.at("status")=="unknown")row["status"]="not_found";
                    store->Cache("ui-reputation-"+id,hash,row,now,row.at("status")=="found"?21600:900);
                }
            }catch(...){row={{"provider",id},{"sha256",hash},{"status","unavailable"},{"classification","unknown"}};available=false;}
        }rows.push_back(std::move(row));
    }
    return {{"schema",1},{"complete",true},{"online",online},{"providersComplete",online&&available},{"sha256",hash},{"items",rows},{"verdict","reputation_only"},{"notice","Database matches are supporting evidence. No match or unavailable service does not establish that a file is safe."}};
}
Json HashLookupFile(const fs::path& request) {
    const auto data=Read(request);const auto input=aegis::security::ParseJson(data,65536);
    PWSTR local=nullptr;Check(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)),"Locate reputation cache");const fs::path root=fs::path(local)/L"AegisReputation17";CoTaskMemFree(local);fs::create_directory(root);Ordinary(root);
    std::string key;
    // Credential access is unnecessary for local-only hashing or CIRCL queries.
    const auto providers=input.value("providers",Json::array({"circl","malwarebazaar"}));
    if(input.value("online",false)&&providers.is_array()&&std::find(providers.begin(),providers.end(),Json("malwarebazaar"))!=providers.end())key=ReputationKey();
    return LookupHash(input,root/L"cache.sqlite",scan::Https,std::move(key));
}
} // namespace av::services
