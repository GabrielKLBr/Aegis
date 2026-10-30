#include "Scanner.h"
#include <iostream>
#ifdef GUARDIAN_SCANNER_VM
#include "microvm/runtime/Unified.h"
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
                      {"backend", "MiniNT-unified-14.13"},
                      {"coverage", "hash-enrolled owned-kernel runtime; no host or legacy fallback"},
                      {"issues", Json::array()}};
#ifdef GUARDIAN_SCANNER_UNIFIED
            guardian::telemetry::Policy policy;policy.eventLimit=guardian::telemetry::MaxEvents;
            result=av::microvm::runtime::Run(folder / "payload",Utf8Path(request.value("sandboxRuntime",std::string())),Utf8Path(request.at("sandboxOutput").get<std::string>()),policy,limits.milliseconds);
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
