#include "backup.h"
#include "utils.h"
#include "logger.h"

#include <filesystem>
#include <iostream>
#include <cstdlib>
#include <Windows.h>

namespace fs = std::filesystem;

namespace Shadow {

BackupManager& BackupManager::instance() {
    static BackupManager inst;
    return inst;
}

BackupManager::BackupManager() {}
BackupManager::~BackupManager() {}

void BackupManager::setEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_enabled = enabled;
}

bool BackupManager::isEnabled() const {
    return m_enabled;
}

std::string BackupManager::getDriveLetter(const std::string& path) {
    if (path.length() >= 2 && path[1] == ':') {
        return std::string(1, path[0]);
    }
    return "C";
}

std::string BackupManager::stripDriveLetter(const std::string& path) {
    if (path.length() >= 3 && path[1] == ':' && (path[2] == '\\' || path[2] == '/')) {
        return path.substr(3);
    }
    return path;
}

void BackupManager::beginSession() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_enabled) return;

    std::string timestamp = Utils::getCurrentDateTimeFormatted();
    for (auto& c : timestamp) {
        if (c == ':' || c == ' ') c = '_';
    }

    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    
    m_sessionDir = std::string(tempPath) + "ShadowBackup_" + timestamp;
    
    std::error_code ec;
    fs::create_directories(m_sessionDir, ec);

    if (!ec) {
        m_sessionActive = true;
        Logger::instance().log(LogLevel::INFO, "[+] Backup session started: " + m_sessionDir);
    } else {
        Logger::instance().log(LogLevel::ERR, "[-] Failed to create backup directory: " + ec.message());
        m_sessionActive = false;
    }
}

void BackupManager::finalizeSession() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_sessionActive) return;

    std::string timestamp = Utils::getCurrentDateTimeFormatted();
    for (auto& c : timestamp) {
        if (c == ':' || c == ' ') c = '_';
    }

    std::string desktop = Utils::getDesktopPath();
    std::string zipPath = desktop + "\\ShadowBackup_" + timestamp + ".zip";

    Logger::instance().log(LogLevel::INFO, "[+] Zipping backup to: " + zipPath);

    // Use native Windows 10/11 tar to create zip
    std::string cmd = "tar.exe -a -c -f \"" + zipPath + "\" -C \"" + m_sessionDir + "\" .";
    int res = std::system(cmd.c_str());

    if (res == 0) {
        Logger::instance().log(LogLevel::INFO, "[+] Backup finalized successfully.");
    } else {
        Logger::instance().log(LogLevel::ERR, "[-] Backup compression failed (tar exit code " + std::to_string(res) + ").");
    }

    // Cleanup temp session directory
    std::error_code ec;
    fs::remove_all(m_sessionDir, ec);

    m_sessionActive = false;
    m_sessionDir.clear();
}

bool BackupManager::backupFile(const std::string& originalPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_enabled || !m_sessionActive) return false;

    std::error_code ec;
    if (!fs::exists(originalPath, ec)) return false;

    std::string drive = getDriveLetter(originalPath);
    std::string strippedPath = stripDriveLetter(originalPath);

    std::string destDir = m_sessionDir + "\\" + drive + "\\" + fs::path(strippedPath).parent_path().string();
    std::string destPath = m_sessionDir + "\\" + drive + "\\" + strippedPath;

    fs::create_directories(destDir, ec);
    if (ec) return false;

    fs::copy_file(originalPath, destPath, fs::copy_options::overwrite_existing, ec);
    
    if (ec) {
        Logger::instance().log(LogLevel::ERR, "[-] Backup failed for: " + originalPath + " (" + ec.message() + ")");
        return false;
    }

    Logger::instance().log(LogLevel::DEBUG_DETAIL, "Backed up: " + originalPath);
    return true;
}

bool BackupManager::restoreBackup(const std::string& zipPath) {
    Logger::instance().log(LogLevel::INFO, "[+] Restoring from backup: " + zipPath);

    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    
    std::string extractDir = std::string(tempPath) + "ShadowRestore";
    
    std::error_code ec;
    fs::remove_all(extractDir, ec);
    fs::create_directories(extractDir, ec);

    std::string cmd = "tar.exe -x -f \"" + zipPath + "\" -C \"" + extractDir + "\"";
    int res = std::system(cmd.c_str());

    if (res != 0) {
        Logger::instance().log(LogLevel::ERR, "[-] Restore extraction failed (tar exit code " + std::to_string(res) + ").");
        return false;
    }

    // Iterate through the extracted folder.
    // Top-level folders should be drive letters (e.g. "C")
    bool restoredAny = false;
    if (fs::exists(extractDir, ec)) {
        for (const auto& driveEntry : fs::directory_iterator(extractDir, ec)) {
            if (ec) break;
            if (driveEntry.is_directory()) {
                std::string driveLetter = driveEntry.path().filename().string();
                
                // Recursively copy contents back to the drive
                for (const auto& fileEntry : fs::recursive_directory_iterator(driveEntry.path(), ec)) {
                    if (ec) break;
                    if (fileEntry.is_regular_file()) {
                        // e.g. Temp/ShadowRestore/C/Windows/Prefetch/game.pf
                        std::string relativePath = fileEntry.path().string().substr(driveEntry.path().string().length() + 1);
                        std::string targetPath = driveLetter + ":\\" + relativePath;

                        fs::create_directories(fs::path(targetPath).parent_path(), ec);
                        fs::copy_file(fileEntry.path(), targetPath, fs::copy_options::overwrite_existing, ec);

                        if (!ec) {
                            Logger::instance().log(LogLevel::ACTION, targetPath + " -> Restored");
                            restoredAny = true;
                        } else {
                            Logger::instance().log(LogLevel::WARNING, "Failed to restore: " + targetPath + " (" + ec.message() + ")");
                        }
                    }
                }
            }
        }
    }

    // Cleanup
    fs::remove_all(extractDir, ec);

    if (restoredAny) {
        Logger::instance().log(LogLevel::INFO, "[+] Restore completed successfully.");
        return true;
    } else {
        Logger::instance().log(LogLevel::WARNING, "[-] Restore completed, but no files were found or restored.");
        return false;
    }
}

} // namespace Shadow
