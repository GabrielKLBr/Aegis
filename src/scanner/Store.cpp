#include "Store.h"
#include <chrono>
#include <sqlite3.h>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace av::scan
{
namespace
{
struct Statement
{
    sqlite3_stmt *p = nullptr;
    Statement(sqlite3 *db, const char *sql)
    {
        Require(sqlite3_prepare_v2(db, sql, -1, &p, nullptr) == SQLITE_OK,
                "Scanner database statement failed");
    }
    ~Statement()
    {
        sqlite3_finalize(p);
    }
    void Text(int i, const std::string &s)
    {
        Require(s.size() <= 2ull << 20 &&
                    sqlite3_bind_text(p, i, s.data(), int(s.size()), SQLITE_TRANSIENT) == SQLITE_OK,
                "Database text rejected");
    }
    void Int(int i, int64_t n)
    {
        Require(sqlite3_bind_int64(p, i, n) == SQLITE_OK, "Database integer rejected");
    }
    int Step()
    {
        const auto rc = sqlite3_step(p);
        Require(rc == SQLITE_ROW || rc == SQLITE_DONE, "Scanner database operation failed/full");
        return rc;
    }
    int64_t Int(int i) const
    {
        return sqlite3_column_int64(p, i);
    }
    std::string Text(int i) const
    {
        const auto *s = sqlite3_column_text(p, i);
        return s ? std::string(reinterpret_cast<const char *>(s), size_t(sqlite3_column_bytes(p, i)))
                 : std::string();
    }
};
int64_t Now()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
} // namespace
void Store::Exec(const char *sql)
{
    Require(sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK,
            "Scanner database transaction failed/full");
}
Store::Store(const fs::path &path)
{
    OrdinaryDirectory(path.parent_path());
    for (const auto &suffix : {std::string(), std::string("-wal"), std::string("-shm")})
    {
        const auto p = Utf8Path(PathText(path) + suffix);
        const auto st = fs::symlink_status(p);
        if (fs::exists(st))
            Require(fs::is_regular_file(st) && !fs::is_symlink(st) && fs::file_size(p) <= (64ull << 20),
                    "Database type/size rejected");
#ifdef _WIN32
        if (fs::exists(st))
        {
            const auto attrs = GetFileAttributesW(p.c_str());
            Require(attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT),
                    "Database reparse point rejected");
        }
#endif
    }
    const auto name = PathText(path);
    const auto rc = sqlite3_open_v2(
        name.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK)
    {
        if (db_)
            sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("Cannot open scanner SQLite database");
    }
    try
    {
        sqlite3_busy_timeout(db_, 5000);
        sqlite3_limit(db_, SQLITE_LIMIT_LENGTH, 2 << 20);
        Exec(
            "PRAGMA trusted_schema=OFF; PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA "
            "max_page_count=16384; PRAGMA wal_autocheckpoint=64;"
            "CREATE TABLE IF NOT EXISTS cache(category TEXT NOT NULL,key TEXT NOT NULL,value TEXT NOT "
            "NULL,expires INTEGER NOT NULL,PRIMARY KEY(category,key));"
            "CREATE TABLE IF NOT EXISTS rates(provider TEXT PRIMARY KEY,minute INTEGER NOT NULL,minute_count "
            "INTEGER NOT NULL,day INTEGER NOT NULL,day_count INTEGER NOT NULL,blocked INTEGER NOT NULL);"
            "CREATE TABLE IF NOT EXISTS jobs(id TEXT PRIMARY KEY,hash TEXT NOT NULL UNIQUE,name TEXT NOT "
            "NULL,payload TEXT NOT NULL,created INTEGER NOT NULL,state TEXT NOT NULL,result TEXT NOT "
            "NULL,lease INTEGER NOT NULL DEFAULT 0);");
    }
    catch (...)
    {
        sqlite3_close(db_);
        db_ = nullptr;
        throw;
    }
}
Store::~Store()
{
    if (db_)
        sqlite3_close(db_);
}
Json Store::Cached(const std::string &category, const std::string &key, int64_t now)
{
    Statement s(db_, "SELECT value FROM cache WHERE category=? AND key=? AND expires>?");
    s.Text(1, category);
    s.Text(2, key);
    s.Int(3, now);
    if (s.Step() != SQLITE_ROW)
        return nullptr;
    return Json::parse(s.Text(0));
}
void Store::Cache(const std::string &category, const std::string &key, const Json &value, int64_t now,
                  uint32_t ttl)
{
    if (!ttl)
        return;
    Exec("BEGIN IMMEDIATE");
    try
    {
        {
            Statement s(db_, "DELETE FROM cache WHERE expires<=?");
            s.Int(1, now);
            s.Step();
        }
        Exec("DELETE FROM cache WHERE rowid IN (SELECT rowid FROM cache ORDER BY expires LIMIT MAX(0,(SELECT "
             "COUNT(*) FROM cache)-4095))");
        Statement s(db_,
                    "INSERT INTO cache(category,key,value,expires) VALUES(?,?,?,?) ON CONFLICT(category,key) "
                    "DO UPDATE SET value=excluded.value,expires=excluded.expires");
        s.Text(1, category);
        s.Text(2, key);
        s.Text(3, value.dump());
        s.Int(4, now + ttl);
        s.Step();
        Exec("COMMIT");
    }
    catch (...)
    {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
bool Store::Admit(const std::string &provider, int64_t now, uint32_t perMinute, uint32_t perDay)
{
    Exec("BEGIN IMMEDIATE");
    try
    {
        int64_t minute = now / 60, day = now / 86400, mc = 0, dc = 0, blocked = 0;
        {
            Statement s(db_, "SELECT minute,minute_count,day,day_count,blocked FROM rates WHERE provider=?");
            s.Text(1, provider);
            if (s.Step() == SQLITE_ROW)
            {
                if (s.Int(0) == minute)
                    mc = s.Int(1);
                if (s.Int(2) == day)
                    dc = s.Int(3);
                blocked = s.Int(4);
            }
        }
        if (blocked > now || mc >= perMinute || dc >= perDay)
        {
            Exec("COMMIT");
            return false;
        }
        Statement s(db_, "INSERT INTO rates VALUES(?,?,?,?,?,?) ON CONFLICT(provider) DO UPDATE SET "
                         "minute=excluded.minute,minute_count=excluded.minute_count,day=excluded.day,day_"
                         "count=excluded.day_count");
        s.Text(1, provider);
        s.Int(2, minute);
        s.Int(3, mc + 1);
        s.Int(4, day);
        s.Int(5, dc + 1);
        s.Int(6, blocked);
        s.Step();
        Exec("COMMIT");
        return true;
    }
    catch (...)
    {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
void Store::Backoff(const std::string &provider, int64_t until)
{
    Statement s(db_, "UPDATE rates SET blocked=MAX(blocked,?) WHERE provider=?");
    s.Int(1, until);
    s.Text(2, provider);
    s.Step();
}
Json Store::Enqueue(const std::string &hash, const std::string &name, const fs::path &payload, int64_t now)
{
    Exec("BEGIN IMMEDIATE");
    try
    {
        {
            Statement s(db_, "SELECT id,state FROM jobs WHERE hash=?");
            s.Text(1, hash);
            if (s.Step() == SQLITE_ROW)
            {
                Json j = {{"id", s.Text(0)}, {"state", s.Text(1)}, {"duplicate", true}};
                Exec("COMMIT");
                return j;
            }
        }
        {
            Statement s(db_, "SELECT COUNT(*) FROM jobs");
            Require(s.Step() == SQLITE_ROW && s.Int(0) < 128, "sandbox queue count limit exceeded");
        }
        const auto id = Token();
        Statement s(
            db_,
            "INSERT INTO jobs(id,hash,name,payload,created,state,result) VALUES(?,?,?,?,?,'queued','{}')");
        s.Text(1, id);
        s.Text(2, hash);
        s.Text(3, name);
        s.Text(4, PathText(payload));
        s.Int(5, now);
        s.Step();
        Exec("COMMIT");
        return {{"id", id}, {"state", "queued"}, {"duplicate", false}};
    }
    catch (...)
    {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
Json Store::Jobs()
{
    Statement s(db_, "SELECT id,hash,name,created,state,result FROM jobs ORDER BY created,id LIMIT 128");
    Json out = Json::array();
    while (s.Step() == SQLITE_ROW)
        out.push_back({{"id", s.Text(0)},
                       {"sha256", s.Text(1)},
                       {"name", s.Text(2)},
                       {"created", s.Int(3)},
                       {"state", s.Text(4)},
                       {"result", Json::parse(s.Text(5))}});
    return out;
}
Json Store::Claim()
{
    Exec("BEGIN IMMEDIATE");
    try
    {
        {
            Statement s(db_, "UPDATE jobs SET state='inconclusive',result='{\"issue\":\"sandbox worker "
                             "interrupted/lease expired\"}' WHERE state='running' AND lease<?");
            s.Int(1, Now());
            s.Step();
        }
        Json job = nullptr;
        {
            Statement s(
                db_,
                "SELECT id,hash,name,payload FROM jobs WHERE state='queued' ORDER BY created,id LIMIT 1");
            if (s.Step() == SQLITE_ROW)
                job = {{"id", s.Text(0)}, {"sha256", s.Text(1)}, {"name", s.Text(2)}, {"payload", s.Text(3)}};
        }
        if (!job.is_null())
        {
            Statement s(db_, "UPDATE jobs SET state='running',lease=? WHERE id=? AND state='queued'");
            s.Int(1, Now() + 120);
            s.Text(2, job.at("id"));
            s.Step();
        }
        Exec("COMMIT");
        return job;
    }
    catch (...)
    {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
void Store::Finish(const std::string &id, const Json &result)
{
    Statement s(db_, "UPDATE jobs SET state=?,result=?,lease=0 WHERE id=? AND state='running'");
    s.Text(1, result.value("complete", false) ? "completed" : "inconclusive");
    s.Text(2, result.dump());
    s.Text(3, id);
    s.Step();
}
} // namespace av::scan
