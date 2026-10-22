#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
namespace av::microvm::state {
inline void Require(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
inline uint64_t Hash(std::span<const uint8_t> bytes) {
    uint64_t h = 14695981039346656037ull;
    for (auto b : bytes) {
        h ^= b;
        h *= 1099511628211ull;
    }
    return h;
}
inline constexpr uint64_t MaxStateBytes = (1ull << 30) + (300ull << 20);
class Writer {
  public:
    std::vector<uint8_t> data;
    template <class T> void Pod(const T &value) {
        static_assert(std::is_trivially_copyable_v<T>);
        Raw(&value, sizeof(value));
    }
    void Raw(const void *p, size_t n) {
        Require(n <= MaxStateBytes - data.size(), "Snapshot exceeds size limit");
        if (n) {
            const auto *b = static_cast<const uint8_t *>(p);
            data.insert(data.end(), b, b + n);
        }
    }
    void Bytes(std::span<const uint8_t> bytes) {
        Pod(uint64_t(bytes.size()));
        Raw(bytes.data(), bytes.size());
    }
    void Text(const std::string &value) {
        Bytes({reinterpret_cast<const uint8_t *>(value.data()), value.size()});
    }
};
class Reader {
  public:
    explicit Reader(std::span<const uint8_t> bytes) : data(bytes) {
        Require(bytes.size() <= MaxStateBytes, "Snapshot size rejected");
    }
    template <class T> T Pod() {
        static_assert(std::is_trivially_copyable_v<T>);
        T value{};
        Raw(&value, sizeof(value));
        return value;
    }
    void Raw(void *p, size_t n) {
        Require(n <= data.size() - offset, "Truncated state");
        if (n)
            std::memcpy(p, data.data() + offset, n);
        offset += n;
    }
    std::vector<uint8_t> Bytes(uint64_t limit = MaxStateBytes) {
        auto n = Pod<uint64_t>();
        Require(n <= limit && n <= data.size() - offset, "State field outside bounds");
        std::vector<uint8_t> out(data.begin() + offset, data.begin() + offset + size_t(n));
        offset += size_t(n);
        return out;
    }
    std::string Text(uint64_t limit = 4096) {
        const auto bytes = Bytes(limit);
        return {bytes.begin(), bytes.end()};
    }
    void End() { Require(offset == data.size(), "Trailing state data rejected"); }

  private:
    std::span<const uint8_t> data;
    size_t offset = 0;
};
inline std::vector<uint8_t> Seal(std::span<const uint8_t> payload) {
    Writer w;
    w.Pod(uint64_t(0x393050414e535647ull));
    w.Pod(uint64_t(payload.size()));
    w.Pod(Hash(payload));
    w.Raw(payload.data(), payload.size());
    return std::move(w.data);
}
inline std::vector<uint8_t> Open(std::span<const uint8_t> file) {
    Reader r(file);
    Require(r.Pod<uint64_t>() == 0x393050414e535647ull, "State format rejected");
    auto n = r.Pod<uint64_t>(), hash = r.Pod<uint64_t>();
    Require(n <= MaxStateBytes && n + 24 == file.size(), "State length rejected");
    std::vector<uint8_t> result(size_t(n), uint8_t{});
    r.Raw(result.data(), result.size());
    r.End();
    Require(Hash(result) == hash, "State checksum rejected");
    return result;
}
struct MachineState {
    uint32_t backend = 0, version = 1;
    uint64_t exits = 0;
    std::vector<uint8_t> ram, access, processor;
    void Save(Writer &w) const {
        w.Pod(backend);
        w.Pod(version);
        w.Pod(exits);
        w.Bytes(ram);
        w.Bytes(access);
        w.Bytes(processor);
    }
    static MachineState Load(Reader &r) {
        MachineState s;
        s.backend = r.Pod<uint32_t>();
        s.version = r.Pod<uint32_t>();
        s.exits = r.Pod<uint64_t>();
        s.ram = r.Bytes(1ull << 30);
        s.access = r.Bytes(262144);
        s.processor = r.Bytes(512ull << 10);
        s.Validate();
        return s;
    }
    void Validate() const {
        Require(version == 1 && (backend == 1 || backend == 2) && ram.size() >= (16ull << 20) &&
                    ram.size() <= (1ull << 30) && !(ram.size() & 4095) &&
                    access.size() == ram.size() / 4096 && !processor.empty(),
                "Invalid machine state");
        for (auto a : access)
            Require(a == 0 || a == 1 || a == 3 || a == 5, "Invalid saved mapping");
    }
};
} // namespace av::microvm::state
