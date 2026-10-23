#include "Scanner.h"
#include <iostream>
#ifdef GUARDIAN_SCANNER_VM
#include "microvm/MicroSandbox.h"
#endif
namespace av::scan
{
int WorkerMain(const fs::path &folder)
{
    try
    {
        OrdinaryDirectory(folder);
        const auto request = ReadJson(folder / "request.json");
        Require(request.value("schema", 0) == 1, "Worker request schema rejected");
        const auto limits = Limits::Parse(request.at("limits"));
        const auto bytes = Read(folder / "payload", limits.fileBytes);
        Require(Digest(bytes) == request.at("sha256").get<std::string>(), "Worker payload integrity failure");
        const auto action = request.at("action").get<std::string>();
        Json result;
        if (action == "static")
            result =
                Inspect(bytes, request.at("name"), request.at("rules"), request.at("signatures"), limits);
        else if (action == "sandbox")
        {
            result = {{"schema", 1},
                      {"classification", "inconclusive"},
                      {"complete", false},
                      {"backend", "MiniNT-WHP-legacy"},
                      {"coverage", "limited CRT-free PE imports; no host process fallback"},
                      {"issues", Json::array()}};
#ifdef GUARDIAN_SCANNER_VM
            Require(bytes.size() >= 2 && bytes[0] == 'M' && bytes[1] == 'Z',
                    "Sandbox accepts PE inputs only");
            av::microvm::MicroSandbox sandbox;
            const auto r = sandbox.Detonate((folder / "payload").wstring());
            result["loaded"] = r.loaded;
            result["ran"] = r.ran;
            result["exitCode"] = r.exitCode;
            result["faulted"] = r.faulted;
            result["unresolved"] = r.unresolved;
            result["error"] = r.error;
            result["events"] = Json::array();
            Require(r.events.size() <= 4096, "Sandbox event limit exceeded");
            for (const auto &event : r.events)
                result["events"].push_back({{"category", int(event.cat)},
                                            {"action", event.action},
                                            {"target", event.target},
                                            {"detail", event.detail}});
            result["complete"] = r.loaded && r.ran && !r.faulted && r.error.empty() && r.unresolved.empty();
            if (result["complete"].get<bool>())
                result["classification"] = "unknown";
            result["virtualFiles"] = Json::array();
            for (const auto &[name, data] : r.files)
                result["virtualFiles"].push_back(
                    {{"path", name}, {"bytes", data.size()}, {"sha256", Digest(data)}});
            result["virtualRegistry"] = r.registry;
#else
            result["backend"] = "unavailable";
            result["issues"].push_back("External PE sandbox handoff requires the Windows MiniNT backend; "
                                       "host execution is disabled");
#endif
        }
        else
            throw std::runtime_error("Worker action rejected");
        WriteJson(folder / "result.json", result);
        return 0;
    }
    catch (const std::exception &)
    {
        return 3;
    }
}
} // namespace av::scan
#ifdef _WIN32
int wmain(int argc, wchar_t **argv)
{
    return argc == 2 ? av::scan::WorkerMain(std::filesystem::path(argv[1])) : 2;
}
#else
int main(int argc, char **argv)
{
    return argc == 2 ? av::scan::WorkerMain(std::filesystem::path(argv[1])) : 2;
}
#endif
