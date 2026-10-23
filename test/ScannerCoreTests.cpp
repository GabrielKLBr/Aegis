#include "scanner/Store.h"
#include <iostream>
#include <thread>
using namespace av::scan;
namespace
{
void Check(bool b, const char *s)
{
    if (!b)
        throw std::runtime_error(s);
}
template <class F> void Reject(F f)
{
    bool failed = false;
    try
    {
        f();
    }
    catch (const std::exception &)
    {
        failed = true;
    }
    Check(failed, "expected explicit rejection");
}
std::span<const uint8_t> Bytes(const std::string &s)
{
    return {reinterpret_cast<const uint8_t *>(s.data()), s.size()};
}
} // namespace
int main(int argc, char **argv)
{
    try
    {
        Check(argc == 2, "test directory required");
        const auto root = fs::absolute(Utf8Path(argv[1])) / Token();
        PrivateState(root);
        const std::string text = "abc", hash = Digest(Bytes(text));
        Check(hash == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA256 vector");
        Check(Digest(Bytes(text), "sha1") == "a9993e364706816aba3e25717850c26c9cd0d89d", "SHA1 vector");
        Check(Digest(Bytes(text), "md5") == "900150983cd24fb0d6963f7d28e17f72", "MD5 vector");
        auto r = Inspect(Bytes(text), "normal.txt", DefaultRules(), Json::array(), {});
        Check(r["complete"] == true && r["classification"] == "unknown", "clean corpus unknown");
        const std::string positive = std::string("AEGIS_SCANNER_TEST_") + "POSITIVE_V1";
        r = Inspect(Bytes(positive), "test.txt", DefaultRules(), Json::array(), {});
        Check(r["classification"] == "detected" && r["complete"] == true, "real YARA positive");
        auto rule = DefaultRules();
        rule.erase("license");
        r = Inspect(Bytes(text), "normal.txt", rule, Json::array(), {});
        Check(r["complete"] == false && r["classification"] == "inconclusive", "license validation");
        rule = DefaultRules();
        rule["source"] = "include \"private-canary\"";
        r = Inspect(Bytes(text), "normal.txt", rule, Json::array(), {});
        Check(!r["complete"].get<bool>(), "YARA includes disabled");
        const std::string mz = "MZbad";
        r = Inspect(Bytes(mz), "broken.exe", DefaultRules(), Json::array(), {});
        Check(r["complete"] == false && r["classification"] == "inconclusive", "malformed PE");
        const std::string script = "Invoke-Expression ([Convert]::FromBase64String('YWJj'))";
        r = Inspect(Bytes(script), "test.ps1", DefaultRules(), Json::array(), {});
        Check(r["classification"] == "suspicious", "heuristic distinct from detection");
        Reject([] { Limits::Parse({{"depth", 99}}); });
        Reject([] { Limits::Parse({{"fileBytes", -1}}); });
        Reject([] {
            Config::Parse({{"schema", 1},
                           {"providers", Json::array({{{"id", "virustotal"},
                                                       {"enabled", true},
                                                       {"termsAccepted", true},
                                                       {"keyEnv", "KEY"}}})}},
                          {});
        });
        {
            Store db(root / "test.sqlite");
            db.Cache("test", "k", {{"v", 1}}, 100, 10);
            Check(db.Cached("test", "k", 109)["v"] == 1, "cache hit");
            Check(db.Cached("test", "k", 110).is_null(), "cache expiration");
            Check(db.Admit("p", 120, 1, 2), "rate admit");
            Check(!db.Admit("p", 121, 1, 2), "minute quota");
            Check(db.Admit("p", 180, 1, 2), "new minute");
            Check(!db.Admit("p", 240, 1, 2), "day quota");
            Check(db.Admit("p", 86400, 1, 2), "new day");
            db.Backoff("p", 87000);
            Check(!db.Admit("p", 86500, 1, 2), "persistent backoff");
        }
        {
            Store db(root / "test.sqlite");
            Check(db.Cached("test", "k", 105)["v"] == 1, "cache persists");
            Check(!db.Admit("p", 86500, 1, 2), "quota persists");
            Config c;
            c.offline = false;
            c.providers = Json::array({{{"id", "circl"}, {"enabled", true}, {"termsAccepted", true}}});
            int calls = 0;
            auto transport = [&](const HttpRequest &req) {
                ++calls;
                Check(req.method == "GET" && req.url.ends_with(hash) && req.body.empty(), "hash only");
                return HttpResponse{200, Json{{"SHA-256", hash}}.dump(), {}};
            };
            auto rows = Enrich(db, c, hash, 1000, transport);
            Check(calls == 1 && rows[0]["status"] == "found" && rows[0]["classification"] == "unknown",
                  "known hash is not safe verdict");
            rows = Enrich(db, c, hash, 1001, transport);
            Check(calls == 1 && rows[0]["cached"] == true, "intelligence cache");
            c.offline = true;
            const auto other = std::string(64, 'a');
            rows = Enrich(db, c, other, 1002, transport);
            Check(calls == 1 && rows[0]["status"] == "offline", "offline no transport");
            c.offline = false;
            rows =
                Enrich(db, c, other, 1100, [](const HttpRequest &) { return HttpResponse{429, "", "90"}; });
            Check(rows[0]["status"] == "rate-limited", "429 backoff");
            rows = Enrich(db, c, other, 1101, transport);
            Check(calls == 1 && rows[0]["status"] == "rate-limited", "no retry flood");
            auto j = db.Enqueue(hash, "test", root / "payload", 2000);
            auto duplicate = db.Enqueue(hash, "test", root / "payload", 2001);
            Check(duplicate["duplicate"] == true && j["id"] == duplicate["id"], "queue dedupe");
            auto claim = db.Claim();
            Check(claim["id"] == j["id"] && db.Claim().is_null(), "atomic claim");
            db.Finish(j["id"], {{"complete", false}});
            Check(db.Jobs()[0]["state"] == "inconclusive", "queue failure not safe");
        }
        Reject([&] {
            ProviderResponse("circl", hash, {200, Json{{"SHA-256", std::string(64, 'b')}}.dump(), {}});
        });
        Reject([&] { ProviderResponse("virustotal", hash, {200, "{}", {}}); });
        Check(ProviderResponse("malwarebazaar", hash,
                               {200, Json{{"query_status", "hash_not_found"}}.dump(), {}})["status"] ==
                  "unknown",
              "missing hash unknown");
        Check(ProviderRequest("malwarebazaar", hash, "test-key").body == "query=get_info&hash=" + hash,
              "hash POST only");
        Reject([&] { ProviderRequest("virustotal", hash, "bad\r\nkey"); });
        {
            std::atomic<int> admitted = 0, failed = 0;
            std::vector<std::thread> threads;
            for (int i = 0; i < 8; ++i)
                threads.emplace_back([&] {
                    try
                    {
                        Store db(root / "test.sqlite");
                        if (db.Admit("concurrent", 200000, 3, 3))
                            ++admitted;
                    }
                    catch (...)
                    {
                        ++failed;
                    }
                });
            for (auto &thread : threads)
                thread.join();
            Check(admitted == 3 && failed == 0, "concurrent persistent request quota");
        }
        const auto vt =
            Json{{"data",
                  {{"type", "file"},
                   {"id", hash},
                   {"attributes", {{"last_analysis_stats", {{"malicious", 2}, {"harmless", 50}}}}}}}};
        Check(ProviderResponse("virustotal", hash, {200, vt.dump(), {}})["classification"] == "suspicious",
              "remote detection remains corroboration");
        Reject([&] { ProviderResponse("circl", hash, {401, "", {}}); });
        Reject([&] { ProviderResponse("circl", hash, {200, "not json", {}}); });
        Check(ProviderResponse(
                  "malwarebazaar", hash,
                  {200,
                   Json{{"query_status", "ok"}, {"data", Json::array({{{"sha256_hash", hash}}})}}.dump(),
                   {}})["classification"] == "suspicious",
              "MalwareBazaar matching hash");
        Config c;
        c.state = root / "engine";
        Engine engine(c);
        Write(root / "abc.txt", Bytes(text));
        r = engine.Analyze(root / "abc.txt");
        Check(r["complete"] == true && r["classification"] == "unknown" && r["sha256"] == hash,
              "supervised worker end to end");
        r = engine.Analyze(root / "abc.txt");
        Check(r["cached"] == true, "local cache hit");
        std::atomic<bool> cancel = true;
        r = engine.Analyze(root / "abc.txt", &cancel);
        Check(r["complete"] == false, "cancel cached analysis");
        r = engine.Analyze(root / "missing");
        Check(r["classification"] == "inconclusive", "read failure");
        std::cout << "scanner core PASS\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
