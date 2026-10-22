// PvDisk.cpp - differential-image block device for the MicroVM engine.
#include "Devices.h"

namespace av {
namespace microvm {

static constexpr uint32_t kSectorSize = 512;

PvDisk::PvDisk(std::wstring diffImagePath, uint16_t lbaPort, uint16_t dataPort)
    : imagePath_(std::move(diffImagePath)), lbaPort_(lbaPort), dataPort_(dataPort) {
    buf_.reserve(kSectorSize);
    // Create/truncate the differential image. This is the ONLY disk handle the
    // sandbox ever opens; the host's physical drives are never touched.
    file_ = CreateFileW(imagePath_.c_str(), GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
}

PvDisk::~PvDisk() {
    if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
}

void PvDisk::OnOut(uint16_t port, uint8_t value) {
    if (port == lbaPort_) {
        // Select the target sector; start a fresh sector buffer.
        curLba_ = value;
        buf_.clear();
        return;
    }
    if (port == dataPort_) {
        buf_.push_back(value);
        if (buf_.size() == kSectorSize) FlushSector();
        return;
    }
}

void PvDisk::FlushSector() {
    if (file_ != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER off;
        off.QuadPart = static_cast<LONGLONG>(curLba_) * kSectorSize;
        SetFilePointerEx(file_, off, nullptr, FILE_BEGIN);
        DWORD wrote = 0;
        WriteFile(file_, buf_.data(), static_cast<DWORD>(buf_.size()), &wrote, nullptr);
    }
    writes_.push_back({curLba_, static_cast<uint32_t>(buf_.size())});
    bytesWritten_ += buf_.size();
    buf_.clear();
}

} // namespace microvm
} // namespace av
