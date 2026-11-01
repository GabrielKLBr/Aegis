#include "services/HashLookup.h"
#include "winui/Backend.h"
#include <fstream>
#include <iostream>
#include <functional>
#include <sddl.h>
using namespace av::services;
namespace {
Json tests=Json::array();
void Test(const char* name,const std::function<void()>& run){run();tests.push_back(name);}
template<class F> void Reject(F run){bool rejected=false;try{run();}catch(const std::exception&){rejected=true;}Check(rejected,"Expected operation to reject input");}
const std::string digest="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
}
int wmain(int argc,wchar_t** argv){
    try{
        Check(argc>=2,"Fresh output directory required");const fs::path out=fs::absolute(argv[1]);Check(!fs::exists(out),"Output directory must be fresh");fs::create_directories(out);
        const auto file=out/L"inert.txt";std::ofstream(file,std::ios::binary)<<"abc";
        if(argc==3&&std::wstring(argv[2])==L"--service"){
            av::ui::Backend backend(out/L"ui-state");
            Test("authenticated restricted worker hashes inert file",[&]{const auto result=backend.Request("reputation.lookup",{{"schema",1},{"source",Text(file.wstring())},{"online",false}});Check(result.at("sha256")==digest&&result.at("items").size()==2&&result.at("verdict")=="reputation_only","Service hash result");});
            Test("service rejects injected endpoint",[&]{Reject([&]{backend.Request("reputation.lookup",{{"schema",1},{"sha256",digest},{"online",true},{"url","https://example.org"}});});});
            Test("service rejects source and digest ambiguity",[&]{Reject([&]{backend.Request("reputation.lookup",{{"schema",1},{"sha256",digest},{"source",Text(file.wstring())}});});});
            Test("credential status exposes presence only",[&]{auto result=backend.Request("reputation.key.status",Json::object());Check(result.size()==1&&result.contains("configured"),"Credential status shape");});
            Test("service registrations available to UI",[&]{const auto result=backend.Request("services.get",Json::object());Check(result.at("items").size()==2&&result.at("items")[0].at("installed")==true,"Services status");});
            backend.Close();
        }else{
            Test("worker descriptor restricts process objects to SYSTEM and administrators",[]{PSECURITY_DESCRIPTOR sd=nullptr;Check(ConvertStringSecurityDescriptorToSecurityDescriptorW(WorkerObjectSecurity,SDDL_REVISION_1,&sd,nullptr),"Worker security descriptor");PACL acl=nullptr;BOOL present=FALSE,def=FALSE;GetSecurityDescriptorDacl(sd,&present,&acl,&def);Check(present&&acl&&acl->AceCount==2,"Worker DACL must contain two entries");for(DWORD i=0;i<acl->AceCount;++i){void* raw=nullptr;Check(GetAce(acl,i,&raw),"Read worker ACE");auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(raw);Check(IsWellKnownSid(&ace->SidStart,WinLocalSystemSid)||IsWellKnownSid(&ace->SidStart,WinBuiltinAdministratorsSid),"Unexpected worker object principal");}LocalFree(sd);});
            Test("installation descriptor grants standard users read and execute only",[]{PSECURITY_DESCRIPTOR sd=nullptr;Check(ConvertStringSecurityDescriptorToSecurityDescriptorW(InstallationSecurity,SDDL_REVISION_1,&sd,nullptr),"Installer security descriptor");PACL acl=nullptr;BOOL present=FALSE,def=FALSE;GetSecurityDescriptorDacl(sd,&present,&acl,&def);bool found=false;if(present&&acl)for(DWORD i=0;i<acl->AceCount;++i){void* raw=nullptr;Check(GetAce(acl,i,&raw),"Read installation ACE");const auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(raw);if(IsWellKnownSid(const_cast<DWORD*>(&ace->SidStart),WinBuiltinUsersSid)){found=true;Check((ace->Mask&(FILE_WRITE_DATA|FILE_APPEND_DATA|DELETE|WRITE_DAC|WRITE_OWNER))==0,"Users must not modify installed payload");}}LocalFree(sd);Check(found,"Read-only users ACE required");});
            const auto cache=out/L"cache.sqlite";int calls=0;
            const av::scan::Transport fake=[&](const av::scan::HttpRequest& r){++calls;Check(r.url=="https://hashlookup.circl.lu/lookup/sha256/"+digest&&r.method=="GET"&&r.body.empty()&&r.headerValue.empty(),"Only hash reaches transport");return av::scan::HttpResponse{200,Json({{"SHA-256",digest}}).dump(),{}};};
            auto q=Json{{"schema",1},{"sha256",digest},{"online",false}};
            Test("streamed SHA256 of inert bytes",[&]{Check(BoundedFileHash(file)==digest,"SHA256 mismatch");});
            Test("offline never contacts provider or creates cache",[&]{const auto result=LookupHash(q,cache,fake);Check(calls==0&&!fs::exists(cache)&&!result.at("providersComplete").get<bool>(),"Offline lookup");});
            q["online"]=true;
            Test("CIRCL context and missing MalwareBazaar key are distinct",[&]{auto result=LookupHash(q,cache,fake);Check(calls==1&&result.at("items")[0].at("status")=="found"&&result.at("items")[0].at("classification")=="unknown"&&result.at("items")[1].at("status")=="authentication_required"&&!result.at("providersComplete").get<bool>(),"Provider status");});
            Test("cache avoids repeated network query",[&]{auto result=LookupHash(q,cache,fake);Check(calls==1&&result.at("items")[0].at("cached")==true,"Cache result");});
            Test("uppercase digest normalized",[&]{auto upper=digest;for(auto& c:upper)if(c>='a'&&c<='f')c=char(c-'a'+'A');auto request=q;request["sha256"]=upper;Check(LookupHash(request,cache,fake).at("sha256")==digest,"Hash normalization");});
            Test("malformed digest rejected",[&]{auto request=q;request["sha256"]="not a hash";Reject([&]{LookupHash(request,cache,fake);});});
            Test("unapproved provider rejected",[&]{auto request=q;request["providers"]=Json::array({"custom"});Reject([&]{LookupHash(request,cache,fake);});});
            Test("duplicate provider rejected",[&]{auto request=q;request["providers"]=Json::array({"circl","circl"});Reject([&]{LookupHash(request,cache,fake);});});
            Test("custom URL rejected",[&]{auto request=q;request["url"]="https://localhost/";Reject([&]{LookupHash(request,cache,fake);});});
            Test("ambiguous input rejected",[&]{auto request=q;request["source"]=Text(file.wstring());Reject([&]{LookupHash(request,cache,fake);});});
            Test("directory rejected",[&]{Reject([&]{BoundedFileHash(out);});});
            Test("relative path rejected",[&]{Reject([&]{BoundedFileHash(L"inert.txt");});});
            Test("network path rejected",[&]{Reject([&]{BoundedFileHash(L"\\\\localhost\\C$\\test");});});
            Test("in-use writer prevents hashing changing file",[&]{Handle h(CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));Check(h.value!=INVALID_HANDLE_VALUE,"Open fixture writer");Reject([&]{BoundedFileHash(file);});});
            Test("oversize file rejected without reading",[&]{const auto big=out/L"sparse-limit.bin";Handle h(CreateFileW(big.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr));Check(h.value!=INVALID_HANDLE_VALUE,"Create size-limit fixture");LARGE_INTEGER size{};size.QuadPart=(1ll<<30)+1;Check(SetFilePointerEx(h.value,size,nullptr,FILE_BEGIN)&&SetEndOfFile(h.value),"Set fixture size");CloseHandle(h.release());Reject([&]{BoundedFileHash(big);});Check(fs::remove(big),"Remove owned size-limit fixture");});
            auto mb=Json{{"schema",1},{"sha256",digest},{"online",true},{"providers",Json::array({"malwarebazaar"})}};
            Test("MalwareBazaar match requires matching returned hash",[&]{const auto result=LookupHash(mb,out/L"mb.sqlite",[&](const av::scan::HttpRequest& r){Check(r.headerName=="Auth-Key"&&r.headerValue=="fixture-key"&&r.body=="query=get_info&hash="+digest,"MalwareBazaar request");return av::scan::HttpResponse{200,Json({{"query_status","ok"},{"data",Json::array({Json{{"sha256_hash",digest}}})}}).dump(),{}};},"fixture-key");Check(result.at("items")[0].at("classification")=="suspicious","MB match");});
            Test("MalwareBazaar miss remains unknown",[&]{const auto result=LookupHash(mb,out/L"miss.sqlite",[](const auto&){return av::scan::HttpResponse{200,"{\"query_status\":\"hash_not_found\"}",{}};},"fixture-key");Check(result.at("items")[0].at("status")=="not_found"&&result.at("items")[0].at("classification")=="unknown","MB miss");});
            Test("provider secrets are excluded from transport failure result",[&]{const auto result=LookupHash(mb,out/L"failure.sqlite",[](const auto&)->av::scan::HttpResponse{throw std::runtime_error("fixture-secret");},"fixture-secret");Check(result.at("items")[0].at("status")=="unavailable"&&result.dump().find("fixture-secret")==std::string::npos,"Error redaction");});
            Test("mismatched provider digest rejected",[&]{Reject([&]{av::scan::ProviderResponse("circl",digest,{200,"{\"SHA-256\":\"wrong\"}",{}});});});
            Test("deep provider JSON rejected",[&]{std::string body="{\"a\":";body+=std::string(40,'[')+"0"+std::string(40,']')+"}";Reject([&]{av::scan::ProviderResponse("circl",digest,{200,body,{}});});});
            Test("header injection and NUL keys rejected",[&]{Reject([&]{av::scan::ProviderRequest("malwarebazaar",digest,"a\r\nb");});Reject([&]{av::scan::ProviderRequest("malwarebazaar",digest,std::string("a\0b",3));});});
            Test("arbitrary network endpoint rejected before connection",[&]{Reject([&]{av::scan::Https({"https://example.org/","GET",{},{},{}});});});
            Test("rate limit persists across hash queries",[&]{const auto db=out/L"rate.sqlite";auto request=q;request["providers"]=Json::array({"circl"});int attempts=0;for(int i=0;i<7;++i){auto h=digest;h[0]=char('0'+i);request["sha256"]=h;auto result=LookupHash(request,db,[&](const auto&){++attempts;return av::scan::HttpResponse{404,"{}",{}};});if(i==6)Check(result.at("items")[0].at("status")=="rate_limited","Rate status");}Check(attempts==6,"Rate transport limit");});
            Test("protected credential roundtrip using isolated test target",[&]{const auto target=L"Aegis.Test.Reputation."+std::to_wstring(GetCurrentProcessId());std::string key="inert-fixture-key";try{SaveReputationKey(key,target.c_str());Check(key.empty(),"Input key wiped");Check(ReputationKey(target.c_str())=="inert-fixture-key","Credential roundtrip");RemoveReputationKey(target.c_str());Check(ReputationKey(target.c_str()).empty(),"Credential removed");}catch(...){RemoveReputationKey(target.c_str());throw;}});
        }
        const Json result{{"status","PASS"},{"tests",tests.size()},{"cases",tests}};std::ofstream(out/L"results.json")<<result.dump(2);std::cout<<result.dump()<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
