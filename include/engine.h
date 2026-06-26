#pragma once
#include <functional>
#include <string>
#include <vector>
#include <set>
#include <mutex>

namespace Shadow {

class Engine;   // forward-declared for the Module callback signature

// ─── Target classification ──────────────────────────────────────────────────
enum class TargetType {
    FILE_TARGET,
    DIRECTORY_TARGET,
    REGISTRY_KEY,
    SYSTEM_ACTION
};

// ─── Module descriptor ──────────────────────────────────────────────────────
struct Module {
    int                              id;
    std::string                      name;
    std::string                      description;
    bool                             enabled = false;
    std::function<void(Engine&)>     execute;
};

// ─── Core engine ────────────────────────────────────────────────────────────
class Engine {
public:
    Engine();

    /// Global simulation flag.  When true, no actual file deletions or
    /// system modifications will occur — every operation is logged only.
    bool IS_DRY_RUN = true;

    // ── Module management ───────────────────────────────────────────────────
    void                       registerModule(const std::string& name,
                                              const std::string& description,
                                              std::function<void(Engine&)> fn = nullptr);
    std::vector<Module>&       getModules();
    const std::vector<Module>& getModules() const;

    // ── Execution ───────────────────────────────────────────────────────────

    /// Runs every enabled module in registration order, wrapped in
    /// full exception handling.
    void execute();

    /// Dry-run–aware operation.  In DRY-RUN mode the target is logged as
    /// "Would Modify".  In LIVE mode the file/directory is actually deleted
    /// (or the registry key handled).  Errors are caught, logged as
    /// "Skipped", and execution continues.
    bool processTarget(const std::string& path, TargetType type);

private:
    std::vector<Module>   m_modules;
    int                   m_nextId = 1;
    std::set<std::string> m_modifiedParentPaths;
    std::mutex            m_engineMutex;

    void addModifiedParentPath(const std::string& path);
};

} // namespace Shadow
