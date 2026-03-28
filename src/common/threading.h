#pragma once
/**
 * @file threading.h
 * @brief Thread utilities: real-time priority, thread-local pools,
 *        spin locks, and work-stealing queues for Metal command encoding.
 *
 * Threading model enforced by this layer:
 *   - MTLDevice        → thread-safe (shared across all threads)
 *   - MTLCommandQueue  → thread-safe (one shared queue per device)
 *   - MTLCommandBuffer → NOT thread-safe (per-recording-thread)
 *   - Encoders         → NOT thread-safe (must be used on the thread
 *                         that created them and finished before commit)
 *
 * CommandBufferPool allocates one MTLCommandBuffer per thread via
 * thread_local storage, satisfying Metal's threading requirements.
 */

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <string>
#include <thread>
#include <vector>

namespace mvrvb {

// ── Real-time thread priority ────────────────────────────────────────────────

/// Set the calling thread to macOS real-time scheduling (THREAD_TIME_CONSTRAINT_POLICY).
/// Used for the async timewarp thread to guarantee <2ms completion.
/// @param periodNs    Expected period in nanoseconds (e.g. 11'111'111 for 90 Hz).
/// @param computeNs   Max CPU time per period (e.g. 2'000'000 for 2 ms).
/// @param constrainedMs If true, the OS will preempt if constraint is exceeded.
bool setRealtimePriority(uint32_t periodNs, uint32_t computeNs, bool constrainedMs = true) noexcept;

/// Set the calling thread to a named, QOS_CLASS_USER_INTERACTIVE background thread.
bool setHighPriority(const char* name) noexcept;

/// Set current thread name (visible in Instruments).
void setThreadName(const char* name) noexcept;

// ── Spin lock ────────────────────────────────────────────────────────────────

/// Lightweight spinlock for very short critical sections (< a few hundred ns).
/// Falls back to std::mutex semantics if contention is detected.
class SpinLock {
public:
    void lock() noexcept {
        for (int spin = 0; !tryLock(); ++spin) {
            if (spin > 16) { // Exponential backoff
                std::this_thread::yield();
                spin = 0;
            } else {
                __asm__ __volatile__("pause" ::: "memory");
            }
        }
    }
    void unlock() noexcept { m_flag.clear(std::memory_order_release); }
    bool tryLock() noexcept { return !m_flag.test_and_set(std::memory_order_acquire); }
private:
    std::atomic_flag m_flag = ATOMIC_FLAG_INIT;
};

// ── Lock-free ring buffer (SPSC) ─────────────────────────────────────────────

/// Single-Producer Single-Consumer ring buffer — zero allocation on hot path.
template<typename T, size_t Capacity>
class RingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
public:
    bool push(T&& value) noexcept {
        const size_t head = m_head.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & (Capacity - 1);
        if (next == m_tail.load(std::memory_order_acquire)) return false; // Full
        m_data[head] = std::move(value);
        m_head.store(next, std::memory_order_release);
        return true;
    }
    bool pop(T& out) noexcept {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire)) return false; // Empty
        out = std::move(m_data[tail]);
        m_tail.store((tail + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }
    [[nodiscard]] bool empty() const noexcept {
        return m_head.load(std::memory_order_acquire) ==
               m_tail.load(std::memory_order_acquire);
    }
private:
    alignas(64) std::atomic<size_t> m_head{0};
    alignas(64) std::atomic<size_t> m_tail{0};
    T m_data[Capacity]{};
};

// ── Thread pool ──────────────────────────────────────────────────────────────

/// Fixed-size thread pool used for parallel shader compilation and frame encoding.
class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads, const char* namePrefix = "mvrvb");
    ~ThreadPool();

    /// Submit a task. Returns immediately; task runs on a pool thread.
    void submit(std::function<void()> task);

    /// Block until all submitted tasks are complete.
    void waitIdle();

    [[nodiscard]] size_t threadCount() const noexcept { return m_threads.size(); }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::vector<std::thread> m_threads;
};

} // namespace mvrvb
