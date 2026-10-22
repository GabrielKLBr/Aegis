// ReportBuilder.h - collects guest behavior into a "what the sample did" report.
//
// Every serviced syscall appends an event (file/registry/device/process). The
// ChangeSet of the architecture doc is built from these: a timeline plus a
// grouped summary the UI/CLI can show after a detonation.
#pragma once

#include <string>
#include <vector>

namespace av {
namespace microvm {

class ReportBuilder {
public:
    enum class Cat { File, Registry, Process, Device, Other };
    struct Event {
        Cat cat;
        std::string action;   // "create", "write", "open", "query"...
        std::string target;   // path / key / device name
        std::string detail;   // e.g. "42 bytes"
    };

    void Add(Cat cat, std::string action, std::string target, std::string detail = "");
    const std::vector<Event>& Events() const { return events_; }

    // Human-readable timeline + summary.
    std::string Render() const;

private:
    std::vector<Event> events_;
};

} // namespace microvm
} // namespace av
