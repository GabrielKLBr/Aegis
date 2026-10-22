#include "../src/microvm/boot/KernelBoot.h"
#include "../src/microvm/gui/Viewer.h"
#include <iostream>
using namespace av::microvm;
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            return 2;
        const auto image = boot::KernelImage::Read(argv[1]);
        const auto root = std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(root);
        boot::CpuOptions cpu{};
        cpu.enabled = true;
        cpu.graphics = true;
        std::vector<boot::BootResult> results;
        uint32_t checks = 0;
        const auto check = [&](bool ok) {
            ++checks;
            if (!ok)
                throw std::runtime_error("Graphics boot check " + std::to_string(checks));
        };
        uint64_t baseline = 0;
        for (uint32_t ram : {16u, 32u, 64u}) {
            gui::Options o;
            o.input = gui::ReferenceInput();
            o.output = root / ("direct-" + std::to_string(ram));
            std::filesystem::create_directories(o.output);
            boot::KernelBootSession direct;
            direct.Prepare(image, uint64_t(ram) << 20, 11, cpu, {}, {}, o);
            auto row = direct.Run(std::chrono::seconds(15));
            results.push_back(row);
            check(row.success);
            check(row.graphicsFrames == 6 && row.graphicsInputs == 12 && row.graphics.denied == 7);
            check(row.graphicsHash != 0);
            if (!baseline)
                baseline = row.graphicsHash;
            check(row.graphicsHash == baseline);
            o.output = root / ("paused-" + std::to_string(ram));
            std::filesystem::create_directories(o.output);
            boot::KernelBootSession paused;
            paused.Prepare(image, uint64_t(ram) << 20, 11, cpu, {}, {}, o);
            paused.PauseAtGraphicsRequest(4);
            auto stop = paused.Run(std::chrono::seconds(15));
            check(stop.paused && !stop.success);
            auto snapshot = paused.Capture();
            for (uint32_t branch = 0; branch < 2; ++branch) {
                o.output = root / ("resume-" + std::to_string(ram) + "-" + std::to_string(branch));
                std::filesystem::create_directories(o.output);
                boot::KernelBootSession resumed;
                resumed.Prepare(image, uint64_t(ram) << 20, 11, cpu, {}, {}, o);
                resumed.Restore(snapshot);
                row = resumed.Run(std::chrono::seconds(15));
                results.push_back(row);
                check(row.success && row.graphicsHash == baseline && row.graphicsFrames == 6 &&
                      row.graphicsInputs == 12);
            }
            o.input[1].key = 27;
            boot::KernelBootSession changed;
            changed.Prepare(image, uint64_t(ram) << 20, 11, cpu, {}, {}, o);
            bool refused = false;
            try {
                changed.Restore(snapshot);
            } catch (const std::exception &) {
                refused = true;
            }
            check(refused);
        }
        for (uint32_t mode = 0; mode < 2; ++mode) {
            gui::Options close;
            close.output = root / ("early-close-" + std::to_string(mode));
            std::filesystem::create_directories(close.output);
            if (mode)
                close.input.push_back({guardian::graphics::Input::Blur});
            close.input.push_back({guardian::graphics::Input::Close});
            boot::KernelBootSession early;
            early.Prepare(image, 32ull << 20, 11, cpu, {}, {}, close);
            const auto row = early.Run(std::chrono::seconds(15));
            results.push_back(row);
            check(row.success && row.graphicsFrames == 3 && row.graphicsInputs == mode + 1);
        }
        boot::WriteBootReport(root / "graphics-boots.json", results);
        std::cout << checks
                  << " graphics real-guest checks; 11 successful boots/resumes + 3 pauses; CPL3 two "
                     "applications identical pixels after restore PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
