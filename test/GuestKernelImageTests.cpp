#include "../src/microvm/boot/KernelImage.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
using av::microvm::boot::KernelImage;
namespace {
void Check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void Reject(F fn) { bool rejected = false; try { fn(); } catch (const std::runtime_error&) { rejected = true; } Check(rejected, "Malformed image accepted"); }
template<class T> T Get(const std::vector<uint8_t>& b, size_t at) { T v{}; std::memcpy(&v, b.data() + at, sizeof(v)); return v; }
template<class T> void Put(std::vector<uint8_t>& b, size_t at, T v) { std::memcpy(b.data() + at, &v, sizeof(v)); }
}
int main(int argc, char** argv) {
    try {
        Check(argc == 2, "Kernel path required"); const auto image = KernelImage::Read(argv[1]); const auto& original = image.FileBytes();
        const size_t pe = Get<uint32_t>(original, 0x3C), opt = pe + 24, sections = opt + 240;
        unsigned cases = 0;
        const auto bad = [&](size_t offset, uint64_t value, unsigned bytes) {
            auto b = original; std::memcpy(b.data() + offset, &value, bytes); Reject([&] { KernelImage::Parse(b); }); ++cases;
        };
        bad(0, 0, 2); bad(0x3C, UINT32_MAX, 4); bad(0x3C, 4, 4); bad(pe, 0, 4);
        bad(pe + 4, 0x14C, 2); bad(pe + 6, 0, 2); bad(pe + 6, 17, 2);
        bad(pe + 12, 1, 4); bad(pe + 16, 1, 4); bad(pe + 20, 224, 2);
        bad(pe + 22, 0x2003, 2); bad(pe + 22, 2, 2); bad(pe + 22, 1, 2);
        bad(opt, 0x10B, 2); bad(opt + 24, 0x140000000ull, 8); bad(opt + 32, 512, 4);
        bad(opt + 36, 1, 4); bad(opt + 68, 3, 2); bad(opt + 108, 15, 4);
        bad(opt + 56, 0, 4); bad(opt + 56, 0xFFFFFFFF, 4); bad(opt + 60, 1, 4);
        static_assert(KernelImage::PreferredBase+KernelImage::MaxImageBytes<0x400000);
        bad(opt + 56, KernelImage::MaxImageBytes+4096, 4);
        bad(opt + 60, 0x1000, 4); bad(opt + 16, 0, 4); bad(opt + 16, image.Size() - 1, 4);
        bad(sections + 8, 0, 4); bad(sections + 8, UINT32_MAX, 4);
        bad(sections + 12, 1, 4); bad(sections + 12, 0x100000, 4);
        bad(sections + 16, 1, 4); bad(sections + 20, UINT32_MAX, 4); bad(sections + 36, 0xE0000020, 4);
        bad(sections + 36, 0x20000020, 4);
        for (unsigned dir : {0u, 1u, 2u, 4u, 5u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u}) {
            auto b = original; Put<uint32_t>(b, opt + 112 + dir * 8, image.Sections()[0].rva);
            Put<uint32_t>(b, opt + 116 + dir * 8, 4); Reject([&] { KernelImage::Parse(b); }); ++cases;
        }
        bad(opt + 112 + 3 * 8, 0xFFFFFF00, 4);
        if (image.Sections().size() > 1) {
            bad(sections + 40 + 12, image.Sections()[0].rva, 4);
            bad(sections + 40 + 20, image.Sections()[0].rawOffset, 4);
        }
        for (size_t n : {size_t(0), size_t(63), opt + 20, original.size() - 1}) {
            Reject([&] { KernelImage::Parse(std::span(original).first(n)); }); ++cases;
        }
        auto trailing = original; trailing.push_back(0); Reject([&] { KernelImage::Parse(trailing); }); ++cases;
        std::vector<uint8_t> excessive(KernelImage::MaxFileBytes + 1); Reject([&] { KernelImage::Parse(excessive); }); ++cases;
        std::vector<uint8_t> small(4096, 0xA5); Reject([&] { image.Map(small); });
        Check(std::all_of(small.begin(), small.end(), [](uint8_t v) { return v == 0xA5; }), "Failed map wrote RAM");
        std::vector<uint8_t> ram((16ull << 20), 0xA5), expected(image.Size(), 0);
        std::memcpy(expected.data(), original.data(), image.HeaderBytes());
        for (const auto& s : image.Sections()) if (s.rawBytes) std::memcpy(expected.data() + s.rva, original.data() + s.rawOffset, s.rawBytes);
        image.Map(ram);
        Check(std::equal(expected.begin(), expected.end(), ram.begin() + image.Base()), "Sections/BSS/padding not mapped correctly");
        Check(ram[image.Base() - 1] == 0xA5 && ram[image.Base() + image.Size()] == 0xA5, "Map wrote outside image");
        Reject([&] { KernelImage empty; empty.Map(ram); });
        std::cout << "Image valid; mapping/BSS/bounds verified; " << cases << " malformed image cases rejected\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
