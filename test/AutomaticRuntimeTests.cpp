#include "microvm/boot/KernelBoot.h"
#include "microvm/runtime/Automatic.h"
#include <iostream>
int wmain(int argc, wchar_t **argv) {
    try {
        using namespace av;
        using namespace microvm::runtime;
        scan::Require(argc == 3, "Use automatic-tests PE VOLUME");
        auto input = scan::Read(argv[1], 8 << 20), volume = scan::Read(argv[2], 64 << 20);
        scan::Require(guardian::pevolume::Valid(volume.data(), uint32_t(volume.size())),
                      "Fixture volume invalid");
        auto pe = automatic::Pe(input);
        const auto imports = pe.Imports();
        scan::Require(!imports.empty() && automatic::Missing(input, volume).empty(),
                      "Fixture imports unavailable");
        uint32_t random = 0x1413A;
        unsigned rejected = 0;
        for (unsigned i = 0; i < 500; ++i) {
            auto bytes = input;
            random = random * 1664525 + 1013904223;
            auto at = random % uint32_t((std::min)(bytes.size(), size_t(1024)));
            bytes[at] ^= uint8_t((i % 255) + 1);
            try {
                auto changed = automatic::Pe(bytes);
                changed.Imports();
                changed.Exports();
            } catch (const std::exception &) {
                ++rejected;
            }
        }
        for (size_t size = 0; size < 64; ++size) {
            bool refused = false;
            try {
                automatic::Pe({input.data(), size}).Imports();
            } catch (const std::exception &) {
                refused = true;
            }
            scan::Require(refused, "Truncated PE accepted");
        }
        auto manifest = scan::Json{{"files", scan::Json::array()}};
        const auto &original = *reinterpret_cast<const guardian::pevolume::Header *>(volume.data());
        for (uint32_t i = 0; i < original.files; ++i)
            manifest["files"].push_back({{"path", guardian::pevolume::FileAt(original, i).path}});
        guardian::nt::Info binding;
        automatic::Stage(volume, manifest, binding, input);
        const auto &h = *reinterpret_cast<const guardian::pevolume::Header *>(volume.data());
        const auto &root = guardian::pevolume::FileAt(h, h.root);
        scan::Require(root.bytes == input.size() &&
                          !std::memcmp(volume.data() + root.offset, input.data(), input.size()) &&
                          std::string(root.path) == "c:\\app\\sample.exe",
                      "Rebinding changed input bytes");
        scan::Require(automatic::Missing(input, volume).empty(), "Rebinding changed dependencies");
        std::cout << "Automatic runtime ASan PASS: 500 PE mutations, 64 truncations, rebind; rejected "
                  << rejected << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
