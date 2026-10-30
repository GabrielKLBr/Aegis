#pragma once
#include <atomic>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>
namespace av::scan
{
using Json = nlohmann::json;
namespace fs = std::filesystem;
struct Limits
{
    uint64_t fileBytes = 32ull << 20, expandedBytes = 64ull << 20;
    uint32_t entries = 256, depth = 3, milliseconds = 5000;
    Json ToJson() const;
    static Limits Parse(const Json &);
};
struct Config
{
    fs::path state, signatures, rulePackage, sandboxRuntime;
    Limits limits;
    bool offline = true, queueUnknown = false;
    uint32_t cacheSeconds = 3600;
    Json providers = Json::array();
    static Config Parse(const Json &, const fs::path &base);
};
class Engine
{
  public:
    explicit Engine(Config = {});
    ~Engine();
    Engine(const Engine &) = delete;
    Json Analyze(const fs::path &, const std::atomic<bool> *cancel = nullptr);
    Json Queue();
    Json DispatchOne(const std::atomic<bool> *cancel = nullptr);
    size_t SignatureCount() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::string PathText(const fs::path &);
fs::path Utf8Path(const std::string &);
fs::path Executable();
Json DefaultRules();
Json Inspect(std::span<const uint8_t>, const std::string &name, const Json &rules, const Json &signatures,
             const Limits &);
std::string Digest(std::span<const uint8_t>, const std::string &algorithm = "sha256");
std::vector<uint8_t> Read(const fs::path &, uint64_t limit);
void Write(const fs::path &, std::span<const uint8_t>);
void WriteJson(const fs::path &, const Json &);
Json ReadJson(const fs::path &, uint64_t limit = 2ull << 20);
void Require(bool, const std::string &);
void OrdinaryDirectory(const fs::path &);
void PrivateState(const fs::path &);
fs::path DefaultState();
Config UserConfig();
std::string Token();
Json RunWorker(const fs::path &job, const Json &request, const std::atomic<bool> *cancel);
int WorkerMain(const fs::path &);
int CliMain(const std::vector<std::string> &);
} // namespace av::scan
