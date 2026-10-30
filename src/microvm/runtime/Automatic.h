#pragma once
#include "../../../guest/ApiSet.h"
#include "Unified.h"
#include <map>
namespace av::microvm::runtime::automatic {
using Bytes = std::span<const uint8_t>;
struct Pe {
    Bytes data;
    uint32_t opt = 0, sections = 0, table = 0;
    bool x86 = false;
    uint32_t U(uint32_t at, uint32_t n = 4) const {
        scan::Require(at <= data.size() && n <= data.size() - at, "PE header outside input");
        uint32_t v = 0;
        for (uint32_t i = 0; i < n; ++i)
            v |= uint32_t(data[at + i]) << (i * 8);
        return v;
    }
    explicit Pe(Bytes bytes) : data(bytes) {
        scan::Require(data.size() >= 64 && U(0, 2) == 0x5A4D, "PE input required");
        auto pe = U(60);
        scan::Require(pe <= data.size() && data.size() - pe >= 24 && U(pe) == 0x4550,
                      "PE signature rejected");
        auto machine = U(pe + 4, 2);
        x86 = machine == 0x14C;
        scan::Require(x86 || machine == 0x8664, "Only x86/x64 PE inputs are supported");
        opt = pe + 24;
        auto size = U(pe + 20, 2);
        sections = U(pe + 6, 2);
        table = opt + size;
        scan::Require(U(opt, 2) == (x86 ? 0x10Bu : 0x20Bu) && size >= (x86 ? 224u : 240u) && sections &&
                          sections <= 32 && table <= data.size() && sections * 40 <= data.size() - table,
                      "PE layout rejected");
    }
    uint32_t At(uint32_t rva, uint32_t bytes = 1) const {
        for (uint32_t i = 0; i < sections; ++i) {
            auto s = table + i * 40, base = U(s + 12), size = U(s + 16), raw = U(s + 20);
            if (rva >= base && rva - base <= size && bytes <= size - (rva - base)) {
                uint64_t at = uint64_t(raw) + rva - base;
                scan::Require(at <= data.size() && bytes <= data.size() - at, "PE section outside file");
                return uint32_t(at);
            }
        }
        throw std::runtime_error("PE RVA outside initialized sections");
    }
    std::string Text(uint32_t rva) const {
        std::string result;
        for (uint32_t i = 0; i < 256; ++i) {
            scan::Require(rva <= UINT32_MAX - i, "PE string overflow");
            auto c = data[At(rva + i)];
            if (!c)
                return result;
            scan::Require(c >= 32 && c < 127, "PE symbol text rejected");
            result += char(c);
        }
        throw std::runtime_error("PE symbol quota");
    }
    std::pair<uint32_t, uint32_t> Directory(uint32_t index) const {
        auto at = opt + (x86 ? 96 : 112) + index * 8;
        return {U(at), U(at + 4)};
    }
    std::vector<std::pair<std::string, std::string>> Imports() const {
        std::vector<std::pair<std::string, std::string>> result;
        for (auto directory : {1u, 13u}) {
            auto [rva, size] = Directory(directory);
            if (!rva && !size)
                continue;
            uint32_t stride = directory == 1 ? 20 : 32;
            scan::Require(size >= stride && size <= 65536, "Import directory quota");
            bool ended = false;
            for (uint32_t offset = 0; offset + stride <= size; offset += stride) {
                auto at = At(rva + offset, stride);
                bool zero = true;
                for (uint32_t i = 0; i < stride; i += 4)
                    zero &= U(at + i) == 0;
                if (zero) {
                    ended = true;
                    break;
                }
                if (directory == 13)
                    scan::Require(U(at) == 1, "Only RVA delay imports are supported");
                auto name = Text(U(at + (directory == 1 ? 12 : 4)));
                for (auto &c : name)
                    if (c >= 'A' && c <= 'Z')
                        c += 32;
                auto thunk = U(at + (directory == 1 ? 0 : 16));
                if (!thunk)
                    thunk = U(at + (directory == 1 ? 16 : 12));
                bool terminated = false;
                for (uint32_t i = 0; i < 4096; ++i) {
                    auto width = x86 ? 4u : 8u;
                    scan::Require(uint64_t(thunk) + uint64_t(i) * width <= UINT32_MAX,
                                  "Import thunk overflow");
                    auto entry = At(thunk + i * width, width);
                    auto value = U(entry), high = x86 ? 0 : U(entry + 4);
                    if (!value && !high) {
                        terminated = true;
                        break;
                    }
                    bool ordinal = x86 ? (value & 0x80000000u) != 0 : (high & 0x80000000u) != 0;
                    scan::Require(ordinal || !high, "Import name outside PE32 RVA");
                    result.emplace_back(name,
                                        ordinal ? "#" + std::to_string(value & 65535) : Text(value + 2));
                    scan::Require(result.size() <= 8192, "Import count quota");
                }
                scan::Require(terminated, "Import thunk terminator missing");
            }
            scan::Require(ended, "Import descriptor terminator missing");
        }
        return result;
    }
    std::map<std::string, std::string> Exports() const {
        std::map<std::string, std::string> result;
        auto [rva, size] = Directory(0);
        if (!rva && !size)
            return result;
        auto at = At(rva, 40), base = U(at + 16), count = U(at + 20), names = U(at + 24);
        scan::Require(count <= 16384 && names <= count, "Export quota");
        for (uint32_t i = 0; i < count; ++i) {
            auto f = U(At(U(at + 28) + i * 4, 4));
            if (!f)
                continue;
            auto forward = f >= rva && uint64_t(f) < uint64_t(rva) + size ? Text(f) : std::string();
            result["#" + std::to_string(base + i)] = forward;
        }
        for (uint32_t i = 0; i < names; ++i) {
            auto ordinal = U(At(U(at + 36) + i * 2, 2), 2);
            scan::Require(ordinal < count, "Export ordinal rejected");
            auto key = "#" + std::to_string(base + ordinal);
            if (result.contains(key))
                result[Text(U(At(U(at + 32) + i * 4, 4)))] = result.at(key);
        }
        return result;
    }
};
inline scan::Json Missing(Bytes payload, Bytes volume) {
    const auto &h = *reinterpret_cast<const guardian::pevolume::Header *>(volume.data());
    std::map<std::string, Bytes> dlls;
    guardian::apiset::Namespace schema;
    for (uint32_t i = 0; i < h.files; ++i)
        if (i != h.root) {
            const auto &f = guardian::pevolume::FileAt(h, i);
            std::string name = f.path;
            name = name.substr(name.find_last_of('\\') + 1);
            auto bytes = volume.subspan(f.offset, f.bytes);
            if (name == "apisetschema.dll")
                scan::Require(schema.OpenPe(bytes.data(), uint32_t(bytes.size())), "API set schema rejected");
            if (f.kind == guardian::pevolume::Kind::Pe)
                dlls[name] = bytes;
        }
    scan::Json missing = scan::Json::array();
    std::map<std::string, std::map<std::string, std::string>> exportCache;
    std::function<bool(std::string, std::string, std::string, unsigned)> resolve =
        [&](std::string dll, std::string symbol, std::string importer, unsigned depth) {
            if (depth > 16)
                return false;
            for (auto &c : dll)
                if (c >= 'A' && c <= 'Z')
                    c += 32;
            if (dll.starts_with("api-ms-") || dll.starts_with("ext-ms-")) {
                char host[128]{};
                if (!schema.Resolve(dll.c_str(), importer.c_str(), host))
                    return false;
                dll = host;
            }
            if (!dlls.contains(dll))
                return false;
            if (!exportCache.contains(dll))
                exportCache.emplace(dll, Pe(dlls.at(dll)).Exports());
            const auto &exports = exportCache.at(dll);
            if (!exports.contains(symbol))
                return false;
            const auto &next = exports.at(symbol);
            if (next.empty())
                return true;
            auto split = next.find_last_of('.');
            return split != next.npos &&
                   resolve(next.substr(0, split) + ".dll", next.substr(split + 1), dll, depth + 1);
        };
    for (auto [dll, symbol] : Pe(payload).Imports())
        if (!resolve(dll, symbol, "sample.exe", 0) && missing.size() < 128)
            missing.push_back(dll + "!" + symbol);
    return missing;
}
inline void Stage(std::vector<uint8_t> &volume, scan::Json &manifest, guardian::nt::Info &binding,
                  Bytes payload) {
    auto old = volume;
    auto &original = *reinterpret_cast<const guardian::pevolume::Header *>(old.data());
    std::vector<uint8_t> built(old.begin(), old.begin() + original.headerBytes);
    built.resize((built.size() + 4095) & ~size_t(4095));
    for (uint32_t i = 0; i < original.files; ++i) {
        auto file = guardian::pevolume::FileAt(original, i);
        auto bytes = i == original.root ? payload : Bytes(old).subspan(file.offset, file.bytes);
        auto offset = (built.size() + 4095) & ~size_t(4095);
        scan::Require(offset <= guardian::pevolume::MaxBytes &&
                          bytes.size() <= guardian::pevolume::MaxBytes - offset,
                      "Automatic package volume quota");
        built.resize(offset);
        built.insert(built.end(), bytes.begin(), bytes.end());
        file.offset = uint32_t(offset);
        file.bytes = uint32_t(bytes.size());
        file.hash = guardian::pevolume::Hash(bytes.data(), file.bytes);
        if (i == original.root) {
            std::memset(file.path, 0, sizeof(file.path));
            std::memcpy(file.path, "c:\\app\\sample.exe", 18);
            manifest["files"][i]["path"] = "c:\\app\\sample.exe";
            manifest["files"][i]["bytes"] = bytes.size();
            manifest["files"][i]["sha256"] = scan::Digest(bytes);
        }
        size_t at = i < 16 ? offsetof(guardian::pevolume::Header, entries) + i * sizeof(file)
                           : sizeof(guardian::pevolume::Header) + (i - 16) * sizeof(file);
        std::memcpy(built.data() + at, &file, sizeof(file));
    }
    auto &h = *reinterpret_cast<guardian::pevolume::Header *>(built.data());
    h.bytes = uint32_t(built.size());
    h.hash = guardian::pevolume::Hash(built.data(), h.bytes, true);
    volume = std::move(built);
    scan::Require(guardian::pevolume::Valid(volume.data(), uint32_t(volume.size())),
                  "Automatic package invalid");
    manifest["applicationSha256"] = scan::Digest(payload);
    manifest["runtimeSha256"] = scan::Digest(volume);
    auto text = manifest.dump(2, ' ', true);
    auto m = compatibility::Sha256({reinterpret_cast<const uint8_t *>(text.data()), text.size()}),
         v = compatibility::Sha256(volume), a = compatibility::Sha256(payload);
    std::memcpy(binding.manifestSha, m.data(), 32);
    std::memcpy(binding.runtimeSha, v.data(), 32);
    std::memcpy(binding.applicationSha, a.data(), 32);
}
} // namespace av::microvm::runtime::automatic
