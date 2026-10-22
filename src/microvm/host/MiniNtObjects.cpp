// Typed guest handles and object lifetimes for the existing MiniNt personality.
#include "MiniNt.h"
#include "GuestCall.h"
#include <algorithm>
#include <cstring>

namespace av { namespace microvm {
namespace {
constexpr uint32_t AccessViolation = 0xC0000005, InvalidParameter = 0xC000000D,
    NameNotFound = 0xC0000034, NotSupported = 0xC00000BB;
using detail::ObjectCall;

std::string LowerName(std::string text) { for (auto& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A'; return text; }
}

bool MiniNt::DispatchObjects(MicroVm& vm, const std::string& name) {
    const bool fileOpen = name == "NtCreateFile" || name == "NtOpenFile";
    const bool fileIo = name == "NtReadFile" || name == "NtWriteFile";
    const bool eventChange = name == "NtSetEvent" || name == "NtResetEvent" || name == "NtClearEvent";
    const bool keyOpen = name == "NtOpenKey" || name == "NtOpenKeyEx" || name == "NtCreateKey";
    if (!fileOpen && !fileIo && !eventChange && !keyOpen && name != "NtClose" && name != "NtDuplicateObject" &&
        name != "NtQueryObject" && name != "NtSetInformationObject" && name != "NtCreateEvent" &&
        name != "NtWaitForSingleObject" && name != "NtCreateSection" && name != "NtQueryValueKey" && name != "NtSetValueKey") return false;
    ObjectCall call(vm, virtualMemory_);
    auto finish = [&](uint32_t status) { call.Status(status); report_.Add(ReportBuilder::Cat::Process, name, "", "NTSTATUS " + std::to_string(status)); return true; };
    using O = GuestObjects;
    if (!call.valid) return finish(0xC0000001);
    const auto a1 = call.Arg(1), a2 = call.Arg(2), a3 = call.Arg(3), a4 = call.Arg(4);
    try {
        if (name == "NtClose") return finish(objects_.Close(a1));
        if (name == "NtDuplicateObject") {
            uint64_t desired = 0, attributes = 0, options = 0;
            if (!call.Stack(5, desired) || !call.Stack(6, attributes) || !call.Stack(7, options)) return finish(AccessViolation);
            if (static_cast<uint32_t>(options) & ~(O::SameAccess | O::SameAttributes)) return finish(NotSupported);
            if (a1 != UINT64_MAX || a3 != UINT64_MAX) return finish(O::InvalidHandle);
            if (!call.Access(a4, 8, 2)) return finish(AccessViolation);
            uint64_t handle = 0;
            const auto status = objects_.Duplicate(a2, static_cast<uint32_t>(desired), static_cast<uint32_t>(attributes), static_cast<uint32_t>(options), handle);
            if (!status) call.memory.Write(a4, handle);
            return finish(status);
        }
        if (name == "NtQueryObject" || name == "NtSetInformationObject") {
            O::BasicInformation info{};
            auto status = objects_.Query(a1, info); if (status) return finish(status);
            const auto cls = static_cast<uint32_t>(a2);
            if (name == "NtSetInformationObject") {
                if (cls != 4) return finish(0xC0000003);
                if (static_cast<uint32_t>(a4) != 2) return finish(0xC0000004);
                uint8_t flags[2]{};
                if (!call.Access(a3, 2, 1)) return finish(AccessViolation);
                std::memcpy(flags, static_cast<const uint8_t*>(call.memory.Data()) + a3, 2);
                if (flags[0] > 1 || flags[1] > 1) return finish(InvalidParameter);
                return finish(objects_.SetFlags(a1, flags[0] != 0, flags[1] != 0));
            }
            if (cls != 0 && cls != 4) return finish(0xC0000003);
            uint64_t lengthOut = 0;
            if (!call.Stack(5, lengthOut) || (lengthOut && !call.Access(lengthOut, 4, 2))) return finish(AccessViolation);
            const uint32_t needed = cls == 0 ? sizeof(info) : 2;
            if (static_cast<uint32_t>(a4) < needed) { if (lengthOut) call.memory.Write(lengthOut, needed); return finish(0xC0000004); }
            if (!call.Access(a3, needed, 2)) return finish(AccessViolation);
            if (lengthOut && a3 < lengthOut + 4 && lengthOut < a3 + needed) return finish(InvalidParameter);
            if (cls == 0) call.memory.Write(a3, info);
            else {
                const uint8_t flags[2]{static_cast<uint8_t>((info.attributes & O::Inherit) != 0), static_cast<uint8_t>((info.attributes & O::ProtectClose) != 0)};
                std::memcpy(static_cast<uint8_t*>(call.memory.Data()) + a3, flags, 2);
            }
            if (lengthOut) call.memory.Write(lengthOut, needed);
            return finish(0);
        }
        if (name == "NtCreateEvent") {
            uint64_t initial = 0, root = 0; uint32_t attributes = 0; std::string objectName;
            if (!call.Access(a1, 8, 2) || !call.Stack(5, initial)) return finish(AccessViolation);
            auto status = call.Attributes(a3, attributes, root, objectName); if (status) return finish(status);
            if (root || !objectName.empty()) return finish(NotSupported);
            if (static_cast<uint32_t>(a4) > 1) return finish(InvalidParameter);
            uint64_t handle = 0;
            status = objects_.Open(std::make_shared<EventObject>((initial & 0xFF) != 0, static_cast<uint32_t>(a4) == 1), static_cast<uint32_t>(a2), attributes, handle);
            if (!status) call.memory.Write(a1, handle);
            return finish(status);
        }
        if (eventChange || name == "NtWaitForSingleObject") {
            std::shared_ptr<EventObject> event;
            const auto status = objects_.Reference(a1, eventChange ? 2u : 0x100000u, event);
            if (status) {
                if (!eventChange && status == O::TypeMismatch) {
                    std::shared_ptr<ThreadObject> target;
                    const auto threadStatus = objects_.Reference(a1, 0x100000, target);
                    if (!threadStatus) {
                        if (a2 & 0xFF) return finish(NotSupported);
                        int64_t timeout = 0; if (a3 && !call.Read(a3, timeout)) return finish(AccessViolation);
                        if (scheduler_.Get(target->id)->state == GuestScheduler::State::Terminated) return finish(0);
                        if (a3 && !timeout) return finish(0x102);
                        uint32_t failure = 0;
                        if (BlockThread(vm, target, a3 != 0, timeout, failure)) return true;
                        return finish(failure);
                    }
                    if (threadStatus != O::TypeMismatch) return finish(threadStatus);
                    std::shared_ptr<FileObject> file;
                    const auto fileStatus = objects_.Reference(a1, 0x100000, file);
                    if (fileStatus != O::TypeMismatch) return finish(fileStatus ? fileStatus : NotSupported);
                }
                return finish(status);
            }
            if (eventChange) {
                // NtClearEvent has only one argument; RDX is unspecified.
                const auto previousOut = name == "NtClearEvent" ? 0 : a2;
                if (previousOut && !call.Access(previousOut, 4, 2)) return finish(AccessViolation);
                const uint32_t previous = event->signaled ? 1 : 0;
                event->signaled = name == "NtSetEvent";
                if (event->signaled && WakeThreads(event, !event->autoReset) && event->autoReset) event->signaled = false;
                if (previousOut) call.memory.Write(previousOut, previous);
                return finish(0);
            }
            if (a2 & 0xFF) return finish(NotSupported); // No APC delivery yet.
            int64_t timeout = 0;
            if (a3 && !call.Read(a3, timeout)) return finish(AccessViolation);
            if (event->signaled) { if (event->autoReset) event->signaled = false; return finish(0); }
            if (a3 && timeout == 0) return finish(0x102);
            uint32_t failure = 0;
            if (BlockThread(vm, event, a3 != 0, timeout, failure)) return true;
            return finish(failure);
        }
        if (fileOpen) {
            if (!call.Access(a1, 8, 2) || !call.Access(a4, 16, 2)) return finish(AccessViolation);
            if (a1 < a4 + 16 && a4 < a1 + 8) return finish(InvalidParameter);
            uint32_t attributes = 0, desired = 0; uint64_t root = 0; std::string path;
            auto status = call.Attributes(a3, attributes, root, path, true); if (status) return finish(status);
            if (path.empty()) return finish(0xC0000033);
            if (root) return finish(NotSupported);
            status = O::Normalize(ObjectKind::File, static_cast<uint32_t>(a2), desired); if (status) return finish(status);
            uint64_t arguments[7]{}; const unsigned count = name == "NtCreateFile" ? 7 : 2;
            for (unsigned i = 0; i < count; ++i) if (!call.Stack(i + 5, arguments[i])) return finish(AccessViolation);
            const auto share = name == "NtCreateFile" ? arguments[2] : arguments[0];
            const auto options = name == "NtCreateFile" ? arguments[4] : arguments[1];
            const auto disposition = name == "NtCreateFile" ? static_cast<uint32_t>(arguments[3]) : 1u;
            if (share || (options & ~uint64_t(0x60)) || (name == "NtCreateFile" && (arguments[0] || (arguments[1] & ~uint64_t(0x80)) || arguments[5] || arguments[6]))) return finish(NotSupported);
            if ((options & 0x20) && !(desired & 0x100000)) return finish(O::AccessDenied);
            if (disposition > 5) return finish(InvalidParameter);
            const bool exists = fs_.Files().find(path) != fs_.Files().end() && !fs_.IsBlocked(path);
            const bool truncate = disposition == 0 || disposition == 4 || (disposition == 5 && exists);
            if (truncate && !(desired & 2)) return finish(O::AccessDenied);
            if (exists && disposition == 2) return finish(0xC0000035); // OBJECT_NAME_COLLISION
            if (!objects_.HasRoom()) return finish(O::NoMemory);
            auto file = std::make_shared<FileObject>(fs_, 0, path);
            const auto result = fs_.Open(path, disposition != 1 && disposition != 4);
            if (!result.found) { call.memory.Write(a4, uint64_t(NameNotFound)); call.memory.Write(a4 + 8, uint64_t(0)); return finish(NameNotFound); }
            file->handle = result.handle;
            uint64_t handle = 0; status = objects_.Open(file, desired, attributes, handle);
            if (status) return finish(status);
            if (truncate) fs_.Seed(path);
            call.memory.Write(a1, handle); call.memory.Write(a4, uint64_t(0));
            call.memory.Write(a4 + 8, uint64_t(result.created ? 2 : truncate ? (disposition == 0 ? 0 : 3) : 1));
            report_.Add(ReportBuilder::Cat::File, result.created ? "create" : "open", path, name);
            return finish(0);
        }
        if (fileIo) {
            uint64_t args[5]{};
            for (unsigned i = 0; i < 5; ++i) if (!call.Stack(i + 5, args[i])) return finish(AccessViolation);
            if (!call.Access(args[0], 16, 2)) return finish(AccessViolation);
            const auto complete = [&](uint32_t status, uint64_t bytes = 0) {
                call.memory.Write(args[0], uint64_t(status)); call.memory.Write(args[0] + 8, bytes); return finish(status);
            };
            std::shared_ptr<FileObject> file;
            const bool write = name == "NtWriteFile";
            const auto status = objects_.Reference(a1, write ? 2u : 1u, file); if (status) return complete(status);
            if (a2 || a3 || a4 || args[4]) return complete(NotSupported);
            int64_t offset = -2;
            if (args[3] && !call.Read(args[3], offset)) return complete(AccessViolation);
            if (offset != -2) return complete(NotSupported);
            const auto bytes = static_cast<uint32_t>(args[2]);
            if (bytes > 16u * 1024 * 1024 || (write && (file->position > 64ull * 1024 * 1024 || bytes > 64ull * 1024 * 1024 - file->position))) return complete(O::NoMemory);
            if (!call.Access(args[1], bytes, write ? 1 : 2)) return complete(AccessViolation);
            auto* data = static_cast<uint8_t*>(call.memory.Data()) + args[1];
            if (write) {
                if (!fs_.Write(file->handle, data, bytes)) return complete(O::InvalidHandle);
                file->position += bytes; report_.Add(ReportBuilder::Cat::File, "write", file->path, std::to_string(bytes) + " bytes");
                return complete(0, bytes);
            }
            const auto read = fs_.Read(file->handle, data, bytes); file->position += read;
            report_.Add(ReportBuilder::Cat::File, "read", file->path, std::to_string(read) + " bytes");
            return complete(!read && bytes ? 0xC0000011 : 0, read);
        }
        if (name == "NtCreateSection") {
            if (!call.Access(a1, 8, 2)) return finish(AccessViolation);
            uint64_t args[3]{};
            for (unsigned i = 0; i < 3; ++i) if (!call.Stack(i + 5, args[i])) return finish(AccessViolation);
            uint32_t attributes = 0; uint64_t root = 0; std::string objectName;
            auto status = call.Attributes(a3, attributes, root, objectName); if (status) return finish(status);
            if (root || !objectName.empty()) return finish(NotSupported);
            const auto protection = static_cast<uint32_t>(args[0]);
            if (!GuestVirtualMemory::ValidProtection(protection) || protection == 1) return finish(GuestVirtualMemory::InvalidProtection);
            if (args[1] && args[1] != 0x1000000 && args[1] != 0x8000000) return finish(NotSupported);
            auto section = std::make_shared<SectionObject>(); section->image = args[1] == 0x1000000; section->protect = protection;
            int64_t maximum = 0;
            if (a4 && !call.Read(a4, maximum)) return finish(AccessViolation);
            if (a4 && maximum <= 0) return finish(InvalidParameter);
            if (args[2]) {
                status = objects_.Reference(args[2], 1u | ((!section->image && (GuestVirtualMemory::Access(protection) & 2)) ? 2u : 0u), section->file);
                if (status) return finish(status);
                const auto found = fs_.Files().find(section->file->path);
                if (found == fs_.Files().end()) return finish(O::InvalidHandle);
                section->size = section->image ? ImageSize(found->second) : found->second.size();
                if (!section->size) return finish(section->image ? 0xC000007B : 0xC000011E);
                if (!section->image && maximum) {
                    if (static_cast<uint64_t>(maximum) > section->size) return finish(NotSupported);
                    section->size = static_cast<uint64_t>(maximum);
                }
            } else {
                if (section->image || !maximum) return finish(InvalidParameter);
                section->size = static_cast<uint64_t>(maximum);
            }
            uint64_t handle = 0; status = objects_.Open(section, static_cast<uint32_t>(a2), attributes, handle);
            if (!status) call.memory.Write(a1, handle);
            return finish(status);
        }
        if (keyOpen) {
            if (!call.Access(a1, 8, 2)) return finish(AccessViolation);
            uint32_t attributes = 0; uint64_t root = 0; std::string path;
            auto status = call.Attributes(a3, attributes, root, path); if (status) return finish(status);
            if (path.empty()) return finish(0xC0000033);
            if (name == "NtOpenKeyEx") { if (a4) return finish(NotSupported); }
            uint64_t dispositionOut = 0;
            if (name == "NtCreateKey") {
                uint64_t className = 0, options = 0;
                if (!call.Stack(5, className) || !call.Stack(6, options) || !call.Stack(7, dispositionOut)) return finish(AccessViolation);
                if (a4 || className || options) return finish(NotSupported);
                if (dispositionOut && !call.Access(dispositionOut, 4, 2)) return finish(AccessViolation);
            }
            if (root) {
                std::shared_ptr<KeyObject> parent; status = objects_.Reference(root, name == "NtCreateKey" ? 4u : 0u, parent); if (status) return finish(status);
                if (path.front() == '\\') return finish(InvalidParameter);
                path = parent->path + "\\" + path;
            }
            path = LowerName(path);
            if (path.rfind("\\registry\\", 0) != 0) return finish(0xC0000033);
            if(hardware_&&name!="NtCreateKey"&&!registry_.contains(path))return finish(NameNotFound);
            if (dispositionOut && a1 < dispositionOut + 4 && dispositionOut < a1 + 8) return finish(InvalidParameter);
            auto key = std::make_shared<KeyObject>(path);
            bool created = false;
            if (name == "NtCreateKey") created = registry_.try_emplace(path).second;
            uint64_t handle = 0; status = objects_.Open(key, static_cast<uint32_t>(a2), attributes, handle);
            if (status && created) registry_.erase(path);
            if (!status) {
                // Preserve the existing permissive virtual namespace: opens may
                // expose an empty key. ACLs and full create dispositions are pending.
                call.memory.Write(a1, handle);
                if (dispositionOut) call.memory.Write(dispositionOut, uint32_t(created ? 1 : 2));
                report_.Add(ReportBuilder::Cat::Registry, "open-key", path, name);
            }
            return finish(status);
        }
        if (name == "NtQueryValueKey" || name == "NtSetValueKey") {
            const bool write = name == "NtSetValueKey";
            std::shared_ptr<KeyObject> key; auto status = objects_.Reference(a1, write ? 2u : 1u, key); if (status) return finish(status);
            std::string valueName; status = call.String(a2, valueName); if (status) return finish(status);
            valueName = LowerName(valueName);
            uint64_t fifth = 0, sixth = 0;
            if (!call.Stack(5, fifth) || !call.Stack(6, sixth)) return finish(AccessViolation);
            if (write) {
                if(hardware_&&guardian::hardware::RegistryKey(*hardware_,key->path.c_str()))return finish(0xC0000022);
                const auto size = static_cast<uint32_t>(sixth);
                if (a3) return finish(NotSupported);
                if (size > 1024 * 1024) return finish(O::NoMemory);
                if (!call.Access(fifth, size, 1)) return finish(AccessViolation);
                std::vector<uint8_t> bytes(size);
                if (size) std::memcpy(bytes.data(), static_cast<uint8_t*>(call.memory.Data()) + fifth, size);
                registry_[key->path][valueName] = {static_cast<uint32_t>(a4), std::move(bytes)};
                report_.Add(ReportBuilder::Cat::Registry, "set-value", key->path, valueName); return finish(0);
            }
            if (static_cast<uint32_t>(a3) != 2) return finish(0xC0000003);
            if (sixth && !call.Access(sixth, 4, 2)) return finish(AccessViolation);
            const auto foundKey = registry_.find(key->path);
            if (foundKey == registry_.end()) return finish(NameNotFound);
            const auto value = foundKey->second.find(valueName);
            if (value == foundKey->second.end()) return finish(NameNotFound);
            const auto size = static_cast<uint32_t>(value->second.bytes.size()), needed = size + 12;
            if (static_cast<uint32_t>(fifth) < needed) { if (sixth) call.memory.Write(sixth, needed); return finish(0x80000005); }
            if (!call.Access(a4, needed, 2)) return finish(AccessViolation);
            call.memory.Write(a4, uint32_t(0)); call.memory.Write(a4 + 4, value->second.type); call.memory.Write(a4 + 8, size);
            if (size) std::memcpy(static_cast<uint8_t*>(call.memory.Data()) + a4 + 12, value->second.bytes.data(), size);
            if (sixth) call.memory.Write(sixth, needed);
            report_.Add(ReportBuilder::Cat::Registry, "query-value", key->path, valueName); return finish(0);
        }
    } catch (const std::bad_alloc&) { return finish(O::NoMemory); }
    return false;
}
} }
