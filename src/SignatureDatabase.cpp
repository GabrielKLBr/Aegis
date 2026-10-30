#include "SignatureDatabase.h"
#include <mutex>
namespace av
{
namespace
{
std::mutex engineMutex;
}
bool SignatureDatabase::Load(const std::wstring &path)
{
    std::lock_guard lock(engineMutex);
    engine_.reset();
    loadError_.clear();
    try
    {
        scan::Config config = scan::UserConfig();
        if (std::filesystem::exists(std::filesystem::path(path)))
            config.signatures = path;
        else
        {
            engine_ = std::make_shared<scan::Engine>(config);
            return false;
        }
        engine_ = std::make_shared<scan::Engine>(config);
        return true;
    }
    catch (const std::exception &e)
    {
        loadError_ = e.what();
        return false;
    }
}
scan::Json SignatureDatabase::Analyze(const std::wstring &path, const std::atomic<bool> *cancel) const
{
    try
    {
        std::shared_ptr<scan::Engine> engine;
        {
            std::lock_guard lock(engineMutex);
            if (!loadError_.empty())
                throw std::runtime_error(loadError_);
            if (!engine_)
                engine_ = std::make_shared<scan::Engine>(scan::UserConfig());
            engine = engine_;
        }
        return engine->Analyze(std::filesystem::path(path), cancel);
    }
    catch (const std::exception &e)
    {
        return {{"schema", 1},
                {"classification", "inconclusive"},
                {"complete", false},
                {"findings", scan::Json::array()},
                {"issues", scan::Json::array({{{"code", e.what()}}})}};
    }
}
bool SignatureDatabase::Match(const std::wstring &path, Detection &out, const std::string *) const
{
    const auto r = Analyze(path);
    if (r.at("classification") != "detected")
        return false;
    for (const auto &f : r.at("findings"))
        if (f.at("classification") == "detected")
        {
            out = {path, Utf8ToWide(f.at("rule").get<std::string>()),
                   Utf8ToWide(f.at("kind").get<std::string>()), r.value("sha256",std::string())};
            return true;
        }
    return false;
}
} // namespace av
