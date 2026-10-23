#include "Store.h"
#include <algorithm>
#include <chrono>
#include <mutex>
#include <sstream>

#ifndef GUARDIAN_SCANNER_REV
#define GUARDIAN_SCANNER_REV "phase14-dev"
#endif
#ifndef GUARDIAN_SCANNER_CONFIGURATION
#define GUARDIAN_SCANNER_CONFIGURATION "unspecified"
#endif
namespace av::scan
{
namespace
{
int64_t Now()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
struct Job
{
    fs::path folder;
    explicit Job(const fs::path &root)
    {
        OrdinaryDirectory(root);
        size_t count = 0;
        for (const auto &e : fs::directory_iterator(root))
        {
            (void)e;
            ++count;
        }
        Require(count < 64, "temporary job directory limit exceeded");
        folder = root / Token();
        Require(fs::create_directory(folder), "Cannot create scanner job");
        OrdinaryDirectory(folder);
    }
    ~Job()
    {
        std::error_code ec;
        for (const auto *n : {"payload", "request.json", "result.json"})
            fs::remove(folder / n, ec);
        fs::remove(folder, ec);
    }
};
std::string Trim(const std::string &s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == s.npos)
        return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
Json Signatures(const fs::path &p)
{
    Json rules = Json::array();
    if (p.empty())
        return rules;
    const auto bytes = Read(p, 1 << 20);
    std::istringstream text(std::string(bytes.begin(), bytes.end()));
    std::string line;
    while (std::getline(text, line))
    {
        line = Trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        const auto a = line.find(':'), b = a == line.npos ? line.npos : line.find(':', a + 1);
        Require(a != line.npos && b != line.npos, "Malformed signature line");
        const auto type = line.substr(0, a);
        auto value = line.substr(a + 1, b - a - 1);
        const auto name = line.substr(b + 1);
        if (type == "sha256")
            for (auto &c : value)
                if (c >= 'A' && c <= 'F')
                    c = char(c - 'A' + 'a');
        Require((type == "substr" && !value.empty() && value.size() <= 4096) ||
                    (type == "sha256" && value.size() == 64 &&
                     value.find_first_not_of("0123456789abcdef") == value.npos),
                "Invalid signature type/value");
        Require(
            !name.empty() && name.size() <= 256 && rules.size() < 4096 && line.find('\0') == line.npos &&
                std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }),
            "Signature label/count limit");
        rules.push_back({{"type", type}, {"value", value}, {"name", name}});
    }
    return rules;
}
Json Failure(const std::string &why)
{
    return {{"schema", 1},
            {"classification", "inconclusive"},
            {"complete", false},
            {"findings", Json::array()},
            {"issues", Json::array({{{"code", why}}})},
            {"members", Json::array()}};
}
void Incomplete(Json &j, const std::string &why)
{
    j["complete"] = false;
    if (j.value("classification", std::string()) == "unknown")
        j["classification"] = "inconclusive";
    if (!j.contains("issues"))
        j["issues"] = Json::array();
    j["issues"].push_back({{"code", why}});
}
} // namespace
struct Engine::Impl
{
    Config config;
    Json rules, signatures;
    std::string fingerprint;
    std::unique_ptr<Store> store;
    std::mutex mutex;
    explicit Impl(Config c) : config(std::move(c))
    {
        const auto validated = Config::Parse({{"schema", 1},
                                              {"offline", config.offline},
                                              {"providers", config.providers},
                                              {"cacheSeconds", config.cacheSeconds},
                                              {"limits", config.limits.ToJson()}},
                                             {});
        config.limits = validated.limits;
        if (config.state.empty())
            config.state = DefaultState();
        config.state = fs::absolute(config.state).lexically_normal();
        PrivateState(config.state);
        for (const auto *d : {"jobs", "payloads"})
        {
            fs::create_directory(config.state / d);
            OrdinaryDirectory(config.state / d);
        }
        store = std::make_unique<Store>(config.state / "scanner.sqlite");
        rules = config.rulePackage.empty() ? DefaultRules() : ReadJson(config.rulePackage, 128 << 10);
        signatures = Signatures(config.signatures);
        const auto text = Json{{"engine", GUARDIAN_SCANNER_REV},
                               {"configuration", GUARDIAN_SCANNER_CONFIGURATION},
                               {"rules", rules},
                               {"signatures", signatures},
                               {"limits", config.limits.ToJson()}}
                              .dump();
        fingerprint = Digest({reinterpret_cast<const uint8_t *>(text.data()), text.size()});
    }
    Json Request(const std::string &action, const std::string &name, const std::string &hash)
    {
        const auto safeName = Json::parse(Json(name).dump(-1, ' ', false, Json::error_handler_t::replace));
        return {{"schema", 1},
                {"action", action},
                {"name", safeName},
                {"sha256", hash},
                {"limits", config.limits.ToJson()},
                {"rules", rules},
                {"signatures", signatures}};
    }
};
Engine::Engine(Config c) : impl_(std::make_unique<Impl>(std::move(c)))
{
}
Engine::~Engine() = default;
size_t Engine::SignatureCount() const
{
    return impl_->signatures.size();
}
Json Engine::Analyze(const fs::path &path, const std::atomic<bool> *cancel)
{
    std::lock_guard lock(impl_->mutex);
    auto &p = *impl_;
    Json result;
    try
    {
        Require(!(cancel && cancel->load()), "analysis cancelled");
        const auto bytes = Read(path, p.config.limits.fileBytes);
        const auto hash = Digest(bytes), name = PathText(path.filename());
        Require(name.size() <= 512, "Input filename limit exceeded");
        const auto key = hash + ":" + p.fingerprint + ":" + name;
        result = p.store->Cached("static", key, Now());
        const bool cached = !result.is_null();
        if (!cached)
        {
            Job job(p.config.state / "jobs");
            Write(job.folder / "payload", bytes);
            result = RunWorker(job.folder, p.Request("static", name, hash), cancel);
            if (result.value("complete", false))
                p.store->Cache("static", key, result, Now(), p.config.cacheSeconds);
        }
        Require(result.is_object() && result.value("schema", 0) == 1 && result.at("complete").is_boolean() &&
                    result.at("findings").is_array() && result.at("issues").is_array(),
                "Cached/static result schema rejected");
        const auto classification = result.at("classification").get<std::string>();
        Require(classification == "unknown" || classification == "inconclusive" ||
                    classification == "detected" || classification == "suspicious",
                "Cached/static classification rejected");
        result["cached"] = cached;
        result["sha256"] = hash;
        result["sha1"] = Digest(bytes, "sha1");
        result["md5"] = Digest(bytes, "md5");
        result["bytes"] = bytes.size();
        result["engineFingerprint"] = p.fingerprint;
        const auto intelligence = Enrich(*p.store, p.config, hash, Now());
        result["intelligence"] = intelligence;
        for (const auto &row : intelligence)
            if (row.value("classification", std::string()) == "suspicious" &&
                result["classification"] != "detected")
            {
                result["classification"] = "suspicious";
                result["findings"].push_back(
                    {{"path", name},
                     {"kind", "reputation"},
                     {"rule", row.at("provider").get<std::string>() + " reported this SHA256"},
                     {"classification", "suspicious"}});
            }
        if (p.config.queueUnknown && result["classification"] == "unknown")
        {
            const auto payload = p.config.state / "payloads" / hash;
            uint64_t used = 0;
            for (const auto &e : fs::directory_iterator(payload.parent_path()))
            {
                const auto st = e.symlink_status();
                Require(fs::is_regular_file(st) && !fs::is_symlink(st), "Unexpected queue member");
                const auto size = e.file_size();
                Require(size <= (512ull << 20) - used, "Sandbox queue disk limit exceeded");
                used += size;
            }
            const bool created = !fs::exists(payload);
            if (created)
            {
                Require(bytes.size() <= (512ull << 20) - used, "Sandbox queue disk limit exceeded");
                Write(payload, bytes);
            }
            else
                Require(Digest(Read(payload, p.config.limits.fileBytes)) == hash,
                        "Queue payload integrity failure");
            try
            {
                result["sandbox"] = p.store->Enqueue(hash, name, payload, Now());
            }
            catch (...)
            {
                if (created)
                {
                    std::error_code ec;
                    fs::remove(payload, ec);
                }
                throw;
            }
        }
    }
    catch (const std::exception &e)
    {
        const auto classification = result.is_object() ? result.value("classification", Json()) : Json();
        if (result.is_object() &&
            (classification == "unknown" || classification == "inconclusive" ||
             classification == "detected" || classification == "suspicious") &&
            result.contains("issues") && result["issues"].is_array())
            Incomplete(result, e.what());
        else
            result = Failure(e.what());
    }
    result["path"] = PathText(path);
    result["offline"] = p.config.offline;
    return result;
}
Json Engine::Queue()
{
    std::lock_guard lock(impl_->mutex);
    return impl_->store->Jobs();
}
Json Engine::DispatchOne(const std::atomic<bool> *cancel)
{
    std::lock_guard lock(impl_->mutex);
    auto &p = *impl_;
    const auto entry = p.store->Claim();
    if (entry.is_null())
        return {{"schema", 1}, {"state", "empty"}};
    Json result;
    try
    {
        const auto hash = entry.at("sha256").get<std::string>();
        Require(hash.size() == 64 && hash.find_first_not_of("0123456789abcdef") == hash.npos,
                "Queue hash rejected");
        const auto payload = p.config.state / "payloads" / hash;
        Require(PathText(payload) == entry.at("payload").get<std::string>(), "Queue path rejected");
        const auto bytes = Read(payload, p.config.limits.fileBytes);
        Require(Digest(bytes) == hash, "Queue payload integrity failure");
        Job job(p.config.state / "jobs");
        Write(job.folder / "payload", bytes);
        result = RunWorker(job.folder, p.Request("sandbox", entry.at("name"), hash), cancel);
    }
    catch (const std::exception &e)
    {
        result = Failure(e.what());
    }
    result["id"] = entry.at("id");
    result["sha256"] = entry.at("sha256");
    result["engineFingerprint"] = p.fingerprint;
    p.store->Finish(entry.at("id"), result);
    return result;
}
} // namespace av::scan
