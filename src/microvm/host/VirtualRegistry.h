// VirtualRegistry.h - an in-memory virtual Windows registry.
//
// Registry reads return values WE seed (OS version, GPU name, ... -- the system
// "personality"); writes (persistence Run keys, etc.) are captured here and
// never touch the real HKLM/HKCU. With ReportBuilder this answers "what registry
// keys did the sample read and write?".
#pragma once

#include <string>
#include <map>

namespace av {
namespace microvm {

class VirtualRegistry {
public:
    void SetValue(const std::string& key, const std::string& name, const std::string& data);
    bool QueryValue(const std::string& key, const std::string& name, std::string& out) const;

    const std::map<std::string, std::map<std::string, std::string>>& Keys() const { return keys_; }

private:
    std::map<std::string, std::map<std::string, std::string>> keys_;  // key -> {name -> data}
};

} // namespace microvm
} // namespace av
