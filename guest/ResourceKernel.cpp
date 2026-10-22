#include "ScalableVolume.h"
#include "guardian/ResourceAbi.h"
#include <intrin.h>
#include <xmmintrin.h>
using namespace guardian;
namespace {
volatile long readyMask = 0, topologyMask = 0, doneMask = 0, go = 0, fpuMask = 0;
volatile __int64 counter = 0;
uint64_t sequence = 0;
uint32_t evidence[4][12]{};
volatile const uint64_t capability[4]{resource::CapabilityMagic, resource::Version, scalable::MaxCapacity, 4};
__declspec(noreturn) void Spin() noexcept {
    for (;;)
        _mm_pause();
}
scalable::Status Io(void *, scalable::BlockOp op, uint64_t at, uint8_t *data) noexcept {
    auto *req = reinterpret_cast<volatile resource::Request *>(resource::RequestGpa);
    if (op == scalable::BlockOp::Write)
        for (uint32_t i = 0; i < scalable::ChunkBytes; ++i)
            reinterpret_cast<volatile uint8_t *>(resource::DataGpa)[i] = data[i];
    req->sequence = ++sequence;
    req->offset = at;
    req->buffer = resource::DataGpa;
    req->available = 0;
    req->operation = uint32_t(op);
    req->bytes = scalable::ChunkBytes;
    req->status = uint32_t(scalable::Status::Invalid);
    req->reserved = 0;
    _ReadWriteBarrier();
    __outdword(resource::Port, 1);
    _ReadWriteBarrier();
    const auto status = scalable::Status(req->status);
    if (status == scalable::Status::Ok &&
        (op == scalable::BlockOp::Read || op == scalable::BlockOp::AllocationMap))
        for (uint32_t i = 0; i < scalable::ChunkBytes; ++i)
            data[i] = reinterpret_cast<const volatile uint8_t *>(resource::DataGpa)[i];
    return status;
}
uint64_t Available(void *) noexcept {
    auto *req = reinterpret_cast<volatile resource::Request *>(resource::RequestGpa);
    req->sequence = ++sequence;
    req->operation = resource::SpaceOperation;
    req->offset = 0;
    req->buffer = resource::DataGpa;
    req->bytes = scalable::ChunkBytes;
    req->reserved = 0;
    _ReadWriteBarrier();
    __outdword(resource::Port, 1);
    _ReadWriteBarrier();
    return req->available;
}
bool Topology(uint32_t id, uint32_t count) noexcept {
    int regs[4]{};
    bool good = true;
    __cpuidex(regs, 1, 0);
    for (uint32_t n = 0; n < 4; ++n)
        evidence[id][n] = uint32_t(regs[n]);
    good = good && ((uint32_t(regs[1]) >> 16) & 255) == count && (uint32_t(regs[1]) >> 24) == id;
    __cpuidex(regs, 0xB, 0);
    for (uint32_t n = 0; n < 4; ++n)
        evidence[id][4 + n] = uint32_t(regs[n]);
    good = good && uint32_t(regs[3]) == id && regs[1] == 1;
    __cpuidex(regs, 0xB, 1);
    for (uint32_t n = 0; n < 4; ++n)
        evidence[id][8 + n] = uint32_t(regs[n]);
    return good && uint32_t(regs[1]) == count && uint32_t(regs[3]) == id;
}
} // namespace
extern "C" __declspec(noreturn) void GuardianResourceEntry(const resource::Boot *p, uint64_t id) noexcept {
    if (capability[0] != resource::CapabilityMagic || capability[1] != resource::Version ||
        capability[2] != scalable::MaxCapacity || capability[3] != 4)
        Spin();
    if (uint64_t(p) != resource::BootGpa || p->magic != resource::Magic || p->version != resource::Version ||
        p->bytes != sizeof(*p) || !p->bootId || p->vcpus < 1 || p->vcpus > 4 || id >= p->vcpus ||
        p->reserved || p->ramBytes < 16ull << 20)
        Spin();
    const auto boot = *p;
    const uint32_t csr = 0x1F80u | (uint32_t(id) << 13);
    _mm_setcsr(csr);
    if (_mm_getcsr() == csr)
        _InterlockedOr(&fpuMask, long(1u << id));
    const auto bit = long(1u << id), all = long((1u << boot.vcpus) - 1);
    if (Topology(uint32_t(id), boot.vcpus))
        _InterlockedOr(&topologyMask, bit);
    _InterlockedOr(&readyMask, bit);
    if (!id) {
        while (_InterlockedCompareExchange(&readyMask, 0, 0) != all)
            _mm_pause();
        _InterlockedExchange(&go, 1);
    } else
        while (!_InterlockedCompareExchange(&go, 0, 0))
            _mm_pause();
    for (uint32_t n = 0; n < resource::Iterations; ++n) {
        _InterlockedIncrement64(&counter);
        if (!id && n == resource::Iterations / 2) {
            auto *req = reinterpret_cast<resource::Request *>(resource::RequestGpa);
            *req = {};
            req->sequence = ++sequence;
            req->operation = resource::SmpCheckpointOperation;
            _ReadWriteBarrier();
            __outdword(resource::Port, 1);
            _ReadWriteBarrier();
        }
    }
    _InterlockedOr(&doneMask, bit);
    if (id) {
        for (;;) {
            if (_mm_getcsr() != csr)
                _InterlockedAnd(&fpuMask, ~bit);
            _mm_pause();
        }
    }
    while (_InterlockedCompareExchange(&doneMask, 0, 0) != all)
        _mm_pause();
    resource::Report report{};
    report.bootId = boot.bootId;
    report.vcpus = boot.vcpus;
    report.readyMask = uint32_t(readyMask);
    report.topologyMask = uint32_t(topologyMask);
    report.doneMask = uint32_t(doneMask);
    report.counter = uint64_t(counter);
    report.capacity = boot.capacity;
    for (uint32_t c = 0; c < boot.vcpus; ++c)
        for (uint32_t n = 0; n < 12; ++n)
            report.cpuid[c][n] = evidence[c][n];

    scalable::Volume volume;
    auto *data = reinterpret_cast<uint8_t *>(0xA00000);
    auto *read = reinterpret_cast<uint8_t *>(0xA10000);
    bool ok = volume.Configure(nullptr, Io, Available, reinterpret_cast<scalable::Metadata *>(0x800000),
                               reinterpret_cast<scalable::Metadata *>(0x900000),
                               reinterpret_cast<uint8_t *>(0xA20000), boot.capacity, boot.quota);
    if (ok && volume.Mount(true) == scalable::Status::Ok) {
        report.checks |= 2;
        const auto created = volume.Create("large.bin");
        ok = created == scalable::Status::Ok || created == scalable::Status::Exists;
        if (ok) {
            for (uint32_t b = 0; b < 80 && ok; ++b) {
                for (uint32_t i = 0; i < scalable::ChunkBytes; ++i)
                    data[i] = uint8_t(b * 7 + i);
                uint32_t n = 0;
                ok = volume.Write("large.bin", uint64_t(b) * scalable::ChunkBytes, data, scalable::ChunkBytes,
                                  n) == scalable::Status::Ok &&
                     n == scalable::ChunkBytes;
            }
            if (ok)
                report.checks |= 4;
        }
        if (ok) {
            for (uint32_t b = 0; b < 80 && ok; ++b) {
                uint32_t n = 0;
                ok = volume.Read("large.bin", uint64_t(b) * scalable::ChunkBytes, read, scalable::ChunkBytes,
                                 n) == scalable::Status::Ok &&
                     n == scalable::ChunkBytes;
                for (uint32_t i = 0; i < scalable::ChunkBytes && ok; ++i)
                    ok = read[i] == uint8_t(b * 7 + i);
            }
            if (ok)
                report.checks |= 8;
        }
        if (ok && volume.Size("large.bin", report.fileBytes) == scalable::Status::Ok &&
            report.fileBytes == 5ull << 20)
            report.checks |= 16;
        report.space = volume.QuerySpace();
        if (report.space.capacity == boot.capacity && report.space.capacity >= 1ull << 30 &&
            report.space.allocated > 5ull << 20 && report.space.available <= report.space.free)
            report.checks |= 32;
    }
    report.fpuMask = uint32_t(_InterlockedCompareExchange(&fpuMask, 0, 0));
    if (report.topologyMask == uint32_t(all) &&
        report.counter == uint64_t(boot.vcpus) * resource::Iterations && report.fpuMask == uint32_t(all) &&
        _mm_getcsr() == csr)
        report.checks |= 1;
    report.status = report.checks == resource::RequiredChecks ? 0u : 1u;
    *reinterpret_cast<resource::Report *>(resource::ReportGpa) = report;
    auto *req = reinterpret_cast<resource::Request *>(resource::RequestGpa);
    *req = {};
    req->sequence = ++sequence;
    req->operation = resource::ReportOperation;
    _ReadWriteBarrier();
    __outdword(resource::Port, 1);
    Spin();
}
