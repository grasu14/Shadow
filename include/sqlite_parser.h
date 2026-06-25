#pragma once
#include <string>
#include <vector>

namespace Shadow {
namespace SQLiteParser {

    /// Scans a SQLite database (e.g., Chrome History) and executes DELETE FROM urls
    /// where the URL contains any of the target strings.
    /// Returns true if successful and modifications were made.
    bool removeTargetsFromHistory(const std::string& dbPath, const std::vector<std::string>& targets);

} // namespace SQLiteParser
} // namespace Shadow
