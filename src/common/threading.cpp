#include "threading.h"
#include "logging.h"

#include <condition_variable>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <queue>
#include <sys/qos.h>

namespace mvrvb {

// ── Real-time priority ───────────────────────────────────────────────────────

bool setRealtimePriority(uint32_t periodNs, uint32_t computeNs, bool constrained) noexcept {
    // Convert ns to absolute Mach time units.
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    const double ns_per_abs = static_cast<double>(tb.numer) / static_cast<double>(tb.denom);

    thread_time_constraint_policy_data_t policy{};
    policy.period      = static_cast<uint32_t>(periodNs  / ns_per_abs);
    policy.computation = static_cast<uint32_t>(computeNs / ns_per_abs);
    policy.constraint  = policy.computation * 2;
    policy.preemptible = constrained ? TRUE : FALSE;

    kern_return_t kr = thread_policy_set(
        mach_thread_self(),
        THREAD_TIME_CONSTRAINT_POLICY,
        reinterpret_cast<thread_policy_t>(&policy),
        THREAD_TIME_CONSTRAINT_POLICY_COUNT);

    if (kr != KERN_SUCCESS) {
        MVRVB_LOG_WARN("setRealtimePriority failed: kern_return=%d", kr);
        return false;
    }
    MVRVB_LOG_DEBUG("Thread promoted to real-time: period=%uns compute=%uns", periodNs, computeNs);
    return true;
}

bool setHighPriority(const char* name) noexcept {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    setThreadName(name);
    return true;
}

void setThreadName(const char* name) noexcept {
    pthread_setname_np(name);
}

// ── ThreadPool ───────────────────────────────────────────────────────────────

struct ThreadPool::Impl {
    std::mutex              mutex;
    std::condition_variable cv;
    std::condition_variable cvIdle;
    std::queue<std::function<void()>> tasks;
    size_t                  activeTasks{0};
    bool                    stopping{false};
};

ThreadPool::ThreadPool(size_t numThreads, const char* namePrefix) {
    m_impl = std::make_unique<Impl>();

    m_threads.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        m_threads.emplace_back([this, i, namePrefix] {
            // Name this thread for Instruments.
            char nameBuf[32];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s-%zu", namePrefix, i);
            setThreadName(nameBuf);
            setHighPriority(nameBuf);

            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lk(m_impl->mutex);
                    m_impl->cv.wait(lk, [this] {
                        return m_impl->stopping || !m_impl->tasks.empty();
                    });
                    if (m_impl->stopping && m_impl->tasks.empty()) return;
                    task = std::move(m_impl->tasks.front());
                    m_impl->tasks.pop();
                    ++m_impl->activeTasks;
                }
                task();
                {
                    std::lock_guard<std::mutex> lk(m_impl->mutex);
                    --m_impl->activeTasks;
                }
                m_impl->cvIdle.notify_all();
            }
        });
    }
    MVRVB_LOG_DEBUG("ThreadPool '%s' created with %zu threads", namePrefix, numThreads);
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lk(m_impl->mutex);
        m_impl->stopping = true;
    }
    m_impl->cv.notify_all();
    for (auto& t : m_threads) if (t.joinable()) t.join();
}

void ThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lk(m_impl->mutex);
        m_impl->tasks.push(std::move(task));
    }
    m_impl->cv.notify_one();
}

void ThreadPool::waitIdle() {
    std::unique_lock<std::mutex> lk(m_impl->mutex);
    m_impl->cvIdle.wait(lk, [this] {
        return m_impl->tasks.empty() && m_impl->activeTasks == 0;
    });
}

} // namespace mvrvb
