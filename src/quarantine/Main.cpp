#include "Store.h"
#include <iostream>
int wmain(int argc, wchar_t **argv) {
    try {
        using namespace av;
        scan::fs::path root = quarantine::DefaultRoot(), input, parent;
        std::wstring leaf;
        uint64_t beforeSequence = 0;
        std::string action = "list", id, sha, threat = "Manual", reason;
#ifdef AEGIS_QUARANTINE_TESTING
        std::string fault;
        bool crash = false;
        unsigned pages = 0, events = 0;
#endif
        for (int i = 1; i < argc; ++i) {
            std::wstring option = argv[i];
            scan::Require(i + 1 < argc, "Option requires a value");
            std::wstring value = argv[++i];
            auto text = scan::PathText(value);
            if (option == L"--root")
                root = value;
            else if (option == L"--action")
                action = text;
            else if (option == L"--input")
                input = value;
            else if (option == L"--directory")
                parent = value;
            else if (option == L"--filename")
                leaf = value;
            else if (option == L"--id")
                id = text;
            else if (option == L"--before")
                beforeSequence = std::stoull(value);
            else if (option == L"--sha256")
                sha = text;
            else if (option == L"--threat")
                threat = text;
            else if (option == L"--reason")
                reason = text;
#ifdef AEGIS_QUARANTINE_TESTING
            else if (option == L"--fault")
                fault = text;
            else if (option == L"--crash")
                crash = value == L"1";
            else if (option == L"--seed-audit")
                events = unsigned(std::stoul(value));
            else if (option == L"--max-pages")
                pages = unsigned(std::stoul(value));
#endif
            else
                throw std::runtime_error("Unknown quarantine option");
        }
        quarantine::Store store(root);
#ifdef AEGIS_QUARANTINE_TESTING
        quarantine::TestFault(fault, crash);
        if (events)
            store.TestAuditEvents(events);
        if (pages)
            store.TestMaxPages(pages);
#endif
        auto result = action == "list"      ? store.List()
                      : action == "audit"   ? store.Audit(beforeSequence)
                      : action == "isolate" ? store.Isolate(input, sha, threat, reason)
                      : action == "restore" ? store.Restore(id, parent, leaf)
                      : action == "erase"   ? store.Erase(id)
                                            : throw std::runtime_error("Unknown quarantine action");
        std::cout << result.dump() << '\n';
        return result.value("complete", false) ? 0 : 3;
    } catch (const std::exception &e) {
        std::cout << av::scan::Json(
                         {{"schema", 2}, {"type", "quarantine"}, {"complete", false}, {"error", e.what()}})
                         .dump()
                  << '\n';
        return 3;
    }
}
