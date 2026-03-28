#pragma once
/**
 * @file memory_pool.h
 * @brief High-performance, allocation-free pool allocators for hot-path objects.
 *
 * Two allocator types:
 *   - PoolAllocator<T, N>   — fixed-size slab: N objects pre-allocated.
 *   - LinearAllocator       — arena bump allocator for per-frame scratch memory.
 *
 * Design constraint: zero heap allocation on the per-draw-call path after init.
 */

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace mvrvb {

// ── Slab pool allocator ──────────────────────────────────────────────────────

/**
 * @brief Lock-free pool of N pre-allocated T objects.
 *
 * Acquire returns a pointer to a free slot; Release returns it.
 * Thread-safe via atomic CAS on the free-list stack.
 *
 * @tparam T  Object type (must be default-constructible).
 * @tparam N  Pool capacity (power of two recommended).
 */
template<typename T, size_t N>
class PoolAllocator {
public:
    static_assert(N > 0, "Pool size must be > 0");

    PoolAllocator() {
        // Build the free list.
        for (size_t i = 0; i < N; ++i) {
            m_next[i] = (i + 1 < N) ? static_cast<int32_t>(i + 1) : -1;
        }
        m_head.store(0, std::memory_order_relaxed);
    }

    /// Acquire a slot. Returns nullptr if pool is exhausted.
    [[nodiscard]] T* acquire() noexcept {
        int32_t head = m_head.load(std::memory_order_acquire);
        while (head != -1) {
            int32_t next = m_next[head];
            if (m_head.compare_exchange_weak(head, next,
                    std::memory_order_release, std::memory_order_acquire)) {
                return new (&m_storage[head]) T();
            }
        }
        return nullptr; // Pool exhausted.
    }

    /// Release a previously acquired slot.
    void release(T* ptr) noexcept {
        if (!ptr) return;
        const ptrdiff_t idx = reinterpret_cast<aligned_storage_t*>(ptr) - m_storage.data();
        assert(idx >= 0 && static_cast<size_t>(idx) < N);
        ptr->~T();
        int32_t head = m_head.load(std::memory_order_acquire);
        do {
            m_next[idx] = head;
        } while (!m_head.compare_exchange_weak(head, static_cast<int32_t>(idx),
                     std::memory_order_release, std::memory_order_acquire));
    }

    [[nodiscard]] constexpr size_t capacity() const noexcept { return N; }

private:
    using aligned_storage_t = std::aligned_storage_t<sizeof(T), alignof(T)>;
    std::array<aligned_storage_t, N> m_storage;
    std::array<int32_t, N>           m_next;
    std::atomic<int32_t>             m_head{0};
};

// ── Linear (arena) allocator ─────────────────────────────────────────────────

/**
 * @brief Per-frame scratch arena. Reset at the start of each frame.
 *
 * Single-threaded use intended (one per frame/recording thread).
 * No destructor support — allocate POD or manually managed types only.
 *
 * @param Capacity  Maximum scratch bytes per frame.
 */
template<size_t Capacity>
class LinearAllocator {
public:
    /// Allocate sz bytes aligned to align. Returns nullptr on overflow.
    [[nodiscard]] void* alloc(size_t sz, size_t align = alignof(std::max_align_t)) noexcept {
        uintptr_t cur = reinterpret_cast<uintptr_t>(m_buf + m_offset);
        uintptr_t aligned = (cur + align - 1) & ~(align - 1);
        size_t newOffset = (aligned - reinterpret_cast<uintptr_t>(m_buf)) + sz;
        if (newOffset > Capacity) return nullptr;
        m_offset = newOffset;
        return reinterpret_cast<void*>(aligned);
    }

    template<typename T, typename... Args>
    [[nodiscard]] T* allocT(Args&&... args) noexcept {
        void* p = alloc(sizeof(T), alignof(T));
        if (!p) return nullptr;
        return new (p) T(std::forward<Args>(args)...);
    }

    /// Reset: all previous allocations are invalid after this call.
    void reset() noexcept { m_offset = 0; }

    [[nodiscard]] size_t used()      const noexcept { return m_offset; }
    [[nodiscard]] size_t remaining() const noexcept { return Capacity - m_offset; }

private:
    alignas(64) uint8_t m_buf[Capacity]{};
    size_t m_offset{0};
};

/// Per-frame scratch allocator — 1 MB per frame thread.
using FrameAllocator = LinearAllocator<1 * 1024 * 1024>;

} // namespace mvrvb
