#pragma once
#include <string>

namespace Shadow {
namespace Updater {

    const std::string CURRENT_VERSION = "v.1.0.8";

    // Checks the GitHub API for the latest release.
    // Returns true if a newer version is found.
    // Populates outVersion with the tag name (e.g. "v1.0.7") and outDownloadUrl with the direct .exe link.
    bool checkForUpdates(std::string& outVersion, std::string& outDownloadUrl);

    // Downloads the file from the given URL to the destPath.
    bool downloadUpdate(const std::string& url, const std::string& destPath);

    // Renames current running exe, replaces it with the downloaded one, launches it, and exits.
    void applyUpdateAndRestart(const std::string& downloadedFile);

}
}
