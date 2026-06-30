#ifndef GUI_H
#define GUI_H

#include "engine.h"
#include "logger.h"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

namespace Shadow {

struct LogEntry {
    LogLevel level;
    std::string text;
};

class GUI {
public:
    GUI(Engine& engine);
    ~GUI();

    void render();
    static void applyTheme();
    static void addLog(LogLevel level, const std::string& log);

private:
    Engine& m_engine;
    std::atomic<bool> m_isExecuting{false};
    void renderDashboardTab();
    void renderTargetsTab();
    void renderPanicTab();
    void renderChangelogTab();
    void renderConsole();

    // Updater state
    bool m_updateAvailable = false;
    std::string m_newVersion = "";
    std::string m_downloadUrl = "";
    bool m_isDownloadingUpdate = false;
    bool m_updateChecked = false;

    // Console state
    static std::vector<LogEntry> s_logs;
    static std::mutex s_logsMutex;
    static bool s_autoScroll;
    
    // Target state
    std::string m_targetsText;
    bool m_targetsLoaded = false;
    
    void loadTargets();
    void saveTargets();
};

} // namespace Shadow

#endif // GUI_H
