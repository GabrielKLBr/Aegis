#include "../guest/Volume.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
void Check(bool b, uint32_t n) noexcept {
    if (!b) {
        std::cerr << "Storage core failure " << n << '\n';
        std::exit(1);
    }
}
namespace {
using namespace guardian;
struct Disk {
    std::array<uint8_t, io::BlockBytes> bytes{};
    int64_t operations = -1;
};
bool Transfer(void *raw, io::Op op, uint64_t offset, uint8_t *bytes) noexcept {
    auto &d = *static_cast<Disk *>(raw);
    if (op == io::Op::Write || op == io::Op::Flush) {
        if (!d.operations)
            return false;
        if (d.operations > 0)
            --d.operations;
    }
    if (op == io::Op::Flush)
        return true;
    if (!bytes || (offset & 511) || offset >= d.bytes.size())
        return false;
    if (op == io::Op::Write)
        std::memcpy(d.bytes.data() + offset, bytes, 512);
    else
        std::memcpy(bytes, d.bytes.data() + offset, 512);
    return true;
}
void Text(char *out, const char *text) {
    for (uint32_t i = 0; text[i]; ++i)
        out[i] = text[i];
}
void InitRecovery(threads::Scheduler &scheduler) {
    using namespace storage;
    uint32_t n = 0;
    const auto verify = [&](bool b) { Check(b, 10000 + (++n)); };
    processes::Token token{};
    token.user = 100;
    token.primaryGroup = 10;
    token.integrity = 2;
    for (int64_t prefix = 0; prefix <= 51; ++prefix) {
        Disk disk;
        auto volume = std::make_unique<Volume>();
        volume->Configure(&disk, Transfer, scheduler);
        verify(volume->Mount() == Success && volume->Sequence() == 0);
        Command c{};
        c.op = Op::Open;
        Text(c.path, "C:\\initial.bin");
        c.access = All;
        c.disposition = 1;
        verify(volume->Execute(1, token, c) == Success);
        const auto handle = c.handle;
        c = {};
        c.op = Op::Write;
        c.handle = handle;
        c.length = 1;
        c.data[0] = 73;
        verify(volume->Execute(1, token, c) == Success);
        volume->Exit(1);
        c = {};
        c.op = Op::RegCreate;
        Text(c.path, "HKLM\\Initial");
        verify(volume->Execute(1, token, c) == Success);
        c = {};
        c.op = Op::RegSet;
        Text(c.path, "HKLM\\Initial");
        c.type = 4;
        c.length = 4;
        c.data[0] = 73;
        verify(volume->Execute(1, token, c) == Success);
        disk.operations = prefix;
        const auto result = volume->Flush();
        verify(result == (prefix < 51 ? IoError : Success));
        disk.operations = -1;
        volume.reset();
        auto reopened = std::make_unique<Volume>();
        reopened->Configure(&disk, Transfer, scheduler);
        verify(reopened->Mount() == Success);
        const auto sequence = reopened->Sequence();
        verify(sequence <= 1);
        c = {};
        c.op = Op::Open;
        Text(c.path, "C:\\initial.bin");
        c.access = Read;
        const auto file = reopened->Execute(2, token, c);
        verify(file == (sequence ? Success : NotFound));
        if (sequence) {
            const auto h = c.handle;
            c = {};
            c.op = Op::Read;
            c.handle = h;
            c.length = 1;
            verify(reopened->Execute(2, token, c) == Success && c.data[0] == 73);
            reopened->Exit(2);
        }
        c = {};
        c.op = Op::RegGet;
        Text(c.path, "HKLM\\Initial");
        c.length = 4;
        const auto registry = reopened->Execute(2, token, c);
        verify(registry == (sequence ? Success : NotFound));
        if (sequence)
            verify(c.data[0] == 73);
        verify(reopened->Flush() == Success && reopened->Unmount() == Success);
    }
    // Unmarked partial data and a forged or damaged intent cannot authorize format.
    for (unsigned mode = 0; mode < 3; ++mode) {
        Disk disk;
        auto v = std::make_unique<Volume>();
        v->Configure(&disk, Transfer, scheduler);
        verify(v->Mount() == Success);
        disk.bytes[FirstSlot + 512] = 99;
        if (mode == 0)
            std::memset(disk.bytes.data() + FormatIntentOffset, 0, 512);
        else if (mode == 1)
            disk.bytes[FormatIntentOffset + 16] ^= 1;
        else
            disk.bytes[FirstSlot] = 1;
        v.reset();
        v = std::make_unique<Volume>();
        v->Configure(&disk, Transfer, scheduler);
        const auto before = disk.bytes;
        verify(v->Mount() == Corrupt);
        verify(disk.bytes == before);
    }
    std::cout << n << " initial volume commit recovery checks; 52 write/flush prefixes PASS\n";
}
} // namespace
int main() {
    auto a = std::make_unique<guardian::storage::Volume>();
    auto b = std::make_unique<guardian::storage::Volume>();
    auto s = std::make_unique<guardian::threads::Scheduler>();
    const auto n = guardian::storage::StorageChecks(*a, *b, *s, Check);
    std::cout << n << " freestanding storage checks PASS\n";
    InitRecovery(*s);
}
