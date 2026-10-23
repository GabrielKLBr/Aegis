#include "Scanner.h"
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324) // Alignment padding in the public YARA compiler structure.
#endif
#include <yara.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <algorithm>
#include <archive.h>
#include <archive_entry.h>
#include <chrono>
#include <cmath>
#include <set>

namespace av::scan
{
Json DefaultRules()
{
    return {
        {"schema", 1},
        {"license", "MIT"},
        {"notice", "MIT License\n\nCopyright (c) 2026 Aegis contributors (scanner test rules only)\n\nPermission is hereby granted, free of charge, to any person obtaining a copy\nof this software and associated documentation files (the \"Software\"), to deal\nin the Software without restriction, including without limitation the rights\nto use, copy, modify, merge, publish, distribute, sublicense, and/or sell\ncopies of the Software, and to permit persons to whom the Software is\nfurnished to do so, subject to the following conditions:\n\nThe above copyright notice and this permission notice shall be included in all\ncopies or substantial portions of the Software.\n\nTHE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\nIMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\nFITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\nAUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\nLIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\nOUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\nSOFTWARE.\n"},
        {"source", "rule Aegis_Harmless_Test { meta: classification = \"detected\" description = \"Aegis "
                   "harmless scanner test marker\" strings: $a = \"AEGIS_SCANNER_TEST_"
                   "POSITIVE_V1\" condition: $a }"}};
}
namespace
{
using Clock = std::chrono::steady_clock;
std::string Lower(std::string s)
{
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return s;
}
struct Analysis
{
    Limits limits;
    Clock::time_point deadline;
    Json result = {{"schema", 1},
                   {"classification", "unknown"},
                   {"complete", true},
                   {"findings", Json::array()},
                   {"issues", Json::array()},
                   {"members", Json::array()}};
    uint64_t expanded = 0;
    uint32_t entries = 0;
    YR_RULES *rules = nullptr;
    Json signatures;
    std::string current;
    void Issue(const std::string &path, const std::string &code)
    {
        result["complete"] = false;
        if (result["issues"].size() < 128)
            result["issues"].push_back({{"path", path}, {"code", code}});
    }
    void Check()
    {
        Require(Clock::now() < deadline, "static analysis deadline exceeded");
    }
    void Finding(const std::string &path, const std::string &kind, const std::string &rule,
                 const std::string &classification)
    {
        if (result["findings"].size() >= 128)
        {
            Issue(path, "finding limit exceeded");
            return;
        }
        result["findings"].push_back(
            {{"path", path}, {"kind", kind}, {"rule", rule}, {"classification", classification}});
    }
    static int Callback(YR_SCAN_CONTEXT *, int message, void *value, void *user)
    {
        auto &self = *static_cast<Analysis *>(user);
        if (message == CALLBACK_MSG_TOO_MANY_MATCHES || message == CALLBACK_MSG_TOO_SLOW_SCANNING)
        {
            self.Issue(self.current, "YARA match/time limit");
            return CALLBACK_ABORT;
        }
        if (message != CALLBACK_MSG_RULE_MATCHING)
            return CALLBACK_CONTINUE;
        const auto *rule = static_cast<YR_RULE *>(value);
        std::string classification = "suspicious";
        YR_META *meta = nullptr;
        yr_rule_metas_foreach(rule, meta)
        {
            if (meta->type == META_TYPE_STRING && std::string(meta->identifier) == "classification" &&
                std::string(meta->string) == "detected")
                classification = "detected";
        }
        self.Finding(self.current, "yara", rule->identifier, classification);
        if (self.result["findings"].size() >= 128)
        {
            self.Issue(self.current, "YARA finding limit");
            return CALLBACK_ABORT;
        }
        return CALLBACK_CONTINUE;
    }
    void Pe(std::span<const uint8_t> b, Json &member, const std::string &path)
    {
        auto n = [&](uint64_t p, size_t count) {
            Require(p <= b.size() && count <= b.size() - size_t(p), "PE range outside file");
        };
        auto u16 = [&](uint64_t p) {
            n(p, 2);
            return uint16_t(b[size_t(p)] | (uint16_t(b[size_t(p) + 1]) << 8));
        };
        auto u32 = [&](uint64_t p) {
            n(p, 4);
            uint32_t v = 0;
            for (unsigned i = 0; i < 4; ++i)
                v |= uint32_t(b[size_t(p) + i]) << (8 * i);
            return v;
        };
        try
        {
            const uint64_t pe = u32(0x3c);
            n(pe, 24);
            Require(u32(pe) == 0x00004550, "PE signature absent");
            const auto count = u16(pe + 6), optional = u16(pe + 20);
            Require(count && count <= 96, "PE section count rejected");
            const auto opt = pe + 24;
            n(opt, optional);
            const auto magic = u16(opt);
            Require(magic == 0x10b || magic == 0x20b, "PE optional header unsupported");
            const uint64_t directory = magic == 0x20b ? 112 : 96;
            Require(optional >= directory, "PE optional header truncated");
            const auto headers = u32(opt + 60);
            const auto table = opt + optional;
            n(table, size_t(count) * 40);
            Json sections = Json::array();
            for (uint32_t i = 0; i < count; ++i)
            {
                Check();
                const auto p = table + i * 40;
                const auto raw = u32(p + 20), bytes = u32(p + 16), flags = u32(p + 36);
                n(raw, bytes);
                std::string name;
                for (unsigned k = 0; k < 8 && b[size_t(p) + k]; ++k)
                {
                    const auto c = b[size_t(p) + k];
                    name += c >= 32 && c < 127 ? char(c) : '?';
                }
                double entropy = 0;
                if (bytes)
                {
                    uint32_t freq[256]{};
                    for (uint64_t k = raw; k < uint64_t(raw) + bytes; ++k)
                        ++freq[b[size_t(k)]];
                    for (auto f : freq)
                        if (f)
                        {
                            const double q = double(f) / bytes;
                            entropy -= q * std::log2(q);
                        }
                }
                sections.push_back(
                    {{"name", name}, {"rawBytes", bytes}, {"entropy", entropy}, {"flags", flags}});
                if ((flags & 0xa0000000u) == 0xa0000000u)
                    Finding(path, "heuristic", "PE writable executable section", "suspicious");
            }
            auto offset = [&](uint32_t rva, size_t bytes) -> uint64_t {
                if (rva < headers)
                {
                    n(rva, bytes);
                    Require(bytes <= uint64_t(headers) - rva, "PE header RVA crosses range");
                    return rva;
                }
                for (uint32_t i = 0; i < count; ++i)
                {
                    const auto p = table + i * 40;
                    const auto va = u32(p + 12), raw = u32(p + 20), length = u32(p + 16);
                    if (rva >= va && uint64_t(rva) - va < length)
                    {
                        const auto delta = uint64_t(rva) - va;
                        Require(bytes <= length - delta, "PE RVA crosses raw section");
                        n(uint64_t(raw) + delta, bytes);
                        return uint64_t(raw) + delta;
                    }
                }
                throw std::runtime_error("PE RVA unmapped");
            };
            auto text = [&](uint32_t rva) {
                std::string s;
                for (uint32_t i = 0; i < 256; ++i)
                {
                    Require(rva <= UINT32_MAX - i, "PE string RVA overflow");
                    const auto c = b[size_t(offset(rva + i, 1))];
                    if (!c)
                        return s;
                    Require(c >= 32 && c < 127, "PE import name invalid");
                    s += char(c);
                }
                throw std::runtime_error("PE import string limit");
            };
            Json imports = Json::array();
            const auto dirCount = u32(opt + directory - 4);
            if (dirCount > 1)
            {
                Require(optional >= directory + 16, "PE import directory truncated");
                const auto rva = u32(opt + directory + 8), size = u32(opt + directory + 12);
                if (rva)
                {
                    Require(size >= 20 && size <= 20 * 257, "PE import directory limit");
                    bool ended = false;
                    for (uint32_t i = 0; i < size / 20; ++i)
                    {
                        Check();
                        Require(uint64_t(rva) + uint64_t(i) * 20 <= UINT32_MAX, "PE import RVA overflow");
                        const auto p = offset(rva + i * 20, 20);
                        bool zero = true;
                        for (unsigned k = 0; k < 20; ++k)
                            zero = zero && b[size_t(p) + k] == 0;
                        if (zero)
                        {
                            ended = true;
                            break;
                        }
                        imports.push_back(text(u32(p + 12)));
                    }
                    Require(ended, "PE import terminator absent");
                }
            }
            member["pe"] = {{"machine", u16(pe + 4)},
                            {"bits", magic == 0x20b ? 64 : 32},
                            {"sections", sections},
                            {"importDlls", imports},
                            {"authenticode", "not-verified"}};
        }
        catch (const std::exception &e)
        {
            Issue(path, e.what());
        }
    }
    static bool SafeName(std::string s)
    {
        if (s.empty() || s.size() > 1024)
            return false;
        std::replace(s.begin(), s.end(), '\\', '/');
        if (s.front() == '/' || s.find(':') != s.npos)
            return false;
        size_t at = 0;
        while (at < s.size())
        {
            const auto end = s.find('/', at);
            const auto part = s.substr(at, end == s.npos ? s.npos : end - at);
            if (part == ".." || part == ".")
                return false;
            if (end == s.npos)
                break;
            at = end + 1;
        }
        return true;
    }
    void Walk(std::span<const uint8_t> b, const std::string &path, uint32_t depth)
    {
        Check();
        Require(entries++ <= limits.entries, "member count limit exceeded");
        Require(b.size() <= limits.fileBytes, "member size limit exceeded");
        Json member = {{"path", path}, {"bytes", b.size()}, {"sha256", Digest(b)}};
        const std::string content(reinterpret_cast<const char *>(b.data()), b.size());
        const auto eicar = std::string("EICAR-STANDARD-") + "ANTIVIRUS-" + "TEST-FILE";
        if (content.find(eicar) != content.npos)
            Finding(path, "test-signature", "EICAR-Test-File", "detected");
        for (const auto &s : signatures)
        {
            Check();
            const auto type = s.at("type").get<std::string>(), value = s.at("value").get<std::string>();
            if ((type == "sha256" && value == member["sha256"].get<std::string>()) ||
                (type == "substr" && content.find(value) != content.npos))
                Finding(path, "signature", s.at("name"), "detected");
        }
        current = path;
        const auto remaining =
            std::chrono::duration_cast<std::chrono::seconds>(deadline - Clock::now()).count();
        const auto status = yr_rules_scan_mem(rules, b.data(), b.size(), SCAN_FLAGS_FAST_MODE, Callback, this,
                                              int(std::max<int64_t>(1, remaining)));
        if (status != ERROR_SUCCESS)
            Issue(path, "YARA scan error/timeout " + std::to_string(status));
        Check();
        const auto ext = Lower(PathText(Utf8Path(path).extension()));
        bool mz = b.size() >= 2 && b[0] == 'M' && b[1] == 'Z';
        if (mz)
            Pe(b, member, path);
        const bool script = ext == ".ps1" || ext == ".js" || ext == ".vbs" || ext == ".bat" ||
                            ext == ".cmd" || ext == ".hta" || ext == ".sh" || ext == ".py";
        if (script)
        {
            std::string normalized = Lower(content);
            if (b.size() >= 2 && b[0] == 0xff && b[1] == 0xfe)
            {
                normalized.clear();
                for (size_t i = 2; i + 1 < b.size(); i += 2)
                    normalized += b[i + 1] == 0 ? char(b[i]) : '?';
                normalized = Lower(normalized);
            }
            member["format"] = "script";
            const bool encoded = normalized.find("frombase64string") != normalized.npos ||
                                 normalized.find("-encodedcommand") != normalized.npos;
            const bool execution = normalized.find("invoke-expression") != normalized.npos ||
                                   normalized.find("iex ") != normalized.npos;
            const bool download = normalized.find("downloadstring") != normalized.npos ||
                                  normalized.find("invoke-webrequest") != normalized.npos;
            if (execution && (encoded || download))
                Finding(path, "heuristic", "script combines dynamic execution with encoding/download",
                        "suspicious");
        }
        const bool archiveMagic = (b.size() >= 4 && b[0] == 'P' && b[1] == 'K') ||
                                  (b.size() >= 2 && b[0] == 0x1f && b[1] == 0x8b) ||
                                  (b.size() > 262 && content.substr(257, 5) == "ustar") ||
                                  (b.size() >= 4 && content.substr(0, 4) == "MSCF");
        const bool archiveExt = ext == ".zip" || ext == ".tar" || ext == ".gz" || ext == ".tgz" ||
                                ext == ".cab" || ext == ".msix" || ext == ".appx" || ext == ".msixbundle" ||
                                ext == ".appxbundle";
        const bool unsupported = ext == ".7z" || ext == ".rar" || ext == ".xz" || ext == ".bz2" ||
                                 ext == ".iso" || ext == ".msi" || ext == ".msp";
        if (unsupported)
            Issue(path, "format unsupported by bounded reader");
        if (mz && (Lower(content).find("nullsoft") != content.npos ||
                   Lower(content).find("inno setup") != content.npos))
            Issue(path, "embedded installer payload unsupported");
        result["members"].push_back(std::move(member));
        if (!archiveMagic && !archiveExt)
            return;
        if (depth >= limits.depth)
        {
            Issue(path, "archive depth limit exceeded");
            return;
        }
        auto *ar = archive_read_new();
        Require(ar != nullptr, "archive allocation failed");
        try
        {
            archive_read_support_filter_none(ar);
            archive_read_support_filter_gzip(ar);
            archive_read_support_format_zip(ar);
            archive_read_support_format_tar(ar);
            archive_read_support_format_cab(ar);
            if (ext == ".gz" || ext == ".tgz")
                archive_read_support_format_raw(ar);
            Require(archive_read_open_memory(ar, b.data(), b.size()) == ARCHIVE_OK,
                    "archive open failed/unsupported compression");
            archive_entry *entry = nullptr;
            std::set<std::string> names;
            int rc = 0;
            while ((rc = archive_read_next_header(ar, &entry)) == ARCHIVE_OK)
            {
                Check();
                const auto *raw = archive_entry_pathname_utf8(entry);
                Require(raw != nullptr, "archive filename encoding unsupported");
                std::string name = raw;
                Require(SafeName(name), "archive unsafe member path");
                std::replace(name.begin(), name.end(), '\\', '/');
                Require(names.insert(Lower(name)).second, "archive duplicate member name");
                Require(archive_entry_is_encrypted(entry) == 0, "encrypted archive unsupported");
                Require(!archive_entry_symlink(entry) && !archive_entry_hardlink(entry),
                        "archive link rejected");
                const auto type = archive_entry_filetype(entry);
                if (type == AE_IFDIR)
                    continue;
                Require(type == AE_IFREG, "archive special member rejected");
                Require(entries <= limits.entries, "member count limit exceeded");
                const auto size = archive_entry_size(entry);
                const bool knownSize = archive_entry_size_is_set(entry) != 0;
                Require(!knownSize || (size >= 0 && uint64_t(size) <= limits.fileBytes &&
                                       uint64_t(size) <= limits.expandedBytes - expanded),
                        "archive expansion size limit exceeded");
                std::vector<uint8_t> data;
                uint8_t block[32768];
                for (;;)
                {
                    Check();
                    const auto read = archive_read_data(ar, block, sizeof(block));
                    Require(read >= 0, "archive decompression failed");
                    if (!read)
                        break;
                    const auto bytes = size_t(read);
                    Require(bytes <= limits.expandedBytes - expanded &&
                                bytes <= limits.fileBytes - data.size(),
                            "archive expansion limit exceeded");
                    expanded += bytes;
                    data.insert(data.end(), block, block + bytes);
                }
                Require(!knownSize || data.size() == uint64_t(size), "archive declared size mismatch");
                Walk(data, path + "!" + name, depth + 1);
            }
            Require(rc == ARCHIVE_EOF, "archive header truncated/unsupported");
            Require(archive_read_has_encrypted_entries(ar) <= 0, "encrypted archive unsupported");
            Require(archive_read_close(ar) == ARCHIVE_OK, "archive integrity failure");
            archive_read_free(ar);
        }
        catch (const std::exception &e)
        {
            archive_read_free(ar);
            Issue(path, e.what());
        }
    }
};
} // namespace
Json Inspect(std::span<const uint8_t> bytes, const std::string &name, const Json &package,
             const Json &signatures, const Limits &limits)
{
    Analysis a;
    a.limits = Limits::Parse(limits.ToJson());
    a.deadline = Clock::now() + std::chrono::milliseconds(limits.milliseconds);
    a.signatures = signatures;
    bool initialized = false;
    YR_COMPILER *compiler = nullptr;
    try
    {
        Require(package.is_object() && package.value("schema", 0) == 1, "YARA package schema rejected");
        const auto license = package.value("license", std::string());
        Require(license == "MIT" || license == "BSD-3-Clause" || license == "Apache-2.0" ||
                    license == "CC0-1.0",
                "YARA package license unsupported/missing");
        Require(!package.value("notice", std::string()).empty(), "YARA license notice missing");
        const auto source = package.at("source").get<std::string>();
        Require(!source.empty() && source.size() <= 65536 && source.find('\0') == source.npos,
                "YARA source limit/encoding rejected");
        Require(signatures.is_array() && signatures.size() <= 4096, "signature count limit exceeded");
        for (const auto &s : signatures)
        {
            const auto type = s.at("type").get<std::string>(), value = s.at("value").get<std::string>(),
                       label = s.at("name").get<std::string>();
            Require(!label.empty() && label.size() <= 256 && !value.empty() && value.size() <= 4096,
                    "signature fields rejected");
            Require(type == "substr" || (type == "sha256" && value.size() == 64 &&
                                         value.find_first_not_of("0123456789abcdef") == value.npos),
                    "signature type/hash invalid");
        }
        Require(yr_initialize() == ERROR_SUCCESS, "YARA initialization failed");
        initialized = true;
        Require(yr_compiler_create(&compiler) == ERROR_SUCCESS, "YARA compiler allocation failed");
        yr_compiler_set_include_callback(compiler, nullptr, nullptr, nullptr);
        Require(yr_compiler_add_string(compiler, source.c_str(), "licensed") == 0,
                "YARA compilation failed (includes disabled)");
        Require(yr_compiler_get_rules(compiler, &a.rules) == ERROR_SUCCESS, "YARA compilation failed");
        yr_compiler_destroy(compiler);
        compiler = nullptr;
        a.Walk(bytes, name, 0);
    }
    catch (const std::exception &e)
    {
        a.Issue(name, e.what());
    }
    if (compiler)
        yr_compiler_destroy(compiler);
    if (a.rules)
        yr_rules_destroy(a.rules);
    if (initialized)
        yr_finalize();
    std::string classification = a.result["complete"].get<bool>() ? "unknown" : "inconclusive";
    for (const auto &f : a.result["findings"])
    {
        const auto value = f.at("classification").get<std::string>();
        if (value == "detected")
            classification = "detected";
        else if (classification != "detected")
            classification = "suspicious";
    }
    a.result["classification"] = classification;
    a.result["expandedBytes"] = a.expanded;
    return a.result;
}
} // namespace av::scan
