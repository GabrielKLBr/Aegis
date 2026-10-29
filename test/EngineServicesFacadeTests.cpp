#include "EngineFacade.h"
#include "services/Services.h"
#include <iostream>
int wmain(int argc, wchar_t** argv) {
    using namespace av::services;
    try {
        Check(argc == 2, "Pass the generated integration corpus directory");
        const fs::path root(argv[1]);
        av::Engine engine(true);
        unsigned updates = 0;
        engine.StartScan(root.wstring(), [&] { ++updates; }, {}); engine.Join();
        auto& state = engine.State();
        Check(updates > 0 && state.filesScanned == 3 && state.threatsFound == 1 && state.inconclusive == 1 &&
            state.unknown == 1 && !state.detections.empty() && state.error.empty(), "App facade lost the service result");
        engine.StartScan((root / L"ordinary.txt").wstring(), {}, {}); engine.Join();
        Check(state.filesScanned == 1 && state.unknown == 1 && !state.threatsFound && state.detections.empty(), "App facade did not reset prior results");
        engine.StartScan(root.wstring(), [&] { if (state.filesScanned) engine.Cancel(); }, {}); engine.Join();
        Check(state.cancel && !engine.IsRunning(), "App cancellation did not finish");
        SetRunning(Kind::Antivirus, false);
        engine.StartScan((root / L"ordinary.txt").wstring(), {}, {}); engine.Join();
        Check(!state.filesScanned && state.inconclusive && !state.error.empty(), "App silently fell back to a local engine");
        SetRunning(Kind::Antivirus, true);
        std::cout << "App engine service facade PASS\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
