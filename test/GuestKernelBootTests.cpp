// Hardware integration uses only our built kernel and tiny programs defined here.
#include "../src/microvm/boot/KernelBoot.h"
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace av::microvm;
using namespace av::microvm::boot;
using namespace guardian::abi;
namespace {
void Check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
void Set(MicroVm& vm, WHV_REGISTER_NAME name, uint64_t value) {
    WHV_REGISTER_VALUE v{}; v.Reg64 = value; Check(vm.SetRegisters(&name, 1, &v), "Register injection failed");
}
void Code(KernelBootSession& s, const KernelImage& image, std::initializer_list<uint8_t> bytes) {
    Check(bytes.size() <= 64, "Owned test code too large");
    std::memcpy(static_cast<uint8_t*>(s.Machine().RamData()) + image.Entry(), bytes.begin(), bytes.size());
}
}
int main(int argc, char** argv) {
    try {
        Check(argc == 3, "Kernel and report paths required"); const auto image = KernelImage::Read(argv[1]);
        // Admission rejects bad RAM, session ID and unparsed image before WHP allocation.
        for (uint64_t ram : {uint64_t(0), MinRamBytes - 1, MaxRamBytes + PageBytes, MinRamBytes + 1}) {
            KernelBootSession s; bool rejected = false;
            try { s.Prepare(image, ram, 1); } catch (const std::runtime_error&) { rejected = true; }
            Check(rejected && !s.Machine().RamData(), "Invalid RAM reached VM allocation");
        }
        for (bool empty : {false, true}) {
            KernelBootSession s; bool rejected = false;
            try { s.Prepare(empty ? KernelImage{} : image, MinRamBytes, empty ? 1 : 0); } catch (const std::runtime_error&) { rejected = true; }
            Check(rejected && !s.Machine().RamData(), "Invalid ID/image reached VM allocation");
        }
        if (!WhpAvailable()) { std::cout << "WHP unavailable: integration skipped\n"; return 77; }
        std::vector<BootResult> rows, negative; uint64_t id = 1;
        for (uint64_t ram : {16ull << 20, 64ull << 20}) {
            for (unsigned run = 0; run < 20; ++run) {
                KernelBootSession s; s.Prepare(image, ram, id++); const auto r = s.Run(); rows.push_back(r);
                if (!r.success) { WriteBootReport(argv[2], rows); throw std::runtime_error("Repeated boot failed: " + r.error); }
                Check(r.cpl == 0 && r.stopStatus == 0 && r.privateBytesValid && r.exits >= 3 && r.exits < 10000 && r.wallMs < 3000,
                    "Boot metrics/result/time limit invalid");
            }
        }
        WriteBootReport(argv[2], rows);
        const auto fault = [&](const char* name, const std::function<void(KernelBootSession&)>& mutate,
                               const std::function<bool(const BootResult&)>& verify = [](const BootResult&) { return true; },
                               std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
            KernelBootSession s; s.Prepare(image, MinRamBytes, id++); mutate(s); auto r = s.Run(timeout); negative.push_back(r);
            if (r.success || !verify(r)) throw std::runtime_error(std::string("Negative boot not rejected correctly: ") + name + ": " + r.error);
            std::cout << "Rejected " << name << ": " << r.error << '\n';
        };
        const auto header = [&](KernelBootSession& s) { return reinterpret_cast<BootInfo*>(static_cast<uint8_t*>(s.Machine().RamData()) + BootGpa); };
        fault("bad boot magic", [&](auto& s) { header(s)->magic = 0; }, [](const auto& r) { return r.console.find("invalid boot header") != std::string::npos; });
        fault("bad boot version", [&](auto& s) { header(s)->major = 99; });
        fault("out-of-range boot table", [&](auto& s) { header(s)->memoryMapGpa = UINT64_MAX; });
        fault("invalid memory map", [&](auto& s) { static_cast<uint8_t*>(s.Machine().RamData())[MapGpa] = 1; },
            [](const auto& r) { return r.console.find("invalid boot tables") != std::string::npos; });
        fault("invalid device", [&](auto& s) { static_cast<uint8_t*>(s.Machine().RamData())[DevicesGpa] = 99; });
        fault("invalid RCX", [&](auto& s) { Set(s.Machine(), WHvX64RegisterRcx, 0); },
            [](const auto& r) { return r.console.find("invalid boot pointer") != std::string::npos; });
        fault("unexpected entry return", [&](auto& s) { Code(s, image, {0xC3}); }, [](const auto& r) { return r.exception == 6 && r.rip == ReturnGpa; });
        fault("guest #UD", [&](auto& s) { Code(s, image, {0x0F, 0x0B}); }, [](const auto& r) { return r.exception == 6; });
        fault("HLT without protocol result", [&](auto& s) { Code(s, image, {0xF4}); }, [](const auto& r) { return r.stopStatus == UINT32_MAX && !r.timedOut; });
        fault("no-exit infinite loop", [&](auto& s) { Code(s, image, {0xEB, 0xFE}); }, [](const auto& r) { return r.timedOut; }, std::chrono::milliseconds(50));
        fault("byte IN", [&](auto& s) { Code(s, image, {0xE4, 0xEA, 0x0F, 0x0B}); });
        fault("wide OUT", [&](auto& s) { Code(s, image, {0xE7, 0xEA, 0x0F, 0x0B}); });
        fault("REP string OUT", [&](auto& s) { Code(s, image, {0xBA, 0xEA, 0, 0, 0, 0xB9, 1, 0, 0, 0, 0xF3, 0x6E, 0x0F, 0x0B}); });
        fault("unknown port", [&](auto& s) { Code(s, image, {0xE6, 0xEB, 0x0F, 0x0B}); });
        fault("invalid control packet", [&](auto& s) { Code(s, image, {0xE6, 0xEA, 0x0F, 0x0B}); });
        fault("oversize control payload", [&](auto& s) {
            auto* h = reinterpret_cast<MessageHeader*>(static_cast<uint8_t*>(s.Machine().RamData()) + ChannelGpa);
            h->payloadBytes = UINT32_MAX; Code(s, image, {0xE6, 0xEA, 0x0F, 0x0B});
        });
        fault("write boot header", [&](auto& s) { Code(s, image, {0xB8, 0, 0, 0x10, 0, 0xC6, 0, 1, 0xF4}); },
            [](const auto& r) { return r.memoryFault && r.faultGpa == BootGpa; });
        fault("execute writable data", [&](auto& s) { Set(s.Machine(), WHvX64RegisterRip, ChannelGpa); },
            [](const auto& r) { return r.memoryFault && r.faultGpa == ChannelGpa; });
        fault("stack guard", [&](auto& s) { Code(s, image, {0x50, 0xF4}); Set(s.Machine(), WHvX64RegisterRsp, StackGpa + PageBytes); },
            [](const auto& r) { return r.memoryFault && r.faultGpa >= StackGpa && r.faultGpa < StackGpa + PageBytes; });
        fault("write text", [&](auto& s) { Code(s, image, {0x48, 0x8D, 0x05, 0xF9, 0xFF, 0xFF, 0xFF, 0xC6, 0, 1, 0xF4}); },
            [&](const auto& r) { return r.memoryFault && r.faultGpa == image.Entry(); });
        fault("failed kernel self-test", [&](auto& s) {
            for (const auto& section : image.Sections()) if (section.access == 3)
                std::memset(static_cast<uint8_t*>(s.Machine().RamData()) + image.Base() + section.rva, 0xA5, section.mappedBytes);
        }, [](const auto& r) { return r.stopStatus > 0 && r.stopStatus < 6 && r.diagnostic == "kernel self-test FAIL"; });
        fault("stop prevents next instruction", [&](auto& s) {
            Code(s, image, {0xE6, 0xEA, 0x0F, 0x0B});
            s.Machine().MapIoTrapDetailed(ControlPort, [](MicroVm& vm, const MicroVm::IoAccess&) { vm.RequestStop(); });
        }, [](const auto& r) { return r.exits == 1 && !r.exception && !r.memoryFault && !r.timedOut; });
        auto negativePath = std::filesystem::path(argv[2]); negativePath.replace_extension("negative.json"); WriteBootReport(negativePath, negative);
        std::cout << "40 real CPL0 boots passed (20 x 16 MiB, 20 x 64 MiB); " << negative.size() << " guest failures contained; 6 pre-allocation admission cases rejected\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
