#pragma once
#include <string>
#include <vector>

namespace Shadow {

class Config {
public:
    static Config& instance() {
        static Config instance;
        return instance;
    }

    /// Encrypts a plaintext newline-separated file into an encrypted binary file.
    bool encryptToFile(const std::string& plainFile, const std::string& outFile);

    /// Loads and decrypts targets.dat into memory.
    bool loadEncrypted(const std::string& filepath);

    const std::vector<std::string>& getAppCacheTargets() const;
    const std::vector<std::string>& getSystemArtifactTargets() const;

private:
    Config() = default;

    std::vector<std::string> m_appCacheTargets;
    std::vector<std::string> m_systemArtifactTargets;

    // A simple, native RC4 cipher implementation for string obfuscation
    void applyCipher(std::string& data);
};

} // namespace Shadow
