#include "KernelBoot.h"
#include <charconv>
#include <iostream>
using namespace av::microvm::boot;
namespace {
uint32_t Number(const char* s) {
    uint32_t n = 0; const std::string text(s);
    const auto result = std::from_chars(text.data(), text.data() + text.size(), n);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size()) throw std::runtime_error("Invalid numeric argument");
    return n;
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--help")) {
            std::cout << "Guardian own-kernel boot (WHP, one vCPU)\n"
                "  guardian_boot --check IMAGE.gk\n"
                "  guardian_boot IMAGE.gk [--cpu|--memory|--threads|--processes|--io|--storage|--network] [--network-profile offline|simulated] [--capture DIRECTORY] [--ram-mib 16|64] [--repeat 1..100] [--report FILE.json] [--disk BASE --overlay FILE --create-disk]\n"; return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--check") {
            const auto image = KernelImage::Read(argv[2]);
            std::cout << "Kernel PE32+ x64 valid: base=0x200000 bytes=" << image.Size() << " sections=" << image.Sections().size()
                << " imports=0 TLS=0 relocations=0 W^X=pass\n"; return 0;
        }
        uint32_t ramMiB = 16, repeat = 1; std::filesystem::path report; CpuOptions cpu;StorageOptions storage;av::microvm::net::Options networking;
        for (int i = 2; i < argc; ++i) {
            if (std::string(argv[i]) == "--network") {cpu.enabled=true;cpu.network=true;continue;}
            if (std::string(argv[i]) == "--cpu") { cpu.enabled = true; continue; }
            if (std::string(argv[i]) == "--memory") { cpu.enabled = true; cpu.memory = true; continue; }
            if (std::string(argv[i]) == "--threads") { cpu.enabled = true; cpu.threads = true; continue; }
            if (std::string(argv[i]) == "--processes") { cpu.enabled = true; cpu.processes = true; continue; }
            if (std::string(argv[i]) == "--io") { cpu.enabled=true;cpu.processes=true;cpu.io=true;continue; }
            if (std::string(argv[i]) == "--storage") { cpu.enabled=true;cpu.processes=true;cpu.io=true;cpu.storage=true;continue; }
            if (std::string(argv[i]) == "--create-disk") { storage.createIfMissing=true;continue; }
            if (i + 1 >= argc) throw std::runtime_error("Missing option value");
            const std::string option(argv[i]);
            if(option=="--network-profile"){const std::string mode=argv[i+1];if(mode=="offline")networking.policy.mode=guardian::net::Mode::Offline;else if(mode=="simulated")networking.policy.mode=guardian::net::Mode::Simulated;else throw std::runtime_error("Use guardian-vm for lab/proxy policies");}
            else if(option=="--capture"){networking.capture=std::filesystem::absolute(argv[i+1]);std::filesystem::create_directories(networking.capture);}
            else if (option == "--ram-mib") ramMiB = Number(argv[i + 1]);
            else if (option == "--repeat") repeat = Number(argv[i + 1]);
            else if (option == "--disk") storage.base=std::filesystem::absolute(argv[i+1]);
            else if (option == "--overlay") storage.overlay=std::filesystem::absolute(argv[i+1]);
            else if (option == "--report") report = argv[i + 1];
            else throw std::runtime_error("Unknown option");
            ++i;
        }
        if ((ramMiB != 16 && ramMiB != 64) || !repeat || repeat > 100) throw std::runtime_error("Phase-1 resource/repeat limit rejected");
        if(!cpu.storage && (!storage.base.empty() || !storage.overlay.empty() || storage.createIfMissing))throw std::runtime_error("Disk options require --storage");
        const auto image = KernelImage::Read(argv[1]);
        if (!WhpAvailable()) { std::cerr << "WHP is unavailable; no Windows settings changed.\n"; return 77; }
        std::vector<BootResult> rows;
        for (uint32_t i = 0; i < repeat; ++i) {
            KernelBootSession session; session.Prepare(image, uint64_t(ramMiB) << 20, i + 1, cpu,storage,networking);
            rows.push_back(session.Run()); const auto& r = rows.back();
            std::cout << "boot " << r.bootId << ": " << (r.success ? "PASS" : "FAIL") << ", " << r.wallMs << " ms, exits=" << r.exits << '\n';
            if (!r.success) { std::cerr << r.error << '\n'; if (!report.empty()) WriteBootReport(report, rows); return 1; }
        }
        if (!report.empty()) WriteBootReport(report, rows); return 0;
    } catch (const std::exception& e) { std::cerr << "Kernel boot: " << e.what() << '\n'; return 1; }
}
