#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace aegis::distribution {
namespace fs=std::filesystem;
using Json=nlohmann::json;
inline constexpr const wchar_t* ManifestName=L"aegis.manifest.json";
inline constexpr const wchar_t* SignatureName=L"aegis.manifest.sig";
inline constexpr const wchar_t* PublisherKeyName=L"Aegis.ReleasePublisher.v1";
struct File {
    HANDLE h=INVALID_HANDLE_VALUE;
    File()=default;explicit File(HANDLE value):h(value){}
    File(const File&)=delete;File& operator=(const File&)=delete;
    File(File&& other)noexcept:h(other.h){other.h=INVALID_HANDLE_VALUE;}
    File& operator=(File&& other)noexcept{if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);h=other.h;other.h=INVALID_HANDLE_VALUE;return *this;}
    ~File(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
};
struct VerifiedTree {
    fs::path root;Json manifest;std::string identity;
    std::vector<File> locks;
};
void Require(bool,const char*);
std::string Utf8(const std::wstring&);
std::wstring Wide(const std::string&);
void Ordinary(const fs::path&);
std::string Hex(std::span<const uint8_t>);
std::vector<uint8_t> Unhex(const std::string&);
std::string Hash(std::span<const uint8_t>);
std::string HashFile(const fs::path&);
std::string Read(const fs::path&,uint64_t limit=2u<<20);
Json ReadJson(const fs::path&);
void AtomicWrite(const fs::path&,const std::string&);
void AtomicJson(const fs::path&,const Json&);
void CreateDirectorySecure(const fs::path&,bool forceSystemOwner=false);
void CopyMember(const fs::path& source,const fs::path& target);
void RelativeName(const std::string&);
Json MakeManifest(const fs::path&,const std::string& kind,uint64_t sequence,const std::string& version,const std::string& configuration);
VerifiedTree Verify(const fs::path&,const std::string& kind,bool managed=false,const std::string& publicKey={});
void VerifySignature(const std::string&,const std::string& signature,const std::string& publicKey={});
std::string PublicKey();
fs::path ExecutablePath();
Json Prerequisites();
// Local extracted packages only: no runtime downloader or ZIP extraction surface.
Json DeploymentStatus(const fs::path& root);
Json ApplyUpdate(const fs::path& root,const fs::path& source,const std::function<void(const char*)>& checkpoint={});
Json Rollback(const fs::path& root);
fs::path SelectedPackage(const fs::path& root);
void RecoverUpdate(const fs::path& root);
Json MigratePreferences(const fs::path& file);
} // namespace aegis::distribution
