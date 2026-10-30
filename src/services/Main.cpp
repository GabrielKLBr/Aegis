#include "Services.h"
#include <iostream>

namespace {
av::services::Kind ParseKind(const std::wstring& text) {
    using namespace av::services;
    Check(text == L"antivirus" || text == L"sandbox", "Choose antivirus or sandbox");
    return text == L"antivirus" ? Kind::Antivirus : Kind::Sandbox;
}
}
int wmain(int argc, wchar_t** argv) {
    using namespace av::services;
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    try {
        const std::wstring action = argc >= 2 ? argv[1] : L"--help";
        if (action == L"--serve" && argc == 5 && std::wstring(argv[3]) == L"--owner")
            return Serve(ParseKind(argv[2]), argv[4]);
        if (action == L"--worker" && argc == 4) {
            size_t consumed = 0; const auto value = std::stoul(argv[3], &consumed);
            Check(consumed == wcslen(argv[3]) && value > 0, "Worker supervisor PID rejected");
            return Worker(argv[2], DWORD(value));
        }
        if ((action == L"--install" || action == L"--uninstall") && argc == 4 && std::wstring(argv[2]) == L"--owner")
            return Install(argv[3], action == L"--uninstall");
        if (action == L"--setup" || action == L"--remove") return int(Setup(nullptr, action == L"--remove"));
        if ((action == L"--start" || action == L"--stop") && argc == 3) { SetRunning(ParseKind(argv[2]), action == L"--start"); return 0; }
        if (action == L"--status") {
            Json states = Json::object();
            for (const auto kind : {Kind::Antivirus, Kind::Sandbox}) {
                const auto state = Query(kind);
                states[kind == Kind::Antivirus ? "antivirus" : "sandbox"] = {{"installed", state.installed}, {"running", state.Running()},
                    {"state", state.state}, {"pid", state.process}, {"error", state.error}};
            }
            std::cout << states.dump() << '\n'; return 0;
        }
        if (action == L"--package-files") {
            Json names = Json::array();
            for (const auto* name : PackageFiles) names.push_back(Text(name));
            std::cout << names.dump() << '\n'; return 0;
        }
        if(action==L"--q-list"||action==L"--q-audit"||action==L"--q-isolate"||action==L"--q-restore"||action==L"--q-erase"){
            const bool simple=action==L"--q-list"||action==L"--q-audit";Check(argc==(simple?2:3)||(action==L"--q-audit"&&argc==3),"Quarantine command requires a request JSON file");
            const auto op=action==L"--q-list"?Operation::QuarantineList:action==L"--q-audit"?Operation::QuarantineAudit:action==L"--q-isolate"?Operation::QuarantineIsolate:action==L"--q-restore"?Operation::QuarantineRestore:Operation::QuarantineErase;
            auto result=Call(Kind::Antivirus,op,argc==2?Executable():fs::path(argv[2]),[](const Json& value){std::cout<<value.dump()<<std::endl;},nullptr,60000);std::cout<<result.dump()<<'\n';return result.value("complete",false)?0:3;
        }
        if ((action == L"--scan" || action == L"--sandbox" || action == L"--bundle" || action == L"--checkpoint" || action == L"--resume") && argc == 3) {
            const bool scanner = action == L"--scan";
            const auto result = Call(scanner ? Kind::Antivirus : Kind::Sandbox,
                scanner ? Operation::Scan : action == L"--bundle" ? Operation::DesktopBundle : action == L"--checkpoint" ? Operation::Checkpoint : action == L"--resume" ? Operation::Resume : Operation::Detonate,
                argv[2], [](const Json& value) { std::cout << value.dump() << std::endl; }, nullptr, scanner ? 3600000 : 60000);
            std::cout << result.dump() << '\n';
            return result.value("complete", false)||result.value("checkpointCreated",false) ? 0 : 3;
        }
        std::cout << "Aegis Windows engine services\n--setup | --remove | --status | --start antivirus|sandbox | --stop antivirus|sandbox\n"
                     "--scan PATH | --sandbox PE | --checkpoint PE | --resume recovery.json | --bundle PREPARED_AUDIO_BUNDLE\n";
        return action == L"--help" ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << Json({{"error", error.what()}}).dump() << '\n';
        const auto* windows = dynamic_cast<const WindowsError*>(&error);
        return windows ? int(windows->code) : 1;
    }
}
