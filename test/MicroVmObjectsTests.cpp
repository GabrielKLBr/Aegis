#include "../src/microvm/host/GuestObjects.h"
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>
using namespace av::microvm;
using O = GuestObjects;
namespace {
void check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
struct File : GuestObject {
    static constexpr ObjectKind Kind = ObjectKind::File;
    explicit File(int& count) : GuestObject(Kind), destroyed(count) {}
    ~File() override { ++destroyed; }
    int& destroyed; int position = 0;
};
struct Event : GuestObject {
    static constexpr ObjectKind Kind = ObjectKind::Event;
    Event() : GuestObject(Kind) {}
};
}
int main() {
    try {
        int destroyed = 0; O table;
        auto file = std::make_shared<File>(destroyed); std::weak_ptr<File> lifetime = file;
        uint64_t handle = 0, duplicate = 0;
        check(table.Open(file, 0xC0000000, 0, handle) == 0, "generic access not mapped");
        file.reset();
        O::BasicInformation info{};
        check(table.Query(handle, info) == 0 && info.grantedAccess == 0x12019F && info.handleCount == 1, "access mask or count wrong");
        check(table.Duplicate(handle, 1, 0, 0, duplicate) == 0 && duplicate != handle, "restricted duplicate failed");
        check(table.Query(duplicate, info) == 0 && info.grantedAccess == 1 && info.handleCount == 2, "duplicate gained rights or lost identity");
        std::shared_ptr<File> first, second;
        check(table.Reference(handle, 2, first) == 0 && table.Reference(duplicate, 1, second) == 0, "valid access denied");
        first->position = 27; check(second->position == 27, "duplicate copied object instead of referencing it");
        std::shared_ptr<Event> event;
        check(table.Reference(handle, 0, event) == O::TypeMismatch, "wrong type accepted");
        check(table.Reference(duplicate, 2, second) == O::AccessDenied, "read-only duplicate writable");
        uint64_t untouched = 0xABC;
        check(table.Duplicate(duplicate, 2, 0, 0, untouched) == O::AccessDenied && untouched == 0xABC, "duplicate escalated access or changed output");
        check(table.Duplicate(handle, 0, 0, 1, untouched) == O::NotSupported && table.Count() == 2, "unsupported close-source changed table");
        check(table.Close(handle) == 0 && table.Close(handle) == O::InvalidHandle, "double close accepted");
        check(table.Reference(handle, 0, first) == O::InvalidHandle, "closed handle remained valid");
        first.reset(); second.reset();
        check(table.Close(duplicate) == 0 && lifetime.expired() && destroyed == 1, "object survived its last reference or destroyed too early");

        auto owned = std::make_shared<File>(destroyed); uint64_t held = 0;
        check(table.Open(owned, 1, O::ProtectClose | O::Inherit, held) == 0, "protected handle creation failed");
        uint64_t same = 0;
        check(table.Duplicate(held, 0xFFFFFFFF, 0xFFFFFFFF, O::SameAccess | O::SameAttributes, same) == 0, "same-access/attributes flags ignored");
        check(table.Query(same, info) == 0 && info.attributes == 3 && info.grantedAccess == 1, "attributes not per handle");
        check(table.Close(held) == O::NotClosable && table.SetFlags(held, false, false) == 0 && table.Close(held) == 0, "protect-close not enforced");
        check(table.Close(same) == O::NotClosable, "changing source flags changed the duplicate");
        check(table.SetFlags(same, false, false) == 0 && table.Close(same) == 0 && destroyed == 1, "external object reference not retained");
        owned.reset(); check(destroyed == 2, "last external reference not released");

        O many(1200); std::set<uint64_t> unique; std::vector<uint64_t> old;
        for (unsigned i = 0; i < 1000; ++i) {
            uint64_t value = 0;
            std::shared_ptr<GuestObject> object = i % 2 ? std::static_pointer_cast<GuestObject>(std::make_shared<Event>()) : std::static_pointer_cast<GuestObject>(std::make_shared<File>(destroyed));
            check(many.Open(object, 0, 0, value) == 0 && unique.insert(value).second, "handle namespaces collided");
            old.push_back(value);
        }
        for (auto value : old) check(many.Close(value) == 0, "mass close failed");
        uint64_t later = 0;
        check(many.Open(std::make_shared<Event>(), 0, 0, later) == 0 && !unique.count(later), "stale handle aliases a new object");
        for (auto value : old) check(many.Query(value, info) == O::InvalidHandle, "stale handle revived");
        O bounded(1); uint64_t one = 0;
        auto item = std::make_shared<Event>();
        check(bounded.Open(item, 0, 0, one) == 0, "bounded open failed");
        untouched = 0xABC;
        check(bounded.Duplicate(one, 0, 0, 0, untouched) == O::NoMemory && untouched == 0xABC && bounded.Count() == 1, "capacity failure leaked a handle");
        check(bounded.Close(one) == 0 && bounded.Open(item, 0, 0, one) == 0, "capacity not recovered after close");
        uint32_t mask = 123;
        check(O::Normalize(ObjectKind::File, 0x02000000, mask) == O::NotSupported && mask == 123, "MAXIMUM_ALLOWED was silently granted");
        std::cout << "PASS: unique typed handles, generic rights, restricted duplicates, no escalation, per-handle flags, stale handles, capacity and last-reference destruction\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
