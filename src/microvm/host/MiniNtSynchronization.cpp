#include "MiniNt.h"
#include "GuestCall.h"
#include <chrono>
#include <cstring>

namespace av { namespace microvm {
uint32_t MiniNt::AcquireMutant(const std::shared_ptr<MutantObject>& object, uint64_t thread) {
    if (object->state.Owner() && object->state.Owner() != thread) return GuestMutant::Unavailable;
    if (!object->state.Owner()) {
        if (ownedMutants_.size() >= 4096) return GuestObjects::NoMemory;
        // Publish the retaining reference before changing ownership. Allocation
        // failure must leave the object available and the caller unblocked.
        ownedMutants_.emplace(object.get(), object);
    }
    return object->state.Acquire(thread);
}
void MiniNt::HandoffMutant(MicroVm& vm, const std::shared_ptr<MutantObject>& object) {
    if (object->state.Owner()) return;
    const auto waiter = scheduler_.NextWaiter(reinterpret_cast<uint64_t>(object.get()),
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count() / 100));
    if (waiter) {
        // The old owner's reference remains in ownedMutants_, so handoff does
        // not allocate and ownership is assigned before the waiter is runnable.
        const auto status = object->state.Acquire(waiter);
        if ((status != 0 && status != GuestMutant::Abandoned) || !scheduler_.CompleteWait(waiter, status))
            vm.FailExecution(L"Invalid guest mutant handoff");
    } else ownedMutants_.erase(object.get());
}
void MiniNt::AbandonMutants(MicroVm& vm, uint64_t thread) {
    for (auto it = ownedMutants_.begin(); it != ownedMutants_.end();) {
        auto object = it++->second;
        if (object->state.Abandon(thread)) HandoffMutant(vm, object);
    }
}

bool MiniNt::DispatchSynchronization(MicroVm& vm, const std::string& name) {
    const bool semaphoreCreate = name == "NtCreateSemaphore", mutantCreate = name == "NtCreateMutant";
    const bool semaphoreQuery = name == "NtQuerySemaphore", mutantQuery = name == "NtQueryMutant";
    const bool semaphoreRelease = name == "NtReleaseSemaphore", mutantRelease = name == "NtReleaseMutant";
    const bool wait = name == "NtWaitForSingleObject";
    const bool open = name == "NtOpenSemaphore" || name == "NtOpenMutant";
    if (!semaphoreCreate && !mutantCreate && !semaphoreQuery && !mutantQuery && !semaphoreRelease && !mutantRelease && !wait && !open) return false;
    using O = GuestObjects;
    detail::ObjectCall call(vm, virtualMemory_);
    const auto finish = [&](uint32_t status) {
        call.Status(status); report_.Add(ReportBuilder::Cat::Process, name, "", "NTSTATUS " + std::to_string(status)); return true;
    };
    if (!call.valid) return finish(0xC0000001);
    if (open) return finish(O::NotSupported); // No named-object namespace yet.
    const auto a1 = call.Arg(1), a2 = call.Arg(2), a3 = call.Arg(3), a4 = call.Arg(4);
    try {
        if (semaphoreCreate || mutantCreate) {
            if (!call.Access(a1, 8, 2)) return finish(detail::AccessViolation);
            int32_t maximum = 0, initial = static_cast<int32_t>(a4);
            if (semaphoreCreate) {
                uint64_t fifth = 0; if (!call.Stack(5, fifth)) return finish(detail::AccessViolation);
                maximum = static_cast<int32_t>(fifth);
                if (!GuestSemaphore::Valid(initial, maximum)) return finish(O::InvalidParameter);
            }
            uint32_t attributes = 0, desired = 0; uint64_t root = 0; std::string objectName;
            auto status = call.Attributes(a3, attributes, root, objectName); if (status) return finish(status);
            if (root || !objectName.empty()) return finish(O::NotSupported);
            status = O::Normalize(semaphoreCreate ? ObjectKind::Semaphore : ObjectKind::Mutant, static_cast<uint32_t>(a2), desired);
            if (status) return finish(status);
            if (!objects_.HasRoom()) return finish(O::NoMemory);
            uint64_t handle = 0;
            if (semaphoreCreate) status = objects_.Open(std::make_shared<SemaphoreObject>(initial, maximum), desired, attributes, handle);
            else {
                auto object = std::make_shared<MutantObject>();
                if (a4 & 0xFF) {
                    if (!StartThreading(vm, status)) return finish(status);
                    status = AcquireMutant(object, scheduler_.Current()); if (status) return finish(status);
                }
                status = objects_.Open(object, desired, attributes, handle);
                if (status) ownedMutants_.erase(object.get());
            }
            if (!status) call.memory.Write(a1, handle);
            return finish(status);
        }
        std::shared_ptr<SemaphoreObject> semaphore; std::shared_ptr<MutantObject> mutant;
        if (wait) {
            // Leave events, threads and unsupported file waits on their existing
            // path. Wrong types and invalid handles keep that path's statuses.
            auto status = objects_.Reference(a1, 0x100000, semaphore);
            if (status == O::TypeMismatch) status = objects_.Reference(a1, 0x100000, mutant);
            if (status == O::TypeMismatch || status == O::InvalidHandle) return false;
            if (status) return finish(status);
            if (a2 & 0xFF) return finish(O::NotSupported);
            int64_t timeout = 0; if (a3 && !call.Read(a3, timeout)) return finish(detail::AccessViolation);
            if (semaphore && semaphore->state.Acquire()) return finish(0);
            if (mutant) {
                if (!StartThreading(vm, status)) return finish(status);
                status = AcquireMutant(mutant, scheduler_.Current());
                if (status != GuestMutant::Unavailable) return finish(status);
            }
            if (a3 && !timeout) return finish(0x102);
            uint32_t failure = 0;
            std::shared_ptr<GuestObject> object = semaphore ? std::static_pointer_cast<GuestObject>(semaphore) : std::static_pointer_cast<GuestObject>(mutant);
            if (BlockThread(vm, std::move(object), a3 != 0, timeout, failure)) return true;
            return finish(failure);
        }
        const auto status = (semaphoreQuery || semaphoreRelease) ?
            objects_.Reference(a1, semaphoreQuery ? 1u : 2u, semaphore) :
            objects_.Reference(a1, mutantQuery ? 1u : 0u, mutant);
        if (status) return finish(status);
        if (semaphoreQuery || mutantQuery) {
            if (static_cast<uint32_t>(a2) != 0) return finish(0xC0000003);
            uint64_t lengthOut = 0;
            if (!call.Stack(5, lengthOut) || (lengthOut && !call.Access(lengthOut, 4, 2))) return finish(detail::AccessViolation);
            if (static_cast<uint32_t>(a4) < 8) { if (lengthOut) call.memory.Write(lengthOut, uint32_t(8)); return finish(0xC0000004); }
            if (!call.Access(a3, 8, 2)) return finish(detail::AccessViolation);
            if (lengthOut && a3 < lengthOut + 4 && lengthOut < a3 + 8) return finish(O::InvalidParameter);
            if (semaphore) {
                struct Basic { int32_t current, maximum; };
                call.memory.Write(a3, Basic{semaphore->state.Count(), semaphore->state.Maximum()});
            } else {
                struct Basic { int32_t current; uint8_t owned, abandoned; uint16_t padding; };
                static_assert(sizeof(Basic) == 8, "MUTANT_BASIC_INFORMATION layout");
                call.memory.Write(a3, Basic{mutant->state.Count(), static_cast<uint8_t>(threading_ && mutant->state.Owner() == scheduler_.Current()),
                    static_cast<uint8_t>(mutant->state.IsAbandoned()), 0});
            }
            if (lengthOut) call.memory.Write(lengthOut, uint32_t(8));
            return finish(0);
        }
        const auto previousOut = semaphoreRelease ? a3 : a2;
        if (previousOut && !call.Access(previousOut, 4, 2)) return finish(detail::AccessViolation);
        int32_t previous = 0;
        if (semaphore) {
            const auto result = semaphore->state.Release(static_cast<int32_t>(a2), previous); if (result) return finish(result);
            // Reserve one permit per completed wait, including suspended waiters.
            // Expiration runs before selection, so a timeout cannot consume one.
            while (semaphore->state.Count() > 0) {
                const auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count() / 100);
                const auto waiter = scheduler_.NextWaiter(reinterpret_cast<uint64_t>(semaphore.get()), now);
                if (!waiter) break;
                semaphore->state.Acquire();
                if (!scheduler_.CompleteWait(waiter, 0)) { vm.FailExecution(L"Invalid guest semaphore handoff"); return true; }
            }
        } else {
            const auto result = mutant->state.Release(threading_ ? scheduler_.Current() : 0, previous); if (result) return finish(result);
            HandoffMutant(vm, mutant);
        }
        if (previousOut) call.memory.Write(previousOut, previous);
        return finish(0);
    } catch (const std::bad_alloc&) { return finish(O::NoMemory); }
}
} }
