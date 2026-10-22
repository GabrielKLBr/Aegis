#include "Files.h"
#include "StorageBudget.h"
#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include <system_error>
#ifdef _WIN32
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace av::microvm::state {
namespace {
std::atomic<int64_t> budget{-1};
void Step() {
    auto n = budget.load();
    if (n == 0)
        throw std::runtime_error("Injected durable transaction interruption");
    if (n > 0)
        --budget;
}
} // namespace
void FailAfter(int64_t n) { budget = n; }
std::string Token() {
    static std::atomic<uint64_t> n{0};
    std::random_device r;
    return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
           std::to_string(r()) + "-" + std::to_string(++n);
}
bool Ordinary(const std::filesystem::path &p) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(p, ec);
    if (ec || !std::filesystem::is_regular_file(status))
        return false;
#ifdef _WIN32
    const auto attrs = GetFileAttributesW(p.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return true;
#endif
}
std::vector<uint8_t> ReadFile(const std::filesystem::path &p, uint64_t limit) {
    // A mounted filesystem can transiently invalidate a cached lookup while
    // replacing a directory entry. Only retry absence/staleness; opened handles
    // must still pass the ordinary-file checks below.
    bool ordinary = false;
    for (unsigned attempt = 0; attempt < 21; ++attempt) {
        ordinary = Ordinary(p);
        if (ordinary)
            break;
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(p, ec);
        if (!ec && std::filesystem::is_regular_file(status))
            continue;
        if ((!ec && status.type() != std::filesystem::file_type::not_found) ||
            (ec && ec != std::errc::no_such_file_or_directory && ec != std::errc::io_error))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto utf8 = p.u8string();
    const std::string name(utf8.begin(), utf8.end());
    if (!ordinary)
        throw std::runtime_error("State input must be an ordinary file: " + name);
#ifdef _WIN32
    HANDLE h = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 21; ++attempt) {
        h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (h != INVALID_HANDLE_VALUE)
            break;
        const auto error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (h == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot open state input: " + name);
    try {
        BY_HANDLE_FILE_INFORMATION info{};
        LARGE_INTEGER size{};
        Require(GetFileInformationByHandle(h, &info) &&
                    !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
                    GetFileSizeEx(h, &size) && size.QuadPart >= 0 && uint64_t(size.QuadPart) <= limit,
                "State input size/type rejected");
        std::vector<uint8_t> bytes(static_cast<size_t>(size.QuadPart));
        DWORD actual = 0;
        Require(bytes.empty() ||
                    (::ReadFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &actual, nullptr) &&
                     actual == bytes.size()),
                "Cannot read state input");
        CloseHandle(h);
        return bytes;
    } catch (...) {
        CloseHandle(h);
        throw;
    }
#else
    int fd = -1;
    for (unsigned attempt = 0; attempt < 21; ++attempt) {
        fd = ::open(p.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        if (fd >= 0 || (errno != ENOENT && errno != ESTALE))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (fd < 0)
        throw std::runtime_error("Cannot open state input: " + name);
    try {
        struct stat s{};
        Require(fstat(fd, &s) == 0 && S_ISREG(s.st_mode) && s.st_size >= 0 && uint64_t(s.st_size) <= limit,
                "State input size/type rejected");
        std::vector<uint8_t> bytes(static_cast<size_t>(s.st_size));
        size_t at = 0;
        while (at < bytes.size()) {
            auto n = ::read(fd, bytes.data() + at, bytes.size() - at);
            if (n < 0 && errno == EINTR)
                continue;
            Require(n > 0, "Cannot read state input");
            at += size_t(n);
        }
        close(fd);
        return bytes;
    } catch (...) {
        close(fd);
        throw;
    }
#endif
}
void AtomicWrite(const std::filesystem::path &p, std::span<const uint8_t> bytes) {
    Require(bytes.size() <= MaxStateBytes, "State output exceeds limit");
    StorageBudget::Check(p,bytes.size());
    if(bytes.size()>=(1ull<<20)){const auto free=std::filesystem::space(p.parent_path()).available;Require(free>(256ull<<20)&&bytes.size()<=free-(256ull<<20),"Insufficient host disk space including atomic replacement reserve");}
    Step();
    const auto temp = p.parent_path() / (".partial-" + Token());
#ifdef _WIN32
    auto h = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(h != INVALID_HANDLE_VALUE, "Cannot create transaction file");
    try {
        Step();
        DWORD actual = 0;
        Require(bytes.empty() ||
                    (::WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &actual, nullptr) &&
                     actual == bytes.size()),
                "Cannot write transaction file");
        Step();
        Require(FlushFileBuffers(h) != 0, "Cannot flush transaction file");
        CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
        Step();
        // A reader may still hold the replaced file's delete-pending object.
        // Retry only bounded sharing/access conflicts; never remove the old file
        // first, so a failure leaves the previous durable generation intact.
        for(unsigned attempt=0;;++attempt){
            if(MoveFileExW(temp.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))break;
            const auto error=GetLastError();
            if(attempt>=100 || (error!=ERROR_ACCESS_DENIED && error!=ERROR_SHARING_VIOLATION && error!=ERROR_LOCK_VIOLATION))
                throw std::system_error(static_cast<int>(error),std::system_category(),"Cannot publish transaction file");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    } catch (...) {
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
        DeleteFileW(temp.c_str());
        throw;
    }
#else
    int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    Require(fd >= 0, "Cannot create transaction file");
    try {
        Step();
        size_t at = 0;
        while (at < bytes.size()) {
            auto n = ::write(fd, bytes.data() + at, bytes.size() - at);
            if (n < 0 && errno == EINTR)
                continue;
            Require(n > 0, "Cannot write transaction file");
            at += size_t(n);
        }
        Step();
        Require(::fsync(fd) == 0, "Cannot flush transaction file");
        close(fd);
        fd = -1;
        Step();
        Require(::rename(temp.c_str(), p.c_str()) == 0, "Cannot publish transaction file");
        const int dir = ::open(p.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        Require(dir >= 0, "Cannot open transaction directory");
        const bool flushed = ::fsync(dir) == 0;
        close(dir);
        Require(flushed, "Cannot flush transaction directory");
    } catch (...) {
        if (fd >= 0)
            close(fd);
        ::unlink(temp.c_str());
        throw;
    }
#endif
}
void AtomicText(const std::filesystem::path &p, const std::string &text) {
    AtomicWrite(p, {reinterpret_cast<const uint8_t *>(text.data()), text.size()});
}
Lease::Lease(const std::filesystem::path &p, bool tryOnly) {
#ifdef _WIN32
    handle_ = CreateFileW(p.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        if (tryOnly && GetLastError() == ERROR_SHARING_VIOLATION)
            return;
        throw std::runtime_error("Cannot acquire VM lease");
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle_, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
        throw std::runtime_error("Invalid lease file");
    }
#else
    handle_ = ::open(p.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    Require(handle_ >= 0, "Cannot open VM lease");
    struct stat s{};
    if (fstat(handle_, &s) || !S_ISREG(s.st_mode)) {
        close(handle_);
        handle_ = -1;
        throw std::runtime_error("Invalid lease file");
    }
    if (::flock(handle_, LOCK_EX | LOCK_NB)) {
        const int reason = errno;
        close(handle_);
        handle_ = -1;
        if (tryOnly && (reason == EWOULDBLOCK || reason == EAGAIN))
            return;
        throw std::runtime_error("VM lease is already held");
    }
#endif
    held_ = true;
}
Lease::~Lease() {
#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE)
        CloseHandle(handle_);
#else
    if (handle_ >= 0)
        close(handle_);
#endif
}
} // namespace av::microvm::state

namespace av::microvm::state {
void PublishDirectory(const std::filesystem::path &source, const std::filesystem::path &target) {
    Require(!std::filesystem::exists(target), "Directory destination already exists");
#ifdef _WIN32
    Require(MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH) != 0,
            "Cannot publish VM directory");
#else
    Require(::rename(source.c_str(), target.c_str()) == 0, "Cannot publish VM directory");
    const int dir = ::open(target.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    Require(dir >= 0, "Cannot open catalog directory");
    const bool ok = ::fsync(dir) == 0;
    close(dir);
    Require(ok, "Cannot flush catalog directory");
#endif
}
} // namespace av::microvm::state
