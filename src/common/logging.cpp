#include "logging.h"
#include <cstdlib>
#include <ctime>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace mvrvb {
namespace {

bool isTerminal(FILE* stream) {
#if defined(_WIN32)
    return _isatty(_fileno(stream)) != 0;
#else
    return isatty(fileno(stream)) != 0;
#endif
}

std::string getEnvValue(const char* key) {
#if defined(_WIN32)
    char* buffer = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buffer, &len, key) != 0 || !buffer) {
        return {};
    }
    std::string value(buffer);
    std::free(buffer);
    return value;
#else
    if (const char* value = std::getenv(key)) {
        return value;
    }
    return {};
#endif
}

FILE* openAppendFile(const char* path) {
#if defined(_WIN32)
    FILE* file = nullptr;
    if (fopen_s(&file, path, "a") != 0) {
        return nullptr;
    }
    return file;
#else
    return std::fopen(path, "a");
#endif
}

void localTime(std::time_t seconds, std::tm* out) {
#if defined(_WIN32)
    localtime_s(out, &seconds);
#else
    localtime_r(&seconds, out);
#endif
}

}  // namespace

Logger& Logger::instance() noexcept {
    static Logger s;
    return s;
}

Logger::Logger() {
    // Honour MVRVB_LOG_LEVEL environment variable.
    const std::string env = getEnvValue("MVRVB_LOG_LEVEL");
    if (!env.empty()) {
        if      (env == "trace") m_level.store(LogLevel::Trace);
        else if (env == "debug") m_level.store(LogLevel::Debug);
        else if (env == "info")  m_level.store(LogLevel::Info);
        else if (env == "warn")  m_level.store(LogLevel::Warn);
        else if (env == "error") m_level.store(LogLevel::Error);
        else if (env == "off")   m_level.store(LogLevel::Off);
    }
    // Honour MVRVB_LOG_FILE environment variable.
    const std::string envFile = getEnvValue("MVRVB_LOG_FILE");
    if (!envFile.empty()) {
        setOutputFile(envFile.c_str());
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
    m_out = openAppendFile(path);
    if (!m_out) { m_out = stderr; m_ownsFile = false; return; }
    m_ownsFile = true;
}

void Logger::log(LogLevel lvl, const char* file, int line,
                 const char* func, std::string_view msg) noexcept {
    // Trim full path to filename only.
    const char* basename = file;
    for (const char* p = file; *p; ++p) {
        if (*p == '/' || *p == '\\') basename = p + 1;
    }

    // Timestamp.
    struct tm tm_info{};
    const auto now = std::chrono::system_clock::now();
    const auto epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch());
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    const long millis = static_cast<long>(epochMs.count() % 1000);
    localTime(seconds, &tm_info);

    // Use colors if writing to a terminal.
    const bool useColor = m_out == stderr && isTerminal(stderr);

    char timebuf[32];
    std::strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &tm_info);

    std::lock_guard<std::mutex> lk(m_mutex);
    if (useColor) {
        std::fprintf(m_out, "%s%s.%03ld [%s] (%s:%d) %s\033[0m\n",
                     levelColor(lvl), timebuf, millis,
                     levelName(lvl), basename, line, std::string(msg).c_str());
    } else {
        std::fprintf(m_out, "%s.%03ld [%s] (%s:%d) %s\n",
                     timebuf, millis,
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
