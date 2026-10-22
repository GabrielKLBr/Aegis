// ComPtr.h - a tiny, dependency-free COM smart pointer (portable MSVC/MinGW).
#pragma once

template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(const ComPtr& o) : p_(o.p_) { if (p_) p_->AddRef(); }
    ComPtr(ComPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ~ComPtr() { if (p_) p_->Release(); }

    ComPtr& operator=(const ComPtr& o) {
        if (this != &o) {
            if (o.p_) o.p_->AddRef();
            if (p_) p_->Release();
            p_ = o.p_;
        }
        return *this;
    }
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) {
            if (p_) p_->Release();
            p_ = o.p_;
            o.p_ = nullptr;
        }
        return *this;
    }

    T*  Get() const { return p_; }
    T*  operator->() const { return p_; }
    T** GetAddressOf() { return &p_; }
    T** ReleaseAndGetAddressOf() { Reset(); return &p_; }
    void Reset() { if (p_) { p_->Release(); p_ = nullptr; } }
    explicit operator bool() const { return p_ != nullptr; }

private:
    T* p_ = nullptr;
};
