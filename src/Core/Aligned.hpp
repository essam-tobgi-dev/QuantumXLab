#pragma once
// Spec 04 §7 — 64-byte aligned allocation for state vectors and kernels.
#include <cstddef>
#include <cstdlib>
#include <new>
#include <vector>
namespace qlab::core {
template <class T, std::size_t Align = 64> struct AlignedAllocator {
    using value_type = T;
    static constexpr std::align_val_t kAlign{Align};
    AlignedAllocator() = default;
    template <class U> AlignedAllocator(const AlignedAllocator<U, Align>&) noexcept {}
    T* allocate(std::size_t n) {
        if (n == 0)
            return nullptr;
        void* p = ::operator new(n * sizeof(T), kAlign, std::nothrow);
        if (!p)
            throw std::bad_alloc();
        return static_cast<T*>(p);
    }
    void deallocate(T* p, std::size_t) noexcept { ::operator delete(p, kAlign); }
    template <class U> struct rebind {
        using other = AlignedAllocator<U, Align>;
    };
    bool operator==(const AlignedAllocator&) const { return true; }
};
template <class T> using aligned_vector = std::vector<T, AlignedAllocator<T>>;
} // namespace qlab::core
