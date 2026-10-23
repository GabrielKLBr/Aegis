#include "EngineFacade.h"
#include "Scanner.h"
#include <iostream>

namespace
{
void Check(bool value, const char *why)
{
    if (!value)
        throw std::runtime_error(why);
}
void Put(const std::filesystem::path &p, const std::string &s)
{
    av::scan::Write(p, {reinterpret_cast<const uint8_t *>(s.data()), s.size()});
}
} // namespace
int main(int argc, char **argv)
{
    try
    {
        Check(argc == 2, "test directory required");
        const auto root = std::filesystem::absolute(av::scan::Utf8Path(argv[1])) / av::scan::Token();
        av::scan::PrivateState(root);
        Check(SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str()) != FALSE, "test state override");
        Put(root / "signatures.txt", "# no extra signatures\n");
        Put(root / "document.txt", "ordinary document");
        Put(root / "positive.txt", std::string("AEGIS_SCANNER_TEST_") + "POSITIVE_V1");
        Put(root / "opaque.msi", "unsupported fixture");
        av::SignatureDatabase db;
        Check(db.Load((root / "signatures.txt").wstring()), "signature load");
        av::Scanner scanner(db, nullptr, false);
        av::ScanStats stats;
        scanner.ScanPath((root / "document.txt").wstring(), stats);
        Check(stats.filesScanned == 1 && stats.unknown == 1 && !stats.threatsFound && !stats.errors,
              "console unknown");
        scanner.ScanPath((root / "positive.txt").wstring(), stats);
        Check(stats.threatsFound == 1 && !stats.quarantined, "console real YARA");
        scanner.ScanPath((root / "opaque.msi").wstring(), stats);
        Check(stats.errors == 1, "console incomplete visible");
        av::Engine engine;
        Check(engine.LoadSignatures((root / "signatures.txt").wstring()), "UI signature load");
        engine.StartScan((root / "document.txt").wstring(), {}, {});
        engine.Join();
        Check(engine.State().filesScanned == 1 && engine.State().unknown == 1 && !engine.State().inconclusive,
              "UI single-file target");
        engine.StartScan((root / "missing").wstring(), {}, {});
        engine.Join();
        Check(!engine.IsRunning() && engine.State().inconclusive == 1, "UI missing target visible");
        engine.StartScan((root / "positive.txt").wstring(), {}, {});
        engine.Join();
        Check(engine.State().threatsFound == 1 && !engine.State().detections.empty(), "UI typed detection");
        engine.StartScan(root.wstring(), {}, {});
        engine.Cancel();
        engine.Join();
        Check(engine.State().cancel && !engine.IsRunning(), "UI cancellation joins worker");
        Put(root / "invalid.txt", "sha256:bad:Bad\n");
        Check(!db.Load((root / "invalid.txt").wstring()), "invalid signatures rejected");
        Check(db.Analyze((root / "document.txt").wstring())["classification"] == "inconclusive",
              "invalid database cannot produce unknown/clean");
        std::cout << "scanner facade PASS\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
