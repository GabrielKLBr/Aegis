// VirtualRegistry.cpp - implementation of the in-memory registry.
#include "VirtualRegistry.h"

namespace av {
namespace microvm {

void VirtualRegistry::SetValue(const std::string& key, const std::string& name,
                               const std::string& data) {
    keys_[key][name] = data;
}

bool VirtualRegistry::QueryValue(const std::string& key, const std::string& name,
                                 std::string& out) const {
    auto k = keys_.find(key);
    if (k == keys_.end()) return false;
    auto v = k->second.find(name);
    if (v == k->second.end()) return false;
    out = v->second;
    return true;
}

} // namespace microvm
} // namespace av
