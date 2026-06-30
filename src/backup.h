#ifndef BACKUP_H
#define BACKUP_H

#include <string>
#include <mutex>

namespace Shadow {

class BackupManager {
public:
    static BackupManager& instance();

    // Configuration
    void setEnabled(bool enabled);
    bool isEnabled() const;

    // Session Management
    void beginSession();
    void finalizeSession();

    // Backup Action
    bool backupFile(const std::string& originalPath);

    // Restore Action
    bool restoreBackup(const std::string& zipPath);

private:
    BackupManager();
    ~BackupManager();

    // Non-copyable
    BackupManager(const BackupManager&) = delete;
    BackupManager& operator=(const BackupManager&) = delete;

    std::string getDriveLetter(const std::string& path);
    std::string stripDriveLetter(const std::string& path);

    bool m_enabled = true;
    bool m_sessionActive = false;
    std::string m_sessionDir;
    std::mutex m_mutex;
};

} // namespace Shadow

#endif // BACKUP_H
