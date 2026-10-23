// PeLoader.cpp - implementation of the minimal PE32+ loader.
#include "PeLoader.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace av
{
namespace microvm
{

namespace
{
uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}
uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint64_t rd64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= (uint64_t)p[i] << (8 * i);
    return v;
}
void wr64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        p[i] = (uint8_t)(v >> (8 * i));
}
// Read a bounded, NUL-terminated string out of the mapped image.
bool range(uint64_t offset, uint64_t bytes, uint64_t total)
{
    return offset <= total && bytes <= total - offset;
}
bool gstr(const uint8_t *base, uint64_t rva, uint64_t imageSize, std::string &s)
{
    s.clear();
    for (uint64_t i = rva; i < imageSize && s.size() <= 512; ++i)
    {
        if (!base[i])
            return !s.empty();
        s.push_back((char)base[i]);
    }
    return false;
}
} // namespace

bool PeLoader::Load(MicroVm &vm, const std::wstring &path, uint64_t loadGpa, const ImportResolver &resolve,
                    PeImage &out)
{
    out = {};
    error_.clear();
    std::ifstream f(std::filesystem::path{path}, std::ios::binary | std::ios::ate);
    if (!f)
    {
        error_ = L"cannot open PE file";
        return false;
    }
    const auto length = f.tellg();
    if (length < 0 || uint64_t(length) > (128ull << 20))
    {
        error_ = L"PE file size limit";
        return false;
    }
    if (length < 0x40)
    {
        error_ = L"file too small";
        return false;
    }
    std::vector<uint8_t> raw(static_cast<size_t>(length));
    f.seekg(0);
    f.read(reinterpret_cast<char *>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (!f || f.peek() != std::char_traits<char>::eof())
    {
        error_ = L"PE read failed or input grew";
        return false;
    }
    if (raw.size() < 0x40)
    {
        error_ = L"file too small";
        return false;
    }
    if (rd16(&raw[0]) != 0x5A4D)
    {
        error_ = L"not a PE (missing MZ)";
        return false;
    }

    uint32_t e_lfanew = rd32(&raw[0x3C]);
    if ((uint64_t)e_lfanew + 0x108 > raw.size())
    {
        error_ = L"bad e_lfanew";
        return false;
    }
    const uint8_t *nt = &raw[e_lfanew];
    if (rd32(nt) != 0x00004550)
    {
        error_ = L"not a PE (missing PE signature)";
        return false;
    }

    const uint8_t *fh = nt + 4;
    uint16_t machine = rd16(fh + 0);
    uint16_t numSec = rd16(fh + 2);
    uint16_t sizeOpt = rd16(fh + 16);
    const uint8_t *oh = fh + 20;
    const auto optionalOffset = uint64_t(e_lfanew) + 24;
    if (!numSec || numSec > 96 || sizeOpt < 0xF0 || !range(optionalOffset, sizeOpt, raw.size()) ||
        !range(optionalOffset + sizeOpt, uint64_t(numSec) * 40, raw.size()))
    {
        error_ = L"PE optional/section table range";
        return false;
    }
    if (rd16(oh + 0) != 0x20B || machine != 0x8664)
    {
        error_ = L"only 64-bit PE32+ images are supported";
        return false;
    }
    uint32_t aoep = rd32(oh + 16);
    uint64_t imageBase = rd64(oh + 24);
    uint32_t sizeOfImage = rd32(oh + 56);
    uint32_t sizeOfHeaders = rd32(oh + 60);
    const auto directories = rd32(oh + 108);
    if (directories > 16)
    {
        error_ = L"PE directory count limit";
        return false;
    }
    for (const auto index : {9u, 13u, 14u})
    {
        if (directories > index && (rd32(oh + 112 + index * 8) || rd32(oh + 116 + index * 8)))
        {
            error_ = L"PE TLS/delay-import/managed initialization unsupported";
            return false;
        }
    }
    uint32_t importVa = directories > 1 ? rd32(oh + 112 + 1 * 8) : 0;
    uint32_t importSize = directories > 1 ? rd32(oh + 112 + 1 * 8 + 4) : 0;
    uint32_t relocVa = directories > 5 ? rd32(oh + 112 + 5 * 8) : 0;
    uint32_t relocSize = directories > 5 ? rd32(oh + 112 + 5 * 8 + 4) : 0;
    const uint8_t *sec = oh + sizeOpt;

    if (!sizeOfImage || !vm.RamData() || !range(loadGpa, sizeOfImage, vm.RamSize()) || aoep >= sizeOfImage)
    {
        error_ = L"image does not fit in guest RAM";
        return false;
    }
    if (!range(0, sizeOfHeaders, sizeOfImage) || !range(0, sizeOfHeaders, raw.size()) ||
        sizeOfHeaders < optionalOffset + sizeOpt + uint64_t(numSec) * 40)
    {
        error_ = L"PE headers outside image/file";
        return false;
    }
    if ((importVa || importSize) &&
        (!importVa || importSize < 20 || !range(importVa, importSize, sizeOfImage)))
    {
        error_ = L"PE import directory range";
        return false;
    }
    if ((relocVa || relocSize) && (!relocVa || relocSize < 8 || !range(relocVa, relocSize, sizeOfImage)))
    {
        error_ = L"PE relocation directory range";
        return false;
    }

    uint8_t *g = static_cast<uint8_t *>(vm.RamData());
    uint8_t *base = g + loadGpa;
    ZeroMemory(base, sizeOfImage);
    CopyMemory(base, raw.data(), sizeOfHeaders);

    for (uint16_t i = 0; i < numSec; ++i)
    {
        const uint8_t *sh = sec + i * 40;
        uint32_t va = rd32(sh + 12);
        uint32_t rawSize = rd32(sh + 16);
        uint32_t rawPtr = rd32(sh + 20);
        const auto virtualSize = rd32(sh + 8);
        if (!range(va, virtualSize, sizeOfImage) || !range(va, rawSize, sizeOfImage) ||
            (rawSize && (!range(rawPtr, rawSize, raw.size()) || va < sizeOfHeaders)))
        {
            error_ = L"PE section range";
            return false;
        }
        if (rawSize)
            CopyMemory(base + va, raw.data() + rawPtr, rawSize);
    }

    // Base relocations (only needed if loaded away from the preferred base).
    const uint64_t delta = loadGpa - imageBase; // PE relocation arithmetic is modulo 2^64.
    if (delta != 0 && relocVa && relocSize)
    {
        uint64_t off = 0, entries = 0;
        while (off < relocSize)
        {
            if (!range(off, 8, relocSize))
            {
                error_ = L"PE relocation block header range";
                return false;
            }
            uint8_t *blk = base + relocVa + off;
            uint32_t pageRva = rd32(blk + 0);
            uint32_t blkSize = rd32(blk + 4);
            if (!blkSize && !pageRva &&
                std::all_of(blk, base + relocVa + relocSize, [](uint8_t b) { return b == 0; }))
                break;
            if (blkSize < 8 || (blkSize & 1) || !range(off, blkSize, relocSize))
            {
                error_ = L"PE relocation block range";
                return false;
            }
            uint32_t n = (blkSize - 8) / 2;
            entries += n;
            if (entries > (1ull << 20))
            {
                error_ = L"PE relocation entry limit";
                return false;
            }
            for (uint32_t k = 0; k < n; ++k)
            {
                uint16_t e = rd16(blk + 8 + k * 2);
                if ((e >> 12) == 10 /* IMAGE_REL_BASED_DIR64 */)
                {
                    const auto target = uint64_t(pageRva) + (e & 0xFFF);
                    if (!range(target, 8, sizeOfImage))
                    {
                        error_ = L"PE relocation target range";
                        return false;
                    }
                    uint8_t *p = base + target;
                    wr64(p, rd64(p) + (uint64_t)delta);
                }
                else if ((e >> 12) != 0)
                {
                    error_ = L"PE relocation type unsupported";
                    return false;
                }
            }
            off += blkSize;
        }
        out.relocated = true;
    }

    // Imports: rewrite each IAT slot to a host stub.
    if (importVa)
    {
        uint32_t functions = 0;
        for (uint64_t d = 0;; d += 20)
        {
            if (d / 20 >= 256 || !range(d, 20, importSize))
            {
                error_ = L"PE import descriptor range/limit";
                return false;
            }
            uint8_t *desc = base + importVa + d;
            uint32_t oft = rd32(desc + 0);
            uint32_t nameRva = rd32(desc + 12);
            uint32_t ft = rd32(desc + 16);
            if (std::all_of(desc, desc + 20, [](uint8_t b) { return b == 0; }))
                break;
            if (!nameRva || !ft)
            {
                error_ = L"PE import descriptor invalid";
                return false;
            }

            std::string dll;
            if (!gstr(base, nameRva, sizeOfImage, dll))
            {
                error_ = L"PE import DLL name range/limit";
                return false;
            }
            out.importedDlls.push_back(dll);
            uint32_t iltRva = oft ? oft : ft;

            for (uint64_t t = 0;; t += 8)
            {
                if (!range(uint64_t(iltRva) + t, 8, sizeOfImage) || !range(uint64_t(ft) + t, 8, sizeOfImage))
                {
                    error_ = L"PE import thunk/IAT range";
                    return false;
                }
                uint64_t thunk = rd64(base + iltRva + t);
                if (thunk == 0)
                    break;
                if (++functions > 4096)
                {
                    error_ = L"PE import function limit";
                    return false;
                }
                uint8_t *iatSlot = base + ft + t;
                if (thunk & 0x8000000000000000ull)
                { // import by ordinal
                    out.missingFns.push_back(dll + "!#ordinal");
                    wr64(iatSlot, 0);
                    continue;
                }
                std::string fn;
                if (thunk > UINT32_MAX || !range(thunk, 2, sizeOfImage) ||
                    !gstr(base, thunk + 2, sizeOfImage, fn))
                {
                    error_ = L"PE import function name range/limit";
                    return false;
                }
                uint64_t stub = resolve ? resolve(dll, fn) : 0;
                if (stub)
                {
                    wr64(iatSlot, stub);
                    out.resolvedFns.push_back(fn);
                }
                else
                {
                    wr64(iatSlot, 0);
                    out.missingFns.push_back(fn);
                }
            }
        }
    }

    out.imageBase = loadGpa;
    out.entryPoint = loadGpa + aoep;
    out.sizeOfImage = sizeOfImage;
    return true;
}

} // namespace microvm
} // namespace av
