// One guest handle namespace. Guest values never become host OS handles.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <new>

namespace av { namespace microvm {
enum class ObjectKind { File, Section, Event, Key, Thread, Semaphore, Mutant };
struct GuestObject {
    explicit GuestObject(ObjectKind value) : kind(value) {}
    virtual ~GuestObject() = default;
    const ObjectKind kind;
};

class GuestObjects {
public:
    static constexpr uint32_t Success = 0, InvalidHandle = 0xC0000008,
        InvalidParameter = 0xC000000D, NoMemory = 0xC0000017,
        AccessDenied = 0xC0000022, TypeMismatch = 0xC0000024,
        NotSupported = 0xC00000BB, NotClosable = 0xC0000235;
    static constexpr uint32_t ProtectClose = 1, Inherit = 2, SameAccess = 2, SameAttributes = 4;
    struct BasicInformation {
        uint32_t attributes = 0, grantedAccess = 0, handleCount = 0, pointerCount = 0;
        uint32_t reserved[10]{};
    };
    static_assert(sizeof(BasicInformation) == 56, "PUBLIC_OBJECT_BASIC_INFORMATION layout");

    explicit GuestObjects(size_t limit = 4096) : limit_(limit) {}
    GuestObjects(const GuestObjects&) = delete;
    GuestObjects& operator=(const GuestObjects&) = delete;
    bool HasRoom() const { return entries_.size() < limit_ && next_ <= UINT64_MAX - 4; }
    size_t Count() const { return entries_.size(); }
    static uint32_t Normalize(ObjectKind kind, uint32_t desired, uint32_t& result) {
        uint32_t all = 0, read = 0, write = 0, execute = 0;
        switch (kind) {
            case ObjectKind::File: all = 0x1F01FF; read = 0x120089; write = 0x120116; execute = 0x1200A0; break;
            case ObjectKind::Section: all = 0xF001F; read = 0x20005; write = 0x20002; execute = 0x20008; break;
            case ObjectKind::Event: all = 0x1F0003; read = 0x20001; write = 0x20002; execute = 0x120000; break;
            case ObjectKind::Key: all = 0xF003F; read = 0x20019; write = 0x20006; execute = 0x20019; break;
            case ObjectKind::Thread: all = 0x1FFFFF; read = 0x20048; write = 0x20037; execute = 0x120000; break;
            case ObjectKind::Semaphore: all = 0x1F0003; read = 0x20001; write = 0x20002; execute = 0x120000; break;
            case ObjectKind::Mutant: all = 0x1F0001; read = 0x20001; write = 0x20000; execute = 0x120000; break;
        }
        auto mask = desired & ~0xF0000000u;
        if (desired & 0x80000000) mask |= read;
        if (desired & 0x40000000) mask |= write;
        if (desired & 0x20000000) mask |= execute;
        if (desired & 0x10000000) mask |= all;
        if (mask & ~all) return NotSupported; // Includes MAXIMUM_ALLOWED; no token/ACL model yet.
        result = mask; return Success;
    }
    uint32_t Open(std::shared_ptr<GuestObject> object, uint32_t desired, uint32_t attributes, uint64_t& handle) {
        if (!object || (attributes & ~uint32_t(3))) return InvalidParameter;
        uint32_t access = 0; const auto status = Normalize(object->kind, desired, access);
        if (status) return status;
        if (!HasRoom()) return NoMemory;
        try { entries_.emplace(next_, Entry{std::move(object), access, attributes}); }
        catch (const std::bad_alloc&) { return NoMemory; }
        handle = next_; next_ += 4; return Success;
    }
    template<class T> uint32_t Reference(uint64_t handle, uint32_t required, std::shared_ptr<T>& object) const {
        const auto it = entries_.find(handle);
        if (it == entries_.end()) return InvalidHandle;
        if (it->second.object->kind != T::Kind) return TypeMismatch;
        if ((it->second.access & required) != required) return AccessDenied;
        auto typed = std::dynamic_pointer_cast<T>(it->second.object);
        if (!typed) return TypeMismatch;
        object = std::move(typed); return Success;
    }
    uint32_t Close(uint64_t handle) {
        const auto it = entries_.find(handle);
        if (it == entries_.end()) return InvalidHandle;
        if (it->second.attributes & ProtectClose) return NotClosable;
        entries_.erase(it); return Success;
    }
    uint32_t SetFlags(uint64_t handle, bool inherit, bool protect) {
        const auto it = entries_.find(handle);
        if (it == entries_.end()) return InvalidHandle;
        it->second.attributes = (inherit ? Inherit : 0) | (protect ? ProtectClose : 0); return Success;
    }
    uint32_t Query(uint64_t handle, BasicInformation& information) const {
        const auto it = entries_.find(handle);
        if (it == entries_.end()) return InvalidHandle;
        information = {}; information.attributes = it->second.attributes; information.grantedAccess = it->second.access;
        for (const auto& entry : entries_) if (entry.second.object == it->second.object) ++information.handleCount;
        information.pointerCount = static_cast<uint32_t>(it->second.object.use_count());
        return Success;
    }
    uint32_t Duplicate(uint64_t source, uint32_t desired, uint32_t attributes, uint32_t options, uint64_t& target) {
        // CLOSE_SOURCE has error-path side effects and is deliberately unsupported.
        if (options & ~(SameAccess | SameAttributes)) return NotSupported;
        const auto it = entries_.find(source);
        if (it == entries_.end()) return InvalidHandle;
        uint32_t access = it->second.access;
        if (!(options & SameAccess)) {
            const auto status = Normalize(it->second.object->kind, desired, access);
            if (status) return status;
            if (access & ~it->second.access) return AccessDenied;
        }
        return Open(it->second.object, access, (options & SameAttributes) ? it->second.attributes : attributes, target);
    }
private:
    struct Entry { std::shared_ptr<GuestObject> object; uint32_t access, attributes; };
    std::map<uint64_t, Entry> entries_;
    size_t limit_;
    uint64_t next_ = 0x100; // Monotonic within this process; closed handles never alias later objects.
};
} }
