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
    /// Appends a CRC32 checksum for integrity verification on load.
    bool encryptToFile(const std::string& plainFile, const std::string& outFile);

    /// Loads and decrypts targets.dat into memory.
    /// Verifies the embedded CRC32 checksum before parsing.
    bool loadEncrypted(const std::string& filepath);

    /// Loads plaintext targets file directly (for development/testing).
    bool loadPlaintext(const std::string& filepath);

    const std::vector<std::string>& getAppCacheTargets() const;
    const std::vector<std::string>& getSystemArtifactTargets() const;

private:
    Config() = default;

    std::vector<std::string> m_appCacheTargets;
    std::vector<std::string> m_systemArtifactTargets;

    /// NOTE: This is obfuscation, NOT cryptographic security.
    /// The key is embedded in the binary and extractable with a hex editor.
    /// Its purpose is to prevent casual inspection of target strings in
    /// targets.dat — not to withstand determined reverse engineering.
    /// For build-time key injection, define SHADOW_CIPHER_KEY via CMake.
    void applyCipher(std::string& data);

    /// Parses section-based target format from decrypted/plaintext content.
    bool parseTargetContent(const std::string& content);

    /// Simple CRC32 for integrity verification (not security).
    static uint32_t crc32(const std::string& data);
};

} // namespace Shadow
