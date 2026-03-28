#include "logging.h"
#include <cstdlib>
#include <ctime>
#include <unistd.h>

namespace mvrvb {

Logger& Logger::instance() noexcept {
    static Logger s;
    return s;
}

Logger::Logger() {
    // Honour MVRVB_LOG_LEVEL environment variable.
    if (const char* env = std::getenv("MVRVB_LOG_LEVEL")) {
        if      (!std::strcmp(env, "trace")) m_level.store(LogLevel::Trace);
        else if (!std::strcmp(env, "debug")) m_level.store(LogLevel::Debug);
        else if (!std::strcmp(env, "info"))  m_level.store(LogLevel::Info);
        else if (!std::strcmp(env, "warn"))  m_level.store(LogLevel::Warn);
        else if (!std::strcmp(env, "error")) m_level.store(LogLevel::Error);
        else if (!std::strcmp(env, "off"))   m_level.store(LogLevel::Off);
    }
    // Honour MVRVB_LOG_FILE environment variable.
    if (const char* envFile = std::getenv("MVRVB_LOG_FILE")) {
        setOutputFile(envFile);
    }
}

Logger::~Logger() {
    if (m_ownsFile && m_out) std::fclose(m_out);
}

void Logger::setOutputFile(const char* path) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_ownsFile && m_out) { std::fclose(m_out); }
    if (!path) {
        m_out = stderr; m_ownsFile = false; return;
    }
    m_out = std::fopen(path, "a");
    if (!m_out) { m_out = stderr; m_ownsFile = false; return; }
    m_ownsFile = true;
}

void Logger::log(LogLevel lvl, const char* file, int line,
                 const char* func, std::string_view msg) noexcept {
    // Trim full path to filename only.
    const char* basename = file;
    for (const char* p = file; *p; ++p) if (*p == '/') basename = p + 1;

    // Timestamp.
    struct timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_info{};
    localtime_r(&ts.tv_sec, &tm_info);

    // Use colors if writing to a terminal.
    bool useColor = m_out == stderr && isatty(fileno(stderr));

    char timebuf[32];
    std::strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &tm_info);

    std::lock_guard<std::mutex> lk(m_mutex);
    if (useColor) {
        std::fprintf(m_out, "%s%s.%03ld [%s] (%s:%d) %s\033[0m\n",
                     levelColor(lvl), timebuf, ts.tv_nsec / 1'000'000L,
                     levelName(lvl), basename, line, std::string(msg).c_str());
    } else {
        std::fprintf(m_out, "%s.%03ld [%s] (%s:%d) %s\n",
                     timebuf, ts.tv_nsec / 1'000'000L,
                     levelName(lvl), basename, line, std::string(msg).c_str());
    }
    std::fflush(m_out);
}

const char* Logger::levelName(LogLevel l) noexcept {
    switch (l) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        default:              return "?????";
    }
}

const char* Logger::levelColor(LogLevel l) noexcept {
    switch (l) {
        case LogLevel::Trace: return "\033[37m";     // white
        case LogLevel::Debug: return "\033[36m";     // cyan
        case LogLevel::Info:  return "\033[32m";     // green
        case LogLevel::Warn:  return "\033[33m";     // yellow
        case LogLevel::Error: return "\033[31m";     // red
        case LogLevel::Fatal: return "\033[1;31m";   // bold red
        default:              return "";
    }
}

} // namespace mvrvb
