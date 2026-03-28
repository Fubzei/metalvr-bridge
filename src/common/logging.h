#pragma once
/**
 * @file logging.h
 * @brief Thread-safe, levelled logging subsystem for MetalVR Bridge.
 *
 * Usage:
 *   MVRVB_LOG_DEBUG("vkCreateDevice called with {} queues", queueCount);
 *   MVRVB_LOG_INFO("Shader cache hit for hash {:016x}", hash);
 *   MVRVB_LOG_WARN("Format {} not natively supported, using fallback", fmt);
 *   MVRVB_LOG_ERROR("MTLDevice creation failed: {}", err);
 */

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

namespace mvrvb {

/// Log verbosity levels (numerically ordered).
enum class LogLevel : int {
    Trace   = 0,
    Debug   = 1,
    Info    = 2,
    Warn    = 3,
    Error   = 4,
    Fatal   = 5,
    Off     = 6,
};

/// Singleton logger. Thread-safe.
class Logger {
public:
    static Logger& instance() noexcept;

    /// Set minimum level; messages below this are discarded.
    void setLevel(LogLevel level) noexcept { m_level.store(level, std::memory_order_relaxed); }
    [[nodiscard]] LogLevel level() const noexcept { return m_level.load(std::memory_order_relaxed); }

    /// Redirect output to a file path (nullptr → stderr).
    void setOutputFile(const char* path);

    /// Core log function. Prefer the macros below.
    void log(LogLevel level, const char* file, int line, const char* func, std::string_view msg) noexcept;

private:
    Logger();
    ~Logger();

    std::atomic<LogLevel> m_level{LogLevel::Debug};
    std::mutex            m_mutex;
    FILE*                 m_out{stderr};
    bool                  m_ownsFile{false};

    static const char* levelName(LogLevel l) noexcept;
    static const char* levelColor(LogLevel l) noexcept;
};

// ── Compile-time format helper (simple sprintf wrapper) ─────────────────────
namespace detail {

template<typename... Args>
std::string format(const char* fmt, Args&&... args) {
    // Fast path for no-arg messages
    if constexpr (sizeof...(args) == 0) {
        return std::string(fmt);
    } else {
        // Use snprintf for zero-allocation path in hot code
        char buf[1024];
        int n = std::snprintf(buf, sizeof(buf), fmt, std::forward<Args>(args)...);
        if (n < 0) return "<format error>";
        if (static_cast<size_t>(n) < sizeof(buf)) return std::string(buf, n);
        // Heap fallback for long messages
        std::string s(n + 1, '\0');
        std::snprintf(s.data(), s.size(), fmt, std::forward<Args>(args)...);
        s.resize(n);
        return s;
    }
}

} // namespace detail

} // namespace mvrvb

// ── Convenience macros ───────────────────────────────────────────────────────
#define MVRVB_LOG(level, fmt, ...)                                              \
    do {                                                                         \
        auto& _lg = ::mvrvb::Logger::instance();                                 \
        if (static_cast<int>(level) >= static_cast<int>(_lg.level())) {         \
            _lg.log(level, __FILE__, __LINE__, __func__,                         \
                    ::mvrvb::detail::format(fmt, ##__VA_ARGS__));                \
        }                                                                         \
    } while(0)

#define MVRVB_LOG_TRACE(fmt, ...) MVRVB_LOG(::mvrvb::LogLevel::Trace, fmt, ##__VA_ARGS__)
#define MVRVB_LOG_DEBUG(fmt, ...) MVRVB_LOG(::mvrvb::LogLevel::Debug, fmt, ##__VA_ARGS__)
#define MVRVB_LOG_INFO(fmt, ...)  MVRVB_LOG(::mvrvb::LogLevel::Info,  fmt, ##__VA_ARGS__)
#define MVRVB_LOG_WARN(fmt, ...)  MVRVB_LOG(::mvrvb::LogLevel::Warn,  fmt, ##__VA_ARGS__)
#define MVRVB_LOG_ERROR(fmt, ...) MVRVB_LOG(::mvrvb::LogLevel::Error, fmt, ##__VA_ARGS__)
#define MVRVB_LOG_FATAL(fmt, ...) MVRVB_LOG(::mvrvb::LogLevel::Fatal, fmt, ##__VA_ARGS__)

/// Assert with logging and abort.
#define MVRVB_ASSERT(cond, fmt, ...)                                             \
    do {                                                                         \
        if (!(cond)) {                                                           \
            MVRVB_LOG_FATAL("Assertion failed: " #cond " — " fmt, ##__VA_ARGS__);\
            std::abort();                                                        \
        }                                                                        \
    } while(0)
