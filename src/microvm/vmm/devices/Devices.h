// Devices.h - paravirtual device models for the MicroVM engine.
#pragma once

#include "../MicroVmMonitor.h"

#include <string>
#include <vector>

namespace av {
namespace microvm {

// PvConsole: a debug console. The guest writes bytes to one port; we collect
// them into a string. (Seed of the guest<->host PvChannel.)
class PvConsole : public IoDevice {
public:
    explicit PvConsole(uint16_t port = 0xE9) : port_(port) {}
    void OnOut(uint16_t port, uint8_t value) override {
        if (port == port_) text_.push_back(static_cast<char>(value));
    }
    const std::string& Text() const { return text_; }
private:
    uint16_t port_;
    std::string text_;
};

// PvDisk: a paravirtual block device backed by a *differential* image file
// (copy-on-write stand-in for diff.vhdx). The guest selects a sector via the
// LBA port, then streams 512 bytes through the data port; each full sector is
// flushed to the diff file -- NEVER to a real \\.\PhysicalDrive. This is what
// makes a MEMZ-style MBR overwrite hit a throwaway file instead of the host.
class PvDisk : public IoDevice {
public:
    struct SectorWrite { uint32_t lba; uint32_t bytes; };

    PvDisk(std::wstring diffImagePath, uint16_t lbaPort = 0x71, uint16_t dataPort = 0x70);
    ~PvDisk();

    void OnOut(uint16_t port, uint8_t value) override;

    const std::vector<SectorWrite>& Writes() const { return writes_; }
    uint64_t BytesWritten() const { return bytesWritten_; }
    const std::wstring& ImagePath() const { return imagePath_; }
    bool HostDiskTouched() const { return false; }  // true by construction

private:
    void FlushSector();

    std::wstring imagePath_;
    uint16_t lbaPort_;
    uint16_t dataPort_;
    uint32_t curLba_ = 0;
    std::vector<uint8_t> buf_;          // in-flight sector bytes
    std::vector<SectorWrite> writes_;
    uint64_t bytesWritten_ = 0;
    HANDLE   file_ = INVALID_HANDLE_VALUE;
};

} // namespace microvm
} // namespace av
