#pragma once
#include "guardian/ScalableStorage.h"
namespace guardian::scalable {
// Guest-owned flat file namespace and COW extents. The host sees raw blocks only.
// Workspaces are supplied explicitly; neither heap nor host pointers are required.
class Volume {
  public:
    bool Configure(void *, Block, Available, Metadata *, Metadata *, uint8_t *, uint64_t, uint64_t) noexcept;
    Status Mount(bool formatIfZero, bool recover = true) noexcept;
    Status Create(const char *) noexcept;
    Status Remove(const char *) noexcept;
    Status Write(const char *, uint64_t, const uint8_t *, uint32_t, uint32_t &) noexcept;
    Status Read(const char *, uint64_t, uint8_t *, uint32_t, uint32_t &) noexcept;
    Status Truncate(const char *, uint64_t) noexcept;
    Status Size(const char *, uint64_t &) const noexcept;
    Space QuerySpace() const noexcept;
    uint64_t Sequence() const noexcept { return mounted_ ? current_->header.sequence : 0; }

  private:
    static bool Name(const char *, char *) noexcept;
    static bool Equal(const char *, const char *) noexcept;
    static uint64_t Digest(const Metadata &) noexcept;
    bool Validate(const Metadata &) const noexcept;
    Status ReadSlot(uint32_t, Metadata &, bool &) noexcept;
    void Begin(Node * = nullptr) noexcept;
    void Rollback() noexcept;
    bool Intent(bool write, bool clear = false) noexcept;
    void Mark(const Metadata &) const noexcept;
    Status Commit() noexcept;
    Node *Find(const char *) const noexcept;
    uint32_t Allocate() const noexcept;
    bool Referenced(uint32_t, const Metadata &) const noexcept;
    Status Reclaim() noexcept;

    void *context_ = nullptr;
    Block block_ = nullptr;
    Available available_ = nullptr;
    Metadata *current_ = nullptr;
    Metadata *previous_ = nullptr;
    uint8_t *scratch_ = nullptr;
    uint64_t capacity_ = 0, quota_ = 0;
    uint32_t active_ = 0;
    bool mounted_ = false;
    Node undo_{};
    Header undoHeader_{};
    uint32_t undoIndex_ = UINT32_MAX;
    bool transaction_ = false;
    mutable uint32_t bitmap_[uint32_t(MaxCapacity / ChunkBytes) / 32]{};
    uint32_t retired_[Extents]{}, retiredCount_ = 0;
};
} // namespace guardian::scalable
