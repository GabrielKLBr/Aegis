#pragma once
#include <sqlite3.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <stdexcept>
namespace aegis::storage {
inline void Check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
inline int Integer(sqlite3* db,const char* query){sqlite3_stmt* statement=nullptr;Check(sqlite3_prepare_v2(db,query,-1,&statement,nullptr)==SQLITE_OK,"Read storage version");const auto step=sqlite3_step(statement);const auto value=step==SQLITE_ROW?sqlite3_column_int(statement,0):-1;sqlite3_finalize(statement);Check(step==SQLITE_ROW,"Storage metadata unavailable");return value;}
// Version zero is the existing on-disk layout. This migration adds an explicit
// version fence, preserving all records and a consistent SQLite backup first.
inline void Migrate(sqlite3* db,const std::filesystem::path& path,int supported){
    const auto version=Integer(db,"PRAGMA user_version");Check(version==0||version==supported,"Newer or unsupported database schema; downgrade refused");if(version==supported)return;
    if(Integer(db,"SELECT COUNT(*) FROM sqlite_master WHERE type='table'")>0){
        const auto backup=std::filesystem::path(path.wstring()+L".schema0.backup.sqlite");
        if(!std::filesystem::exists(backup)){
            const auto temporary=std::filesystem::path(backup.wstring()+L".partial-"+std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
            sqlite3* destination=nullptr;const auto name=temporary.u8string();Check(sqlite3_open_v2(reinterpret_cast<const char*>(name.c_str()),&destination,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_EXCLUSIVE,nullptr)==SQLITE_OK,"Create migration backup");
            auto* copy=sqlite3_backup_init(destination,"main",db,"main");if(!copy){sqlite3_close(destination);throw std::runtime_error("Initialize migration backup");}
            int result=SQLITE_OK;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while((result==SQLITE_OK||result==SQLITE_BUSY||result==SQLITE_LOCKED)&&std::chrono::steady_clock::now()<deadline){result=sqlite3_backup_step(copy,64);if(result==SQLITE_BUSY||result==SQLITE_LOCKED)std::this_thread::sleep_for(std::chrono::milliseconds(10));}
            const auto finished=sqlite3_backup_finish(copy);const auto closed=sqlite3_close(destination);Check(result==SQLITE_DONE&&finished==SQLITE_OK&&closed==SQLITE_OK,"Migration backup interrupted; original database retained");std::filesystem::rename(temporary,backup);
        }
    }
    Check(sqlite3_exec(db,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr)==SQLITE_OK,"Begin schema migration");
    const auto query="PRAGMA user_version="+std::to_string(supported);
    if(sqlite3_exec(db,query.c_str(),nullptr,nullptr,nullptr)!=SQLITE_OK||sqlite3_exec(db,"COMMIT",nullptr,nullptr,nullptr)!=SQLITE_OK){sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);throw std::runtime_error("Schema migration failed; transaction rolled back");}
}
}
