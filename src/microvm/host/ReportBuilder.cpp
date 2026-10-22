// ReportBuilder.cpp - implementation of the behavior report.
#include "ReportBuilder.h"

#include <set>
#include <sstream>

namespace av {
namespace microvm {

namespace {
const char* CatName(ReportBuilder::Cat c) {
    switch (c) {
        case ReportBuilder::Cat::File:     return "File";
        case ReportBuilder::Cat::Registry: return "Registry";
        case ReportBuilder::Cat::Process:  return "Process";
        case ReportBuilder::Cat::Device:   return "Device";
        default:                           return "Other";
    }
}
} // namespace

void ReportBuilder::Add(Cat cat, std::string action, std::string target, std::string detail) {
    events_.push_back({cat, std::move(action), std::move(target), std::move(detail)});
}

std::string ReportBuilder::Render() const {
    std::ostringstream o;
    o << "Behavior report\n";
    o << "  timeline (" << events_.size() << " event(s)):\n";
    for (const auto& e : events_) {
        o << "    [" << CatName(e.cat) << "] " << e.action << "  " << e.target;
        if (!e.detail.empty()) o << "  (" << e.detail << ")";
        o << "\n";
    }

    // Summary: distinct files created/written, total bytes written.
    std::set<std::string> created, written;
    unsigned long long bytes = 0;
    for (const auto& e : events_) {
        if (e.cat != Cat::File) continue;
        if (e.action == "create") created.insert(e.target);
        if (e.action == "write") {
            written.insert(e.target);
            // detail looks like "<n> bytes"
            unsigned long long n = 0;
            for (char c : e.detail) { if (c < '0' || c > '9') break; n = n * 10 + (c - '0'); }
            bytes += n;
        }
    }
    o << "  summary:\n";
    o << "    files created : " << created.size() << "\n";
    o << "    files written : " << written.size() << "\n";
    o << "    bytes written : " << bytes << "\n";
    return o.str();
}

} // namespace microvm
} // namespace av
