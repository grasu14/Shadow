#pragma once
#include <atomic>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace Shadow {

// ─── Log severity ────────────────────────────────────────────────────────────
enum class LogLevel {
    INFO,           // Module headers, status messages
    DEBUG_DETAIL,   // Verbose detail  (visible only in Debug builds)
    WARNING,        // Non-fatal issues
    ERR,            // Hard errors     (named ERR to avoid Windows macro clash)
    ACTION,         // Individual file/registry operations
    SUMMARY         // Final execution summary block
};

// ─── Operation outcome ──────────────────────────────────────────────────────
enum class ActionStatus {
    WOULD_MODIFY,   // Dry-run: action logged but not executed
    MODIFIED,       // Live:    action completed successfully
    SKIPPED         // Error:   action could not be performed
};

// ─── Logger (singleton) ─────────────────────────────────────────────────────
class Logger {
public:
    static Logger& instance();

    /// Opens shadow_simulation_log.txt on the user's desktop and writes
    /// the file header.  Must be called before any log/logAction calls.
    void initialize(bool isDryRun, bool isPanicMode = false);

    /// Writes the summary footer and closes the log file.
    void shutdown();

    /// General-purpose log line (respects Debug/Release verbosity).
    void log(LogLevel level, const std::string& message);

    /// Structured action entry — tracks a targeted path, its outcome,
    /// and optional detail text.  Always written to the log file;
    /// console output follows build-mode rules.
    void logAction(const std::string& targetPath,
                   ActionStatus       status,
                   const std::string& details = "");

    // ── Statistics ───────────────────────────────────────────────────────────
    int getTargetedCount() const { return m_targeted.load(); }
    int getModifiedCount() const { return m_modified.load(); }
    int getSkippedCount()  const { return m_skipped.load();  }
    int getErrorCount()    const { return m_errors.load();   }

    void resetStats();

    /// After a dry-run, appends the AI safety assessment prompt (containing
    /// every targeted path) to the log file and prints it to the console.
    void generateAISafetyPrompt();

    /// Returns the list of {path, reason} pairs for items that were skipped.
    const std::vector<std::pair<std::string, std::string>>& getSkippedItems() const;

private:
    Logger()  = default;
    ~Logger();
    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

    void        writeToFile(const std::string& entry);
    void        writeToConsole(LogLevel level, const std::string& text);
    std::string formatLogEntry(LogLevel level, const std::string& message);
    const char* levelTag(LogLevel level);
    const char* statusTag(ActionStatus status);
    const char* consoleColor(LogLevel level);

    std::ofstream       m_logFile;
    std::mutex          m_mutex;
    bool                m_initialized = false;

    std::atomic<int>    m_targeted{0};
    std::atomic<int>    m_modified{0};
    std::atomic<int>    m_skipped{0};
    std::atomic<int>    m_errors{0};

    std::vector<std::string>                          m_targetedPaths;
    std::vector<std::pair<std::string, std::string>>  m_skippedItems;
};

} // namespace Shadow
