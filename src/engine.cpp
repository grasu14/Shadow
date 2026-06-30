#include "engine.h"
#include "logger.h"
#include "utils.h"
#include "timestomp.h"
#include "backup.h"
#include <filesystem>
#include <iostream>
#include <Windows.h>

namespace fs = std::filesystem;

namespace Shadow {

// ─── Windows error-code formatter ───────────────────────────────────────────

static std::string formatWindowsError(int code) {
    switch (code) {
        case 2:   return "Error 2: File Not Found";
        case 3:   return "Error 3: Path Not Found";
        case 5:   return "Error 5: Access Denied";
        case 19:  return "Error 19: Write Protected";
        case 32:  return "Error 32: Sharing Violation (File In Use)";
        case 33:  return "Error 33: Lock Violation";
        case 80:  return "Error 80: File Already Exists";
        case 123: return "Error 123: Invalid Name";
        case 145: return "Error 145: Directory Not Empty";
        case 183: return "Error 183: Already Exists";
        default:  return "Error " + std::to_string(code);
    }
}

Engine::Engine() = default;

// ─── Module registration ────────────────────────────────────────────────────

void Engine::registerModule(const std::string& name,
                             const std::string& description,
                             std::function<void(Engine&)> fn) {
    Module mod;
    mod.id          = m_nextId++;
    mod.name        = name;
    mod.description = description;
    mod.enabled     = false;
    mod.execute     = fn ? std::move(fn) : [](Engine&) {};
    m_modules.push_back(std::move(mod));
}

std::vector<Module>&       Engine::getModules()       { return m_modules; }
const std::vector<Module>& Engine::getModules() const { return m_modules; }

// ─── Main execution loop ────────────────────────────────────────────────────

void Engine::execute(bool isPanicMode) {
    Logger& log = Logger::instance();
    log.initialize(IS_DRY_RUN, isPanicMode);

    log.log(LogLevel::INFO,
            "============================================");
    log.log(LogLevel::INFO,
            std::string("  Execution started  -  Mode: ") +
            (IS_DRY_RUN ? "DRY-RUN" : "LIVE"));
    if (isPanicMode) {
        log.log(LogLevel::WARNING, "  *** PANIC MODE ACTIVATED ***");
    }
    log.log(LogLevel::INFO,
            "============================================");

    if (!IS_DRY_RUN && IS_BACKUP_ENABLED && !isPanicMode) {
        BackupManager::instance().setEnabled(true);
        BackupManager::instance().beginSession();
    } else {
        BackupManager::instance().setEnabled(false);
    }

    // Check that at least one module is enabled.
    bool anyEnabled = false;
    for (const auto& m : m_modules)
        if (m.enabled) { anyEnabled = true; break; }

    if (!anyEnabled) {
        log.log(LogLevel::WARNING, "No modules enabled. Nothing to do.");
        log.shutdown();
        return;
    }

    // Explicit administrator privilege check for live execution.
    if (!IS_DRY_RUN && !Utils::isRunningAsAdmin()) {
        log.log(LogLevel::ERR,
                "Cannot execute in LIVE mode without Administrator privileges.");
        log.log(LogLevel::ERR,
                "Restart Shadow as Administrator or use DRY-RUN mode.");
        log.shutdown();
        return;
    }

    // Execute each enabled module sequentially.
    // Sequential execution eliminates race conditions on shared Engine state
    // (IS_DRY_RUN, m_modifiedParentPaths, logger counters) and provides no
    // real performance benefit since all modules perform filesystem I/O on
    // the same disk.
    for (auto& mod : m_modules) {
        if (!mod.enabled) continue;

        log.log(LogLevel::INFO, "");
        log.log(LogLevel::INFO, "--- Module: " + mod.name + " ---");
        log.log(LogLevel::DEBUG_DETAIL, "    " + mod.description);

        try {
            mod.execute(*this);
        } catch (const std::exception& e) {
            log.log(LogLevel::ERR,
                    "Module '" + mod.name +
                    "' threw an unhandled exception: " + e.what());
        } catch (...) {
            log.log(LogLevel::ERR,
                    "Module '" + mod.name +
                    "' threw an unknown exception.");
        }
        log.log(LogLevel::DEBUG_DETAIL,
                "Module '" + mod.name + "' finished.");
    }

    // ── Summary ─────────────────────────────────────────────────────────────
    log.log(LogLevel::INFO, "");
    log.log(LogLevel::SUMMARY, "============================================");
    log.log(LogLevel::SUMMARY, "  Execution Complete");
    log.log(LogLevel::SUMMARY,
            "  Targeted : " + std::to_string(log.getTargetedCount()) +
            "  |  Modified : " + std::to_string(log.getModifiedCount()) +
            "  |  Skipped : " + std::to_string(log.getSkippedCount()) +
            "  |  Errors : " + std::to_string(log.getErrorCount()));
    log.log(LogLevel::SUMMARY, "============================================");

    // Use mode-aware log filename with timestamp
    if (!isPanicMode) {
        std::string timestamp = Utils::getCurrentDateTimeFormatted();
        // Replace colons and spaces for filesystem compatibility
        for (auto& c : timestamp) {
            if (c == ':' || c == ' ') c = '_';
        }
        std::string logName = std::string("shadow_") +
            (IS_DRY_RUN ? "dryrun_" : "live_") + timestamp + ".txt";
        std::string logPath = Utils::getDesktopPath() + "\\" + logName;
        log.log(LogLevel::INFO, "  Log saved to: " + logPath);
    }

    // ── Skipped items detail ────────────────────────────────────────────────
    const auto& skipped = log.getSkippedItems();
    if (!skipped.empty()) {
        log.log(LogLevel::INFO, "");
        log.log(LogLevel::WARNING,
                "============================================");
        log.log(LogLevel::WARNING,
                "  Skipped Items (" + std::to_string(skipped.size()) + ")");
        log.log(LogLevel::WARNING,
                "============================================");

        int idx = 1;
        for (const auto& item : skipped) {
            std::string line = "  [" + std::to_string(idx++) + "] " + item.first;
            if (!item.second.empty())
                line += " -> " + item.second;
            log.log(LogLevel::WARNING, line);
        }

        log.log(LogLevel::WARNING,
                "============================================");
    }

    // ── AI Safety Prompt (dry-run only) ─────────────────────────────────────
    if (IS_DRY_RUN && log.getTargetedCount() > 0) {
        log.generateAISafetyPrompt();
    }

    // ── Timestomping ────────────────────────────────────────────────────────
    Timestomp::applyToAllPaths(m_modifiedParentPaths, IS_DRY_RUN);
    m_modifiedParentPaths.clear();

    if (!IS_DRY_RUN && IS_BACKUP_ENABLED && !isPanicMode) {
        BackupManager::instance().finalizeSession();
    }

    log.shutdown();
}

// ─── Dry-run–aware target processor ─────────────────────────────────────────

bool Engine::processTarget(const std::string& path, TargetType type) {
    Logger& log = Logger::instance();

    // ── Dry-run path ────────────────────────────────────────────────────────
    if (IS_DRY_RUN) {
        log.logAction(path, ActionStatus::WOULD_MODIFY, "Dry-run simulation");
        return true;
    }

    // ── Live path (error-code aware) ────────────────────────────────────────
    try {
        std::error_code ec;

        switch (type) {

            case TargetType::FILE_TARGET: {
                if (!fs::exists(path, ec)) {
                    if (ec) {
                        log.logAction(path, ActionStatus::SKIPPED,
                                      formatWindowsError(ec.value()));
                        return false;
                    }
                    log.log(LogLevel::DEBUG_DETAIL, "File not found: " + path);
                    return false;
                }

                BackupManager::instance().backupFile(path);

                fs::remove(path, ec);
                if (ec) {
                    log.logAction(path, ActionStatus::SKIPPED,
                                  formatWindowsError(ec.value()));
                    return false;
                }

                addModifiedParentPath(fs::path(path).parent_path().string());
                log.logAction(path, ActionStatus::MODIFIED);
                return true;
            }

            case TargetType::DIRECTORY_TARGET: {
                if (!fs::exists(path, ec)) {
                    if (ec) {
                        log.logAction(path, ActionStatus::SKIPPED,
                                      formatWindowsError(ec.value()));
                        return false;
                    }
                    log.log(LogLevel::DEBUG_DETAIL,
                            "Directory not found: " + path);
                    return false;
                }

                BackupManager::instance().backupFile(path);

                fs::remove_all(path, ec);
                if (ec) {
                    log.logAction(path, ActionStatus::SKIPPED,
                                  formatWindowsError(ec.value()));
                    return false;
                }

                addModifiedParentPath(fs::path(path).parent_path().string());
                log.logAction(path, ActionStatus::MODIFIED);
                return true;
            }

            case TargetType::REGISTRY_KEY: {
                void* rootKey = nullptr;
                std::string subKey;
                std::string valueName;

                if (!Registry::parseRegistryPath(path, rootKey, subKey, valueName)) {
                    log.logAction(path, ActionStatus::SKIPPED, "Invalid registry path format");
                    return false;
                }

                if (valueName.empty()) {
                    log.logAction(path, ActionStatus::SKIPPED, "Whole key deletion not implemented (safety)");
                    return false;
                }

                HKEY hKey;
                LSTATUS status = RegOpenKeyExA(static_cast<HKEY>(rootKey), subKey.c_str(), 0, KEY_SET_VALUE | KEY_WRITE, &hKey);
                if (status != ERROR_SUCCESS) {
                    log.logAction(path, ActionStatus::SKIPPED, formatWindowsError(status));
                    return false;
                }

                status = RegDeleteValueA(hKey, valueName.c_str());
                if (status != ERROR_SUCCESS) {
                    RegCloseKey(hKey);
                    log.logAction(path, ActionStatus::SKIPPED, formatWindowsError(status));
                    return false;
                }

                // Timestomp the key so the modification isn't obvious
                Timestomp::applyToRegistryKey(hKey);

                RegCloseKey(hKey);
                log.logAction(path, ActionStatus::MODIFIED);
                return true;
            }

            // SYSTEM_ACTION is a pseudo-target type used to log operations that
            // don't correspond to a single file/key (e.g., DNS cache flush).
            // In live mode it just records the action in the log.
            case TargetType::SYSTEM_ACTION: {
                log.logAction(path, ActionStatus::MODIFIED);
                return true;
            }
        }
    } catch (const fs::filesystem_error& e) {
        // Extract the OS error code from the nested error_code.
        int code = e.code().value();
        log.logAction(path, ActionStatus::SKIPPED,
                      formatWindowsError(code) + " - " + e.what());
    } catch (const std::exception& e) {
        log.logAction(path, ActionStatus::SKIPPED,
                      std::string("Exception: ") + e.what());
    } catch (...) {
        log.logAction(path, ActionStatus::SKIPPED,
                      "Unknown exception encountered");
    }

    return false;
}

void Engine::addModifiedParentPath(const std::string& path) {
    if (!path.empty()) {
        std::lock_guard<std::mutex> lock(m_engineMutex);
        m_modifiedParentPaths.insert(path);
    }
}

} // namespace Shadow
