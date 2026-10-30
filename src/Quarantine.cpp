#include "Quarantine.h"
#ifdef GUARDIAN_ENGINE_SERVICES
#include "quarantine/Store.h"
#include "services/Services.h"
#endif
namespace av {
Quarantine::Quarantine(std::wstring dir) { (void)dir; }
bool Quarantine::Isolate(const Detection &det) {
#ifdef GUARDIAN_ENGINE_SERVICES
    std::filesystem::path job;
    try {
        if (det.sha256.empty())
            return false;
        const auto folder = quarantine::DefaultRoot().parent_path() / L"quarantine-requests";
        scan::PrivateState(folder);
        job = folder / (scan::Token() + ".json");
        scan::WriteJson(job, {{"schema", 2},
                              {"source", scan::PathText(scan::fs::absolute(det.file).lexically_normal())},
                              {"sha256", det.sha256},
                              {"threat", WideToUtf8(det.threat)},
                              {"reason", WideToUtf8(det.reason)}});
        const auto result = services::Call(services::Kind::Antivirus, services::Operation::QuarantineIsolate,
                                           job, {}, nullptr, 60000);
        std::error_code ignored;
        scan::fs::remove(job, ignored);
        return result.value("complete", false);
    } catch (const std::exception &e) {
        if (!job.empty()) {
            std::error_code ignored;
            scan::fs::remove(job, ignored);
        }
        PrintLine(Utf8ToWide(e.what()), Color::Red);
        return false;
    }
#else
    (void)det;
    return false;
#endif
}
void Quarantine::List() const {
#ifdef GUARDIAN_ENGINE_SERVICES
    try {
        PrintLine(Utf8ToWide(services::Call(services::Kind::Antivirus, services::Operation::QuarantineList,
                                            services::Executable(), {}, nullptr, 15000)
                                 .dump(2)),
                  Color::Gray);
    } catch (const std::exception &e) {
        PrintLine(Utf8ToWide(e.what()), Color::Red);
    }
#else
    PrintLine(L"O serviço de quarentena não está incluído nesta build.", Color::Red);
#endif
}
} // namespace av
