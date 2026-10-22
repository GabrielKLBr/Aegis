#include "DiskLocation.h"
#ifdef _WIN32
#include <Windows.h>
#endif
namespace av::microvm::storage {
namespace fs = std::filesystem;
void SafeDiskParent(const fs::path &p) {
    state::Require(p.is_absolute() && p == p.lexically_normal(),
                   "Disk folder must be an absolute normalized path");
    auto at = p.root_path();
    for (const auto &part : p.relative_path()) {
        at /= part;
        const auto status = fs::symlink_status(at);
        state::Require(fs::is_directory(status) && !fs::is_symlink(status),
                       "Disk folder ancestor type rejected");
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(at.c_str());
        state::Require(attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT),
                       "Disk folder reparse ancestor rejected");
#endif
    }
}
void CreateLocation(const fs::path &path, const std::string &owner) {
    state::Require(!owner.empty() && owner.size() < 64, "Disk location identity rejected");
    SafeDiskParent(path.parent_path());
    for (auto at = path.parent_path(); !at.empty(); at = at.parent_path()) {
        state::Require(!fs::exists(at / ".guardian-location-owner") &&
                           !fs::exists(at / ".guardian-disk-owner"),
                       "Cannot nest an independent disk inside another managed disk");
        if (state::Ordinary(at / ".owner") && fs::file_size(at / ".owner") <= 128) {
            const auto bytes = state::ReadFile(at / ".owner", 128);
            const std::string marker(bytes.begin(), bytes.end());
            state::Require(!marker.starts_with("GuardianVM phase 9\n"),
                           "Cannot place an independent disk inside VM state");
        }
        if (at == at.root_path())
            break;
    }
    state::Require(fs::create_directory(path), "Disk location already exists");
    state::AtomicText(path / ".guardian-location-owner", owner);
}
void ValidateLocation(const fs::path &path, const std::string &owner) {
    SafeDiskParent(path);
    const auto marker = state::ReadFile(path / ".guardian-location-owner", 128);
    state::Require(std::string(marker.begin(), marker.end()) == owner,
                   "Independent disk location ownership rejected");
}
void RemoveGeneration(const fs::path &path, const std::string &owner) {
    ValidateLocation(path.parent_path(), owner);
    SafeDiskParent(path);
    const auto name = path.filename().string();
    state::Require(name.starts_with(".active-") &&
                       name.find_first_not_of(".active-0123456789") == std::string::npos,
                   "Generation name rejected");
    const auto marker = path / ".guardian-disk-owner";
    const bool marked = state::Ordinary(marker);
    if (marked) {
        const auto bytes = state::ReadFile(marker, 128);
        state::Require(std::string(bytes.begin(), bytes.end()) == owner,
                       "Generation belongs to another disk");
    }
    for (const auto &e : fs::recursive_directory_iterator(path)) {
        state::Require(state::Ordinary(e.path()) && fs::hard_link_count(e.path()) == 1,
                       "Generation cleanup refuses unknown directories or links");
        const auto file = e.path().filename().string();
        if (marked)
            state::Require(file == ".guardian-disk-owner" || file == "disk.lock" || file == "payload.gvd2",
                           "Generation cleanup refuses unrelated files");
        else
            state::Require((file == "base.gvd" ||
                            (file.starts_with(".partial-") &&
                             file.find_first_not_of(".partial-0123456789") == std::string::npos)) &&
                               fs::file_size(e.path()) <= 65536,
                           "Unmarked generation contains unrecognized data");
    }
    fs::remove_all(path);
}
void ValidateLocationRemoval(const fs::path &path, const std::string &owner) {
    ValidateLocation(path, owner);
    for (const auto &e : fs::recursive_directory_iterator(path)) {
        state::Require(e.path().filename() != ".guardian-location-owner" || e.path().parent_path() == path,
                       "Refusing to remove a nested independent disk location");
        if (e.path().filename() == ".guardian-disk-owner") {
            const auto bytes = state::ReadFile(e.path(), 128);
            state::Require(std::string(bytes.begin(), bytes.end()) == owner,
                           "Refusing to remove another disk's generation");
        }
        if (fs::is_directory(fs::symlink_status(e.path())))
            SafeDiskParent(e.path());
        else
            state::Require(state::Ordinary(e.path()) && fs::hard_link_count(e.path()) == 1,
                           "Disk removal entry type rejected");
    }
}
void RemoveLocation(const fs::path &path, const std::string &owner) {
    ValidateLocationRemoval(path, owner);
    fs::remove_all(path);
}
} // namespace av::microvm::storage
