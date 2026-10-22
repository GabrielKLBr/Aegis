// PeLoader.cpp - implementation of the minimal PE32+ loader.
#include "PeLoader.h"

#include <windows.h>

#include <fstream>
#include <filesystem>
#include <iterator>
#include <algorithm>

namespace av {
namespace microvm {

namespace {
uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint64_t rd64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (8 * i);
    return v;
}
void wr64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i));
}
// Read a bounded, NUL-terminated string out of the mapped image.
std::string gstr(const uint8_t* base, uint64_t rva, uint64_t imageSize) {
    std::string s;
    for (uint64_t i = rva; i < imageSize && base[i]; ++i) s.push_back((char)base[i]);
    return s;
}
} // namespace

bool PeLoader::Load(MicroVm& vm, const std::wstring& path, uint64_t loadGpa,
                    const ImportResolver& resolve, PeImage& out) {
    std::ifstream f(std::filesystem::path{path}, std::ios::binary);
    if (!f) { error_ = L"cannot open PE file"; return false; }
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
    if (raw.size() < 0x40) { error_ = L"file too small"; return false; }
    if (rd16(&raw[0]) != 0x5A4D) { error_ = L"not a PE (missing MZ)"; return false; }

    uint32_t e_lfanew = rd32(&raw[0x3C]);
    if ((uint64_t)e_lfanew + 0x108 > raw.size()) { error_ = L"bad e_lfanew"; return false; }
    const uint8_t* nt = &raw[e_lfanew];
    if (rd32(nt) != 0x00004550) { error_ = L"not a PE (missing PE signature)"; return false; }

    const uint8_t* fh = nt + 4;
    uint16_t machine = rd16(fh + 0);
    uint16_t numSec  = rd16(fh + 2);
    uint16_t sizeOpt = rd16(fh + 16);
    const uint8_t* oh = fh + 20;
    if (rd16(oh + 0) != 0x20B || machine != 0x8664) {
        error_ = L"only 64-bit PE32+ images are supported"; return false;
    }
    uint32_t aoep         = rd32(oh + 16);
    uint64_t imageBase    = rd64(oh + 24);
    uint32_t sizeOfImage  = rd32(oh + 56);
    uint32_t sizeOfHeaders= rd32(oh + 60);
    uint32_t importVa     = rd32(oh + 112 + 1 * 8);
    uint32_t relocVa      = rd32(oh + 112 + 5 * 8);
    uint32_t relocSize    = rd32(oh + 112 + 5 * 8 + 4);
    const uint8_t* sec    = oh + sizeOpt;

    if (loadGpa + sizeOfImage > vm.RamSize()) {
        error_ = L"image does not fit in guest RAM"; return false;
    }

    uint8_t* g    = static_cast<uint8_t*>(vm.RamData());
    uint8_t* base = g + loadGpa;
    ZeroMemory(base, sizeOfImage);
    CopyMemory(base, raw.data(), (size_t)std::min<uint64_t>(sizeOfHeaders, raw.size()));

    for (uint16_t i = 0; i < numSec; ++i) {
        const uint8_t* sh = sec + i * 40;
        uint32_t va      = rd32(sh + 12);
        uint32_t rawSize = rd32(sh + 16);
        uint32_t rawPtr  = rd32(sh + 20);
        if (rawSize && (uint64_t)rawPtr + rawSize <= raw.size() &&
            (uint64_t)va + rawSize <= sizeOfImage)
            CopyMemory(base + va, raw.data() + rawPtr, rawSize);
    }

    // Base relocations (only needed if loaded away from the preferred base).
    int64_t delta = (int64_t)loadGpa - (int64_t)imageBase;
    if (delta != 0 && relocVa && relocSize) {
        uint32_t off = 0;
        while (off + 8 <= relocSize) {
            uint8_t* blk = base + relocVa + off;
            uint32_t pageRva = rd32(blk + 0);
            uint32_t blkSize = rd32(blk + 4);
            if (blkSize < 8 || pageRva == 0) break;
            uint32_t n = (blkSize - 8) / 2;
            for (uint32_t k = 0; k < n; ++k) {
                uint16_t e = rd16(blk + 8 + k * 2);
                if ((e >> 12) == 10 /* IMAGE_REL_BASED_DIR64 */) {
                    uint8_t* p = base + pageRva + (e & 0xFFF);
                    wr64(p, rd64(p) + (uint64_t)delta);
                }
            }
            off += blkSize;
        }
        out.relocated = true;
    }

    // Imports: rewrite each IAT slot to a host stub.
    if (importVa) {
        for (uint32_t d = 0;; d += 20) {
            uint8_t* desc = base + importVa + d;
            uint32_t oft     = rd32(desc + 0);
            uint32_t nameRva = rd32(desc + 12);
            uint32_t ft      = rd32(desc + 16);
            if (nameRva == 0 && ft == 0) break;

            std::string dll = gstr(base, nameRva, sizeOfImage);
            out.importedDlls.push_back(dll);
            uint32_t iltRva = oft ? oft : ft;

            for (uint32_t t = 0;; t += 8) {
                uint64_t thunk = rd64(base + iltRva + t);
                if (thunk == 0) break;
                uint8_t* iatSlot = base + ft + t;
                if (thunk & 0x8000000000000000ull) {  // import by ordinal
                    out.missingFns.push_back(dll + "!#ordinal");
                    wr64(iatSlot, 0);
                    continue;
                }
                std::string fn = gstr(base, (uint32_t)thunk + 2, sizeOfImage);
                uint64_t stub = resolve ? resolve(dll, fn) : 0;
                if (stub) { wr64(iatSlot, stub); out.resolvedFns.push_back(fn); }
                else      { wr64(iatSlot, 0);    out.missingFns.push_back(fn); }
            }
        }
    }

    out.imageBase   = loadGpa;
    out.entryPoint  = loadGpa + aoep;
    out.sizeOfImage = sizeOfImage;
    return true;
}

} // namespace microvm
} // namespace av
