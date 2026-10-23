#include "Scanner.h"
#include <functional>
#include <iostream>
namespace av::scan
{
int CliMain(const std::vector<std::string> &args)
{
    try
    {
        if (args.empty() || args[0] == "--help")
        {
            std::cout << "Aegis scanner - phase 14\n  guardian-scan scan <file-or-directory> [...] [--config "
                         "config.json] [--signatures file] [--json]\n  guardian-scan queue [--config "
                         "config.json]\n  guardian-scan sandbox-next [--config config.json]\nOffline by "
                         "default. No sample uploads. Exit: 0=unknown/no match, 1=detected or suspicious, "
                         "2=usage/config error, 3=inconclusive.\n";
            return 0;
        }
        const auto command = args[0];
        Require(command == "scan" || command == "queue" || command == "sandbox-next",
                "Unknown scanner command");
        Config config;
        fs::path signatureOverride;
        std::vector<fs::path> paths;
        bool options = true, configured = false;
        for (size_t i = 1; i < args.size(); ++i)
        {
            const auto &a = args[i];
            if (options && a == "--")
            {
                options = false;
                continue;
            }
            if (options && a == "--json")
                continue;
            if (options && (a == "--config" || a == "--signatures"))
            {
                Require(++i < args.size(), "Missing option value");
                const auto path = fs::absolute(Utf8Path(args[i]));
                if (a == "--config")
                {
                    config = Config::Parse(ReadJson(path), path.parent_path());
                    configured = true;
                }
                else
                    signatureOverride = path;
                continue;
            }
            Require(!options || !a.starts_with('-'), "Unknown option");
            paths.push_back(Utf8Path(a));
        }
        if (!configured)
            config = UserConfig();
        if (!signatureOverride.empty())
            config.signatures = signatureOverride;
        if (config.signatures.empty() && fs::exists(Executable().parent_path() / "signatures.txt"))
            config.signatures = Executable().parent_path() / "signatures.txt";
        Require(command == "scan" ? !paths.empty() : paths.empty(), "Invalid file arguments");
        Engine engine(config);
        if (command == "queue")
        {
            std::cout << Json{{"schema", 1}, {"jobs", engine.Queue()}}.dump(-1, ' ', false,
                                                                            Json::error_handler_t::replace)
                      << '\n';
            return 0;
        }
        if (command == "sandbox-next")
        {
            const auto r = engine.DispatchOne();
            std::cout << r.dump() << '\n';
            return r.value("state", std::string()) == "empty" || r.value("complete", false) ? 0 : 3;
        }
        Json rows = Json::array();
        uint64_t detected = 0, suspicious = 0, inconclusive = 0, unknown = 0;
        bool truncated = false;
        uint64_t reportBytes = 0;
        auto record = [&](const Json &row) {
            const auto bytes = row.dump(-1, ' ', false, Json::error_handler_t::replace).size();
            if (rows.size() >= 10000 || bytes > (32ull << 20) - reportBytes)
            {
                truncated = true;
                return false;
            }
            reportBytes += bytes;
            rows.push_back(row);
            return true;
        };
        auto issue = [&](const fs::path &path, const std::string &reason) {
            ++inconclusive;
            record({{"path", PathText(path)},
                    {"schema", 1},
                    {"classification", "inconclusive"},
                    {"complete", false},
                    {"issues", Json::array({{{"code", reason}}})}});
        };
        std::function<void(const fs::path &, uint32_t)> walk = [&](const fs::path &path, uint32_t depth) {
            if (rows.size() >= 10000)
            {
                truncated = true;
                return;
            }
            std::error_code ec;
            const auto st = fs::symlink_status(path, ec);
            if (ec)
            {
                issue(path, "cannot inspect input");
                return;
            }
            if (fs::is_regular_file(st) && !fs::is_symlink(st))
            {
                const auto r = engine.Analyze(path);
                if (!record(r))
                    return;
                const auto c = r.at("classification").get<std::string>();
                if (!r.value("complete", false))
                    ++inconclusive;
                if (c == "detected")
                    ++detected;
                else if (c == "suspicious")
                    ++suspicious;
                else if (c == "unknown")
                    ++unknown;
                return;
            }
            if (!fs::is_directory(st) || fs::is_symlink(st))
            {
                issue(path, "link/special input rejected");
                return;
            }
            if (depth >= 64)
            {
                issue(path, "directory depth limit");
                return;
            }
            try
            {
                OrdinaryDirectory(path);
                fs::directory_iterator it(path, ec), end;
                if (ec)
                {
                    issue(path, "cannot list directory");
                    return;
                }
                for (; it != end; it.increment(ec))
                {
                    if (ec)
                    {
                        issue(path, "directory enumeration failed");
                        break;
                    }
                    walk(it->path(), depth + 1);
                    if (truncated)
                        break;
                }
            }
            catch (const std::exception &e)
            {
                issue(path, e.what());
            }
        };
        for (const auto &path : paths)
        {
            walk(path, 0);
            if (truncated)
                break;
        }
        uint64_t files = 0, errors = 0, partial = 0, skipped = 0;
        for (auto &row : rows)
        {
            row["threat"] = "";
            row["reason"] = "";
            row["status"] = row.at("classification");
            if (row.contains("sha256"))
            {
                ++files;
                row["partial"] = !row.value("complete", false);
                if (row["partial"].get<bool>())
                    ++partial;
            }
            else
            {
                row["sha256"] = "";
                row["partial"] = false;
                ++errors;
            }
            if (row.contains("findings") && !row.at("findings").empty())
                row["threat"] = row["findings"][0].at("rule");
        }
        Json out = {{"schema", 1},
                    {"results", rows},
                    {"filesScanned", files},
                    {"threatsFound", detected + suspicious},
                    {"errors", errors},
                    {"partial", partial},
                    {"skipped", skipped},
                    {"summary",
                     {{"detected", detected},
                      {"suspicious", suspicious},
                      {"unknown", unknown},
                      {"inconclusive", inconclusive},
                      {"truncated", truncated}}}};
        auto output = out.dump(-1, ' ', false, Json::error_handler_t::replace);
        size_t at = 0;
        while ((at = output.find(char(127), at)) != output.npos)
        {
            output.replace(at, 1, "\\u007f");
            at += 6;
        }
        std::cout << output << '\n';
        return inconclusive || truncated ? 3 : (detected || suspicious ? 1 : 0);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Scanner: " << e.what() << '\n';
        return 2;
    }
}
} // namespace av::scan
