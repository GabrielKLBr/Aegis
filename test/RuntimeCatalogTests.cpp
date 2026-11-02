#include "services/RuntimeCatalog.h"
#include "distribution/Distribution.h"
#include "guardian/NtCompatibility.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>

namespace d = aegis::distribution;
using av::services::RefreshRuntimeHelpers;
namespace {
std::string Digest(const std::string& bytes) { return d::Hash({reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()}); }
void Put(const d::fs::path& file, const std::string& bytes) {
    d::fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary); out.write(bytes.data(), std::streamsize(bytes.size()));
    d::Require(out.good(), "Fixture write");
}
d::Json Load(const d::fs::path& file) { return d::Json::parse(d::Read(file)); }
std::map<std::string,std::string> Snapshot(const d::fs::path& root) {
    std::map<std::string,std::string> out;
    for (const auto& file : d::fs::recursive_directory_iterator(root))
        if (file.is_regular_file()) out[file.path().lexically_relative(root).generic_string()] = d::HashFile(file.path());
    return out;
}
template<class F> void Rejected(F action) {
    bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; }
    d::Require(rejected, "Invalid retained runtime was migrated");
}
struct Fixture {
    d::fs::path root, helpers;
    std::string app = std::string(64, 'a');
    explicit Fixture(const d::fs::path& path) : root(path/L"runtime"), helpers(path/L"engines") {
        Put(helpers/L"guardian-native-user-host.exe", "INERT_TRUSTED_NEW_HELPER");
        Add(app);
    }
    void Add(const std::string& key) {
        auto index = d::fs::exists(root/L"index.json") ? Load(root/L"index.json") :
            d::Json{{"schema",1},{"phase","14.13"},{"applications",d::Json::object()},
                    {"automaticTemplates",d::Json::array({app})}};
        const auto prefix = key + "/";
        Put(root/d::Wide(prefix+"guardian-native-user-host.exe"), "INERT_OLD_HELPER");
        Put(root/d::Wide(prefix+"kernel.gk"), "INERT_KERNEL");
        Put(root/d::Wide(prefix+"runtime.gpv"), "INERT_VOLUME");
        d::Json manifest{{"architecture","x64"},{"profile","win10"},{"applicationSha256",key},
            {"nativeGuiRuntime",{{"helperSha256",Digest("INERT_OLD_HELPER")},{"protocol",2}}}};
        const auto text = manifest.dump(2)+"\n";
        Put(root/d::Wide(prefix+"manifest.json"), text);
        guardian::nt::Info binding;
        const auto hash = [&](uint8_t* field, const std::string& value) {
            const auto bytes = d::Unhex(value); std::copy(bytes.begin(), bytes.end(), field);
        };
        hash(binding.applicationSha,key); hash(binding.manifestSha,Digest(text));
        hash(binding.kernelSha,Digest("INERT_KERNEL")); hash(binding.runtimeSha,Digest("INERT_VOLUME"));
        Put(root/d::Wide(prefix+"binding.gnt"), {reinterpret_cast<const char*>(&binding),sizeof(binding)});
        d::Json files = d::Json::object();
        for (const auto* name : {"guardian-native-user-host.exe","kernel.gk","runtime.gpv","manifest.json","binding.gnt"})
            files[prefix+name] = d::HashFile(root/d::Wide(prefix+name));
        index["applications"][key] = {{"architecture","x64"},{"profile","win10"},{"mode","user32"},
            {"helper",prefix+"guardian-native-user-host.exe"},{"kernel",prefix+"kernel.gk"},
            {"volume",prefix+"runtime.gpv"},{"manifest",prefix+"manifest.json"},
            {"binding",prefix+"binding.gnt"},{"files",files}};
        Put(root/L"index.json",index.dump(2)+"\n");
    }
};
}
int wmain(int argc, wchar_t** argv) {
    try {
        d::Require(argc==2,"Fresh catalog test output required");
        const auto output = d::fs::absolute(argv[1]);
        d::Require(!d::fs::exists(output),"Catalog test output must be fresh");
        d::Json cases = d::Json::array();
        const auto Test = [&](const char* name, const auto& action) { action(); cases.push_back(name); };
        Test("verified helper update refreshes all enrollment hash links", [&] {
            Fixture f(output/L"refresh"); const auto oldKernel=d::HashFile(f.root/d::Wide(f.app+"/kernel.gk"));
            d::Require(RefreshRuntimeHelpers(f.root,f.helpers)==1,"Expected one refreshed application");
            auto index=Load(f.root/L"index.json"); const auto& record=index["applications"][f.app];
            for(auto file=record["files"].begin();file!=record["files"].end();++file)
                d::Require(d::HashFile(f.root/d::Wide(file.key()))==file.value().get<std::string>(),"Migrated member hash");
            d::Require(d::HashFile(f.root/d::Wide(f.app+"/kernel.gk"))==oldKernel,"Kernel changed during helper-only upgrade");
            d::Require(RefreshRuntimeHelpers(f.root,f.helpers)==0,"Migrated binding did not validate on second pass");
        });
        Test("matching helper is byte-exact and idempotent", [&] {
            Fixture f(output/L"same");Put(f.helpers/L"guardian-native-user-host.exe","INERT_OLD_HELPER");
            const auto before=Snapshot(f.root);d::Require(RefreshRuntimeHelpers(f.root,f.helpers)==0&&Snapshot(f.root)==before,"Unchanged catalog rewritten");
        });
        Test("retained rollback source remains unchanged", [&] {
            Fixture f(output/L"rollback");const auto before=Snapshot(f.root);const auto copy=output/L"inactive-copy";
            d::fs::copy(f.root,copy,d::fs::copy_options::recursive);RefreshRuntimeHelpers(copy,f.helpers);
            d::Require(Snapshot(f.root)==before&&Snapshot(copy)!=before,"Migration changed retained slot");
        });
        Test("modified retained member is refused before writes", [&] {
            Fixture f(output/L"tamper");Put(f.root/d::Wide(f.app+"/runtime.gpv"),"MODIFIED");const auto before=Snapshot(f.root);
            Rejected([&]{RefreshRuntimeHelpers(f.root,f.helpers);});d::Require(Snapshot(f.root)==before,"Rejected catalog changed");
        });
        Test("invalid manifest binding is refused even with a matching file hash", [&] {
            Fixture f(output/L"binding");const auto name=f.app+"/binding.gnt";auto bytes=d::Read(f.root/d::Wide(name));bytes[48]^=1;Put(f.root/d::Wide(name),bytes);
            auto index=Load(f.root/L"index.json");index["applications"][f.app]["files"][name]=Digest(bytes);Put(f.root/L"index.json",index.dump());
            Rejected([&]{RefreshRuntimeHelpers(f.root,f.helpers);});
        });
        Test("arbitrary executable cannot replace the fixed helper role", [&] {
            Fixture f(output/L"role");auto index=Load(f.root/L"index.json");index["applications"][f.app]["helper"]=f.app+"/arbitrary.exe";Put(f.root/L"index.json",index.dump());
            Rejected([&]{RefreshRuntimeHelpers(f.root,f.helpers);});
        });
        Test("member cannot cross the application namespace", [&] {
            Fixture f(output/L"namespace");auto index=Load(f.root/L"index.json");index["applications"][f.app]["files"]["../outside"]=std::string(64,'0');Put(f.root/L"index.json",index.dump());
            Rejected([&]{RefreshRuntimeHelpers(f.root,f.helpers);});
        });
        Test("all retained records validate before any helper is changed", [&] {
            Fixture f(output/L"transaction");const std::string second(64,'b');f.Add(second);Put(f.root/d::Wide(second+"/kernel.gk"),"CORRUPTED");
            const auto before=Snapshot(f.root);Rejected([&]{RefreshRuntimeHelpers(f.root,f.helpers);});d::Require(Snapshot(f.root)==before,"Earlier record changed before validation finished");
        });
        const d::Json result{{"phase","19A"},{"status","PASS"},{"tests",cases.size()},{"cases",cases}};
        Put(output/L"results.json",result.dump(2)+"\n");std::cout<<result.dump()<<'\n';return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
