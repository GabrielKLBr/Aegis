// Phase 17 client/embedded-language integration. Fixtures are fresh inert data only.
#include "winui/Backend.h"
#include <aegis/localization.hpp>
#include <fstream>
#include <objbase.h>
#include <iostream>
#include <chrono>
#include <thread>
using namespace av::services;
namespace {
Json rows=Json::array();fs::path output;
void Record(const char* name,bool ok){rows.push_back({{"case",name},{"passed",ok}});std::ofstream(output/L"results.json")<<Json{{"tests",rows.size()},{"cases",rows}}.dump(2);Check(ok,name);}
void Denied(const char* name,const std::function<void()>& fn){bool denied=false;try{fn();}catch(...){denied=true;}Record(name,denied);}
void Put(const fs::path& p,const std::string& s){std::ofstream(p,std::ios::binary)<<s;}
Json Wait(av::ui::Backend& b){const auto deadline=GetTickCount64()+30000;for(;;){auto s=b.Request("scan.status",Json::object());if(!s.value("running",false))return s;Check(GetTickCount64()<deadline,"UI scan deadline");Sleep(50);}}
}
int wmain(int argc,wchar_t** argv){
    try{
        Check(argc>=2,"Fresh evidence folder required");output=fs::absolute(argv[1]);Check(!fs::exists(output),"Use a fresh evidence directory");fs::create_directories(output);CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        auto backend=std::make_shared<av::ui::Backend>(output/L"state");auto& b=*backend;
        if(argc>=3&&std::wstring(argv[2])==L"--admin"){
            const auto baseline=b.Request("status",Json::object()).at("policy");std::string id;
            auto restore=[&]{const auto input=output/L"restore-policy.json";Put(input,baseline.dump());Call(Kind::Antivirus,Operation::ProtectionConfigure,input);if(!id.empty())b.Request("quarantine.delete",{{"id",id}});};
            try{
                fs::create_directory(output/L"watch");auto policy=b.Request("protection.set",{{"enabled",false},{"roots",Json::array({Text((output/L"watch").wstring())})},{"exclusions",Json::array()},{"autoQuarantine",false},{"maxFiles",123},{"maxSeconds",60},{"uiScheduleSeconds",3600}});
                Record("admin-ui-policy-fields",policy.at("policy").at("maxFiles")==123&&policy.at("policy").at("maxSeconds")==60);
                bool schedule=false;for(const auto& v:policy.at("policy").at("schedules"))if(v.value("id",std::string())=="winui17-quick")schedule=v.at("intervalSeconds")==3600;Record("admin-ui-schedule-persists",schedule);
                const auto file=output.parent_path()/(output.filename().wstring()+L"-source.txt");const auto dest=output/L"restored.txt";Put(file,"Inert phase 17 quarantine fixture.");auto added=b.Request("quarantine.add",{{"path",Text(file.wstring())}});id=added.at("id");Record("ui-quarantine-removes-verified-original",!fs::exists(file));
                const auto restored=b.Request("quarantine.restore",{{"id",id},{"destination",Text(dest.wstring())}});std::ifstream input(dest);std::string bytes((std::istreambuf_iterator<char>(input)),{});input.close();Record("ui-restores-exact-bytes",restored.value("copyRestored",false)&&bytes=="Inert phase 17 quarantine fixture.");
                Denied("ui-restore-refuses-collision",[&]{b.Request("quarantine.restore",{{"id",id},{"destination",Text(dest.wstring())}});});
                b.Request("quarantine.delete",{{"id",id}});id.clear();Record("ui-vault-erase-retains-restored-file",fs::exists(dest));
                restore();Record("original-policy-restored",b.Request("status",Json::object()).at("policy")==baseline);
                Json result{{"status","PASS"},{"tests",rows.size()},{"cases",rows}};std::ofstream(output/L"results.json")<<result.dump(2);std::cout<<result.dump()<<"\n";CoUninitialize();return 0;
            }catch(...){restore();throw;}
        }
        for(const auto* locale:{"en-US","pt-BR","es-ES"}){
            aegis::Localization l(locale);Record((std::string("embedded-")+locale).c_str(),l.text("nav.dashboard")!="nav.dashboard"&&l.languages().size()==3);
            Record((std::string("plural-")+locale).c_str(),l.plural("scan.files",1)!=l.plural("scan.files",2));Record((std::string("regional-")+locale).c_str(),l.plural("scan.files",10000).find("10")!=std::string::npos&&!l.timestamp(1793556000).empty());
            Record((std::string("parameters-")+locale).c_str(),l.format("scan.completed",{{"count","123"}}).find("123")!=std::string::npos);
        }
        aegis::Localization fallback("unsupported");Record("unknown-locale-falls-back",fallback.locale()=="en-US");Denied("missing-parameter-rejected",[&]{fallback.format("scan.completed");});
        const auto exported=output/L"exported.json";av::ui::Backend::Export(exported,{{"marker","inert"}});Record("report-export-created",fs::exists(exported));Denied("report-export-never-overwrites",[&]{av::ui::Backend::Export(exported,{{"marker","different"}});});
        auto prefs=b.Request("settings.get",Json::object());b.Request("settings.set",{{"locale","es-ES"},{"theme","dark"}});
        {av::ui::Backend reopened(output/L"state");Record("preferences-survive-reopen",reopened.Request("settings.get",Json::object()).at("locale")=="es-ES");}
        int invalidCase=0;for(auto invalid:{Json{{"locale","xx-XX"}},Json{{"theme","invalid"}},Json{{"theme",1}},Json{{"offline",false}}})Denied(("invalid-preference-"+std::to_string(++invalidCase)).c_str(),[&]{b.Request("settings.set",invalid);});
        Record("invalid-preferences-atomic",b.Request("settings.get",Json::object()).at("theme")=="dark");
        Denied("unknown-method-rejected",[&]{b.Request("arbitrary.host.command",Json::object());});Denied("invalid-parameter-envelope",[&]{b.Request("status",Json::array());});Denied("invalid-service-kind",[&]{b.Request("services.set",{{"kind",99},{"running",true}});});
        const auto initial=b.Request("status",Json::object());Record("real-protected-service",initial.at("protectedVault").get<bool>()&&initial.at("servicePid").get<DWORD>()==Query(Kind::Antivirus).process);
        Record("real-service-controls",b.Request("services.get",Json::object()).at("items").size()==2);
        Denied("ordinary-owner-cannot-stop-antivirus",[&]{b.Request("services.set",{{"kind",1},{"running",false}});});
        Denied("ordinary-owner-cannot-change-policy",[&]{b.Request("protection.set",{{"enabled",false},{"roots",initial.at("policy").at("roots")},{"exclusions",initial.at("policy").at("exclusions")}});});
        Record("denied-policy-unchanged",b.Request("status",Json::object()).at("policy")==initial.at("policy"));
        fs::create_directory(output/L"files");Put(output/L"files/ordinary.txt","Inert phase 17 text.");Put(output/L"files/marker.txt","AEGIS_SCANNER_TEST_POSITIVE_V1\nInert phase 17 fixture.\n");
        b.Request("scan.start",{{"mode","custom"},{"path",Text((output/L"files").wstring())}});auto scan=Wait(b);Record("scan-service-result",scan.value("complete",false)&&scan.value("filesScanned",0)==2&&scan.value("threatsFound",0)==1);
        Record("scan-does-not-execute-or-isolate",fs::exists(output/L"files/marker.txt"));
        {av::ui::Backend reopened(output/L"state");Record("scan-id-survives-ui-reopen",reopened.Request("scan.status",Json::object()).at("id")==scan.at("id"));}
        Record("history-from-service",!b.Request("history",Json::object()).at("items").empty());Record("persistent-notifications",b.Request("events",Json::object()).contains("next"));
        Record("protected-quarantine-list",b.Request("quarantine.list",Json::object()).contains("entries"));Record("quarantine-audit",b.Request("quarantine.audit",Json::object()).contains("events"));
        Denied("quarantine-delete-needs-admin",[&]{b.Request("quarantine.delete",{{"id","not-a-real-entry"}});});
        Denied("restore-needs-admin",[&]{b.Request("quarantine.restore",{{"id","not-a-real-entry"},{"destination",Text((output/L"new.bin").wstring())}});});
        b.Request("scan.start",{{"mode","full"}});scan=Wait(b);Record("full-scope-reports-coverage",scan.contains("inconclusive")&&scan.contains("complete"));
        fs::create_directory(output/L"cancel");for(int i=0;i<512;++i)Put(output/L"cancel"/(std::to_wstring(i)+L".txt"),std::string(8192,'x'));
        b.Request("scan.start",{{"mode","custom"},{"path",Text((output/L"cancel").wstring())}});b.Request("scan.cancel",Json::object());scan=Wait(b);Record("manual-scan-cancelled",scan.value("state",std::string())=="cancelled"&&!scan.value("complete",true));
        Denied("invalid-scan-mode",[&]{b.Request("scan.start",{{"mode","invalid"}});});
        Denied("network-input-rejected",[&]{b.Request("scan.start",{{"mode","custom"},{"path",R"(\\server\share)"}});});
        if(argc>=3){const bool wasRunning=Query(Kind::Sandbox).Running();
            b.Request("services.set",{{"kind",2},{"running",true}});Record("start-sandbox-from-client",Query(Kind::Sandbox).Running());
            b.Request("session.start",{{"path",Text(fs::absolute(argv[2]).wstring())}});
            const auto deadline=GetTickCount64()+45000;Json session;do{session=b.Request("session.status",Json::object());if(!session.value("running",false))break;Check(GetTickCount64()<deadline,"Session deadline");Sleep(100);}while(true);
            Record("real-minint-session-report",session.contains("report")&&session.at("report").value("ran",false));
            Record("vm-session-controls-reflect-report",b.Request("vm.list",Json::object()).at("items")[0].at("id")==session.at("id"));
            Record("behavior-report-from-session",b.Request("behavior",Json::object()).at("report").contains("events"));
            {av::ui::Backend reopened(output/L"state");Record("session-summary-survives-reopen",reopened.Request("session.status",Json::object()).at("id")==session.at("id"));}
            b.Request("session.start",{{"path",Text(fs::absolute(argv[2]).wstring())}});b.Request("session.cancel",Json::object());while(b.Request("session.status",Json::object()).value("running",false))Sleep(50);Record("sandbox-cancellation-observable",b.Request("session.status",Json::object()).value("state",std::string())=="cancelled");
            if(!wasRunning)b.Request("services.set",{{"kind",2},{"running",false}});
        }
        b.Request("settings.set",{{"locale",prefs.at("locale")},{"theme",prefs.at("theme")}});b.Close();Denied("close-cancels-further-client-work",[&]{b.Request("status",Json::object());});
        Json result{{"status","PASS"},{"tests",rows.size()},{"cases",rows}};std::ofstream(output/L"results.json")<<result.dump(2);std::cout<<result.dump()<<"\n";CoUninitialize();return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
