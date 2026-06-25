#include "logger.h"
#include "utils.h"

#include <iostream>
#include <sstream>

namespace Shadow {

// ─── ANSI escape codes ──────────────────────────────────────────────────────
namespace Clr {
    constexpr const char* RESET   = "\033[0m";
    constexpr const char* DIM     = "\033[2m";
    constexpr const char* RED     = "\033[31m";
    constexpr const char* GREEN   = "\033[32m";
    constexpr const char* YELLOW  = "\033[33m";
    constexpr const char* CYAN    = "\033[36m";
    constexpr const char* WHITE   = "\033[97m";
}

// ─── Singleton access ───────────────────────────────────────────────────────

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

Logger::~Logger() { shutdown(); }

// ─── Lifecycle ──────────────────────────────────────────────────────────────

void Logger::initialize(bool isDryRun) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) return;

    std::string path = Utils::getDesktopPath() + "\\shadow_simulation_log.txt";
    m_logFile.open(path, std::ios::out | std::ios::trunc);

    if (m_logFile.is_open()) {
        m_logFile
            << "================================================================================\n"
            << "  SHADOW - System Privacy & Cleanup Utility\n"
            << "  Log Generated : " << Utils::getCurrentDateTimeFormatted() << "\n"
            << "  Execution Mode: " << (isDryRun ? "DRY-RUN (Simulation)" : "LIVE") << "\n"
            << "================================================================================\n"
            << std::endl;
        m_initialized = true;
    }

    resetStats();
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) return;

    if (m_logFile.is_open()) {
        m_logFile
            << "\n"
            << "================================================================================\n"
            << "  Summary\n"
            << "  Targeted : " << m_targeted.load() << "\n"
            << "  Modified : " << m_modified.load() << "\n"
            << "  Skipped  : " << m_skipped.load()  << "\n"
            << "  Errors   : " << m_errors.load()   << "\n"
            << "================================================================================\n";
        m_logFile.close();
    }
    m_initialized = false;
}

void Logger::resetStats() {
    m_targeted = 0;
    m_modified = 0;
    m_skipped  = 0;
    m_errors   = 0;
    m_targetedPaths.clear();
    m_skippedItems.clear();
}

// ─── General log ────────────────────────────────────────────────────────────

void Logger::log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string entry = formatLogEntry(level, message);
    writeToConsole(level, entry);
    writeToFile(entry);
}

// ─── Action log (structured) ────────────────────────────────────────────────

void Logger::logAction(const std::string& targetPath,
                        ActionStatus       status,
                        const std::string& details) {
    std::lock_guard<std::mutex> lock(m_mutex);

    ++m_targeted;
    m_targetedPaths.push_back(targetPath);

    switch (status) {
        case ActionStatus::MODIFIED:
            ++m_modified;
            break;
        case ActionStatus::SKIPPED:
            ++m_skipped;
            m_skippedItems.emplace_back(targetPath, details);
            if (!details.empty()) ++m_errors;
            break;
        case ActionStatus::WOULD_MODIFY:
            break;
    }

    // Build log line
    std::ostringstream oss;
    oss << "[" << Utils::getTimestamp() << "] "
        << "[ACTION] " << targetPath
        << " -> " << statusTag(status);
    if (!details.empty()) oss << " (" << details << ")";

    std::string entry = oss.str();

    LogLevel displayLevel = (status == ActionStatus::SKIPPED)
                                ? LogLevel::WARNING
                                : LogLevel::ACTION;

    writeToConsole(displayLevel, entry);
    writeToFile(entry);
}

// ─── Output helpers ─────────────────────────────────────────────────────────

void Logger::writeToFile(const std::string& entry) {
    if (m_logFile.is_open()) {
        m_logFile << entry << "\n";
        m_logFile.flush();
    }
}

void Logger::writeToConsole(LogLevel level, const std::string& text) {
#ifdef SHADOW_DEBUG
    // Debug build: print everything.
    std::cout << consoleColor(level) << text << Clr::RESET << "\n";
#else
    // Release build: only module headers, warnings, errors, and summaries.
    switch (level) {
        case LogLevel::INFO:
        case LogLevel::WARNING:
        case LogLevel::ERR:
        case LogLevel::SUMMARY:
            std::cout << consoleColor(level) << text << Clr::RESET << "\n";
            break;
        case LogLevel::DEBUG_DETAIL:
        case LogLevel::ACTION:
        default:
            break;   // suppressed in Release
    }
#endif
}

std::string Logger::formatLogEntry(LogLevel level, const std::string& message) {
    std::ostringstream oss;
    oss << "[" << Utils::getTimestamp() << "] "
        << "[" << levelTag(level) << "] "
        << message;
    return oss.str();
}

const char* Logger::levelTag(LogLevel level) {
    switch (level) {
        case LogLevel::INFO:         return "INFO";
        case LogLevel::DEBUG_DETAIL: return "DEBUG";
        case LogLevel::WARNING:      return "WARNING";
        case LogLevel::ERR:          return "ERROR";
        case LogLevel::ACTION:       return "ACTION";
        case LogLevel::SUMMARY:      return "SUMMARY";
    }
    return "UNKNOWN";
}

const char* Logger::statusTag(ActionStatus status) {
    switch (status) {
        case ActionStatus::WOULD_MODIFY: return "Would Modify";
        case ActionStatus::MODIFIED:     return "Modified";
        case ActionStatus::SKIPPED:      return "Skipped";
    }
    return "Unknown";
}

const char* Logger::consoleColor(LogLevel level) {
    switch (level) {
        case LogLevel::INFO:         return Clr::WHITE;
        case LogLevel::DEBUG_DETAIL: return Clr::DIM;
        case LogLevel::WARNING:      return Clr::YELLOW;
        case LogLevel::ERR:          return Clr::RED;
        case LogLevel::ACTION:       return Clr::GREEN;
        case LogLevel::SUMMARY:      return Clr::CYAN;
    }
    return Clr::RESET;
}

// ─── AI Safety Prompt ───────────────────────────────────────────────────────

void Logger::generateAISafetyPrompt() {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::ostringstream oss;
    oss << "\n--- AI SAFETY ASSESSMENT REQUEST ---\n"
        << "Analyze the following list of system modifications and "
        << "file deletions target paths. Determine if executing these "
        << "deletions will cause operating system instability, permanent "
        << "boot failures, or critical application crashes on a standard "
        << "Windows 10/11 environment. Detail any high-risk paths:\n\n";

    for (const auto& p : m_targetedPaths)
        oss << "  " << p << "\n";

    oss << "\n--- END OF ASSESSMENT REQUEST ---\n";

    std::string block = oss.str();

    // Always write to the log file.
    if (m_logFile.is_open()) {
        m_logFile << block;
        m_logFile.flush();
    }

    // Always print to console (both Debug and Release).
    std::cout << Clr::CYAN << block << Clr::RESET << std::endl;
}

const std::vector<std::pair<std::string, std::string>>&
Logger::getSkippedItems() const {
    return m_skippedItems;
}

} // namespace Shadow

