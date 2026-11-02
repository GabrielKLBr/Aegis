#include "RuntimeCatalog.h"
#include "common/BoundedJson.h"
#include "distribution/Distribution.h"
#include "guardian/NtCompatibility.h"
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstring>

namespace av::services {
namespace d = aegis::distribution;
namespace {
std::string Digest(const std::string& bytes) {
    return d::Hash({reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()});
}
bool HashName(const std::string& text) {
    return text.size() == 64 && text.find_first_not_of("0123456789abcdef") == text.npos;
}
struct Plan {
    std::filesystem::path helper, helperSource, manifest, binding;
    std::string manifestBytes, bindingBytes;
};
void ReplaceHelper(const std::filesystem::path& source, const std::filesystem::path& target) {
    std::array<uint8_t,16> random{};
    d::Require(BCryptGenRandom(nullptr, random.data(), ULONG(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,
               "Runtime helper transaction identity");
    const auto temporary=target.parent_path()/d::Wide("refresh-"+d::Hex(random)+".tmp");
    try {
        // CopyMember applies the protected installation owner/DACL. Never
        // create a temporarily user-owned executable in the inactive slot.
        d::CopyMember(source,temporary);
        d::Require(MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE,
                   "Commit refreshed runtime helper");
    } catch (...) { DeleteFileW(temporary.c_str()); throw; }
}
}
size_t RefreshRuntimeHelpers(const std::filesystem::path& registry,
                             const std::filesystem::path& verifiedEngines) {
    auto index = aegis::security::ParseJson(d::Read(registry / L"index.json"));
    d::Require(index.value("schema", 0) == 1 && index.value("phase", std::string()) == "14.13" &&
               index.at("applications").is_object() && index.at("applications").size() <= 16,
               "Runtime catalog migration schema rejected");
    std::vector<Plan> plans;
    uint64_t total = 0;
    for (auto app = index["applications"].begin(); app != index["applications"].end(); ++app) {
        auto& record = app.value();
        d::Require(HashName(app.key()), "Runtime application identity rejected");
        const auto mode = record.at("mode").get<std::string>();
        d::Require(mode == "user32" || mode == "compat32" || mode == "frameworks",
                   "Runtime helper family rejected");
        d::Require(record.at("profile") == "win10" &&
                   (record.at("architecture") == "x64" || record.at("architecture") == "x86"),
                   "Runtime migration profile rejected");
        const std::string helperName = mode == "frameworks" ? "guardian-audio-host.exe" : "guardian-native-user-host.exe";
        const auto helper = record.at("helper").get<std::string>(),
                   manifestName = record.at("manifest").get<std::string>(),
                   bindingName = record.at("binding").get<std::string>();
        d::Require(helper == app.key() + "/" + helperName && manifestName == app.key() + "/manifest.json" &&
                   bindingName == app.key() + "/binding.gnt", "Runtime helper/member role rejected");
        auto& files = record.at("files");
        d::Require(files.is_object() && files.size() >= 5 && files.size() <= 12, "Runtime member quota rejected");
        for (auto file = files.begin(); file != files.end(); ++file) {
            d::RelativeName(file.key());
            d::Require(file.key().starts_with(app.key() + "/") && file.value().is_string() &&
                       HashName(file.value().get<std::string>()), "Runtime namespace/hash rejected");
            const auto path = registry / d::Wide(file.key());
            d::Ordinary(path);
            const auto size = std::filesystem::file_size(path);
            d::Require(size <= (64u << 20), "Runtime retained member size rejected");
            total += size;
            d::Require(total <= (512ull << 20) && d::HashFile(path) == file.value().get<std::string>(), "Runtime retained member hash rejected");
        }
        const auto oldManifest = d::Read(registry / d::Wide(manifestName));
        auto manifest = aegis::security::ParseJson(oldManifest);
        const auto bindingBytes = d::Read(registry / d::Wide(bindingName), sizeof(guardian::nt::Info));
        d::Require(bindingBytes.size() == sizeof(guardian::nt::Info), "Runtime binding size rejected");
        guardian::nt::Info binding;
        std::memcpy(&binding, bindingBytes.data(), sizeof(binding));
        d::Require(guardian::nt::Valid(binding, 0) &&
                   d::Hex(binding.manifestSha) == Digest(oldManifest) &&
                   d::Hex(binding.applicationSha) == app.key() &&
                   d::Hex(binding.kernelSha) == files.at(record.at("kernel").get<std::string>()).get<std::string>() &&
                   d::Hex(binding.runtimeSha) == files.at(record.at("volume").get<std::string>()).get<std::string>(),
                   "Runtime retained binding rejected");
        d::Require(manifest.at("applicationSha256") == app.key() &&
                   manifest.at("architecture") == record.at("architecture") &&
                   manifest.at("profile") == record.at("profile") &&
                   manifest.at("nativeGuiRuntime").at("helperSha256") == files.at(helper),
                   "Runtime retained manifest/helper binding rejected");
        const auto newHelper = verifiedEngines / d::Wide(helperName);
        d::Ordinary(newHelper);
        d::Require(std::filesystem::file_size(newHelper) <= (16u << 20), "Runtime helper size rejected");
        const auto newHelperHash = d::HashFile(newHelper);
        if (newHelperHash == files.at(helper).get<std::string>()) continue;
        manifest["nativeGuiRuntime"]["helperSha256"] = newHelperHash;
        const auto nextManifest = manifest.dump(-1, ' ', true) + "\n";
        const auto manifestHash = d::Unhex(Digest(nextManifest));
        std::copy(manifestHash.begin(), manifestHash.end(), binding.manifestSha);
        const std::string nextBinding(reinterpret_cast<const char*>(&binding), sizeof(binding));
        files[helper] = newHelperHash;
        files[manifestName] = Digest(nextManifest);
        files[bindingName] = Digest(nextBinding);
        plans.push_back({registry / d::Wide(helper), newHelper, registry / d::Wide(manifestName),
                         registry / d::Wide(bindingName), nextManifest, nextBinding});
    }
    // Validate every old member and binding before modifying this inactive copy.
    // Interrupted writes cannot affect the retained active/rollback slot.
    for (const auto& plan : plans) {
        ReplaceHelper(plan.helperSource, plan.helper);
        d::AtomicWrite(plan.manifest, plan.manifestBytes);
        d::AtomicWrite(plan.binding, plan.bindingBytes);
    }
    if (!plans.empty()) d::AtomicJson(registry / L"index.json", index);
    return plans.size();
}
}
