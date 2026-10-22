#include "../src/microvm/state/VmManager.h"
#include <atomic>
#include <exception>
#include <iostream>
#include <thread>
#ifdef _WIN32
#include <Windows.h>
#endif
using namespace av::microvm::state;
namespace {
unsigned checks = 0;
void Check(bool value) {
    ++checks;
    if (!value)
        throw std::runtime_error("Catalog check " + std::to_string(checks));
}
template <class F> void Reject(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    Check(failed);
}
} // namespace
int main(int argc, char **argv) {
    try {
        Check(argc == 3);
        const auto image = std::filesystem::absolute(argv[1]);
        const auto output = std::filesystem::absolute(argv[2]) / Token();
        std::filesystem::create_directories(output);
        const auto catalog = output / "catalog";
        const auto foreign = output / "foreign.txt";
        AtomicText(foreign, "Unrelated user content\n");
        const auto original = ReadFile(foreign);
        {
            VmManager m(catalog);
            VmConfig c; c.hostReserveMiB=1024;
            c.id = "source";
            c.contract = "basic";
            c.ramMiB = 16;
            m.Create(c, image);
        }
        const auto sourceImage = ReadFile(catalog / "source/image.gk"),
                   sourceBase = ReadFile(catalog / "source/base.gvd");
        for (int64_t fault = 0; fault < 32; ++fault) {
            {
                VmManager m(catalog);
                VmConfig c; c.hostReserveMiB=1024;
                c.id = "creation";
                c.contract = "basic";
                c.ramMiB = 16;
                FailAfter(fault);
                try {
                    m.Create(c, image);
                } catch (const std::exception &) {
                }
                FailAfter(-1);
            }
            {
                VmManager recovered(catalog);
                if (std::filesystem::exists(catalog / "creation")) {
                    Check(recovered.Status("creation").find("stopped") != std::string::npos);
                    Check(ReadFile(catalog / "creation/image.gk") == sourceImage);
                    recovered.Delete("creation");
                }
                Check(ReadFile(catalog / "source/image.gk") == sourceImage &&
                      ReadFile(catalog / "source/base.gvd") == sourceBase);
            }
        }
        for (int64_t fault = 0; fault < 32; ++fault) {
            {
                VmManager m(catalog);
                FailAfter(fault);
                try {
                    m.Clone("source", "branch");
                } catch (const std::exception &) {
                }
                FailAfter(-1);
            }
            {
                VmManager recovered(catalog);
                if (std::filesystem::exists(catalog / "branch")) {
                    Check(recovered.Status("branch").find("stopped") != std::string::npos);
                    Check(ReadFile(catalog / "branch/image.gk") == sourceImage &&
                          ReadFile(catalog / "branch/base.gvd") == sourceBase);
                    recovered.Delete("branch");
                }
                Check(ReadFile(catalog / "source/image.gk") == sourceImage &&
                      ReadFile(catalog / "source/base.gvd") == sourceBase);
            }
        }
        // A committed intent owns its unpublished directory, even before a marker
        // or the last data file was written. Recovery keeps the live source intact.
        for (const auto kind : {".create-interrupted", ".delete-interrupted"}) {
            const auto stage = catalog / kind;
            auto intent = stage;
            intent += ".intent";
            AtomicText(intent, "GuardianVM phase 9\ninterrupted\n");
            std::filesystem::create_directory(stage);
            AtomicText(stage / "incomplete", "partial data");
            {
                VmManager recovered(catalog);
                Check(!std::filesystem::exists(stage) && !std::filesystem::exists(intent));
                Check(ReadFile(catalog / "source/base.gvd") == sourceBase);
            }
        }
        {
            VmManager m(catalog);
            for (const auto bad : {"../outside", "con", "aux", "com1", "a/b", "A", "a..b", ""})
                Reject([&] { m.Delete(bad); });
            Reject([&] { m.Delete("unknown"); });
            Reject([&] { m.Configure("source", 1025, 10000); });
            Check(m.Status("source").find("\"ramMiB\":16") != std::string::npos);
            m.Clone("source", "branch");
            Reject([&] { m.Clone("source", "branch"); });
            m.Delete("branch");
        }
        const auto published = output / "published";
        const std::vector<uint8_t> left(4096, 0x5A), right(4096, 0xA5);
        AtomicWrite(published, left);
        {
            std::atomic<bool> done{false};
            std::exception_ptr failure;
            std::jthread writer([&] {
                try {
                    for (unsigned i = 0; i < 64; ++i)
                        AtomicWrite(published, i & 1 ? left : right);
                } catch (...) {
                    failure = std::current_exception();
                }
                done = true;
            });
            unsigned reads = 0;
            while (!done || reads < 128) {
                const auto bytes = ReadFile(published);
                Check(bytes == left || bytes == right);
                ++reads;
            }
            writer.join();
            if (failure)
                std::rethrow_exception(failure);
        }
        std::cout << "Concurrent atomic state publication PASS\n";
#ifdef _WIN32
        {
            // A permanently blocked replacement must fail within the retry
            // budget, preserve the old generation and clean the temporary file.
            const auto old=ReadFile(published);
            const auto held=CreateFileW(published.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            Check(held!=INVALID_HANDLE_VALUE);
            Reject([&]{AtomicWrite(published,right);});CloseHandle(held);
            Check(ReadFile(published)==old);
            for(const auto& entry:std::filesystem::directory_iterator(output))Check(!entry.path().filename().string().starts_with(".partial-"));
            AtomicWrite(published,right);Check(ReadFile(published)==right);
            std::cout<<"Blocked Windows replacement preserves durable generation and releases retries PASS\n";
        }
#endif
        const auto adopted = output / "unowned";
        std::filesystem::create_directory(adopted);
        AtomicText(adopted / "user.txt", "preserve");
        Reject([&] { VmManager invalid(adopted); });
        Check(ReadFile(adopted / "user.txt") ==
              std::vector<uint8_t>({'p', 'r', 'e', 's', 'e', 'r', 'v', 'e'}));
#ifndef _WIN32
        std::filesystem::create_symlink(foreign, catalog / "source/worker.log");
        {
            VmManager m(catalog);
            Reject([&] { m.Start("source", std::filesystem::absolute(argv[0]), true); });
            Check(ReadFile(foreign) == original);
        }
        std::filesystem::remove(catalog / "source/worker.log");
        std::filesystem::create_directory_symlink(output, catalog / "alien");
        {
            VmManager m(catalog);
            Reject([&] { m.Delete("alien"); });
        }
        Check(ReadFile(foreign) == original);
        std::filesystem::remove(catalog / "alien");
#endif
        {
            VmManager m(catalog);
            m.Delete("source");
            Check(m.List() == "[]");
        }
        Check(ReadFile(foreign) == original);
        std::filesystem::remove_all(output);
        std::cout << checks
                  << " VM catalog ownership, 64 interrupted create/clone transactions, recovery and "
                     "isolation checks PASS\n";
        return 0;
    } catch (const std::exception &e) {
        FailAfter(-1);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
