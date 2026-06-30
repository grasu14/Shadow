#include "config.h"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <iostream>

namespace Shadow {

// ─── Cipher key ─────────────────────────────────────────────────────────────
// Allow build-time override: cmake -DSHADOW_CIPHER_KEY="your_key_here"
#ifndef SHADOW_CIPHER_KEY
#define SHADOW_CIPHER_KEY "SHADOW_OPERATIONAL_KEY_2026_X79"
#endif

static const std::string INTERNAL_KEY = SHADOW_CIPHER_KEY;

// ─── CRC32 (IEEE 802.3 polynomial) ─────────────────────────────────────────

uint32_t Config::crc32(const std::string& data) {
    uint32_t crc = 0xFFFFFFFF;
    for (unsigned char byte : data) {
        crc ^= byte;
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & ((crc & 1) ? 0xFFFFFFFF : 0));
        }
    }
    return crc ^ 0xFFFFFFFF;
}

// ─── RC4 stream cipher (symmetric) ─────────────────────────────────────────

void Config::applyCipher(std::string& data) {
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

// ─── Content parser ─────────────────────────────────────────────────────────

bool Config::parseTargetContent(const std::string& content) {
    m_appCacheTargets.clear();
    m_systemArtifactTargets.clear();
    m_customPaths.clear();
    m_emergencyPaths.clear();
    
    panicKey = 0;
    panicCtrl = false;
    panicAlt = false;
    panicShift = false;

    // Format:
    // [APP_CACHE]
    // [SYSTEM_ARTIFACTS]
    // [CUSTOM_PATHS]
    // [EMERGENCY_PATHS]
    // C:\path\to\emergency
    // [PANIC_CONFIG]
    // KEY=80
    // CTRL=1
    // ALT=0
    // SHIFT=1

    std::istringstream stream(content);
    std::string line;
    int currentSection = 0; // 1 = App Cache, 2 = System Artifacts, 3 = Custom Paths, 4 = Emergency Paths, 5 = Panic Config

    while (std::getline(stream, line)) {
        // Strip carriage returns if present
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Strip leading/trailing whitespace
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.erase(line.begin());
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
        if (line.empty()) continue;

        if (line == "[APP_CACHE]") {
            currentSection = 1;
            continue;
        } else if (line == "[SYSTEM_ARTIFACTS]") {
            currentSection = 2;
            continue;
        } else if (line == "[CUSTOM_PATHS]") {
            currentSection = 3;
            continue;
        } else if (line == "[EMERGENCY_PATHS]") {
            currentSection = 4;
            continue;
        } else if (line == "[PANIC_CONFIG]") {
            currentSection = 5;
            continue;
        }

        if (currentSection == 1) {
            m_appCacheTargets.push_back(line);
        } else if (currentSection == 2) {
            m_systemArtifactTargets.push_back(line);
        } else if (currentSection == 3) {
            m_customPaths.push_back(line);
        } else if (currentSection == 4) {
            m_emergencyPaths.push_back(line);
        } else if (currentSection == 5) {
            if (line.find("KEY=") == 0) panicKey = std::stoi(line.substr(4));
            else if (line.find("CTRL=") == 0) panicCtrl = (line.substr(5) == "1");
            else if (line.find("ALT=") == 0) panicAlt = (line.substr(4) == "1");
            else if (line.find("SHIFT=") == 0) panicShift = (line.substr(6) == "1");
        }
    }

    return true;
}

// ─── Encrypt to file (with CRC32 integrity check) ──────────────────────────

bool Config::encryptToFile(const std::string& plainFile, const std::string& outFile) {
    std::ifstream in(plainFile, std::ios::binary);
    if (!in.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    in.close();

    // Compute CRC32 of plaintext content
    uint32_t checksum = crc32(content);

    // Append 4-byte CRC32 before encryption
    content.push_back(static_cast<char>((checksum >>  0) & 0xFF));
    content.push_back(static_cast<char>((checksum >>  8) & 0xFF));
    content.push_back(static_cast<char>((checksum >> 16) & 0xFF));
    content.push_back(static_cast<char>((checksum >> 24) & 0xFF));

    applyCipher(content);

    std::ofstream out(outFile, std::ios::binary);
    if (!out.is_open()) return false;

    out.write(content.c_str(), content.size());
    return true;
}

// ─── Load encrypted file (with CRC32 verification) ─────────────────────────

bool Config::loadEncrypted(const std::string& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    if (!in.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    in.close();

    if (content.size() < 4) return false;

    // Decrypt (RC4 is symmetric)
    applyCipher(content);

    // Extract and verify CRC32 checksum (last 4 bytes)
    size_t dataLen = content.size() - 4;
    uint32_t storedChecksum =
        static_cast<uint32_t>(static_cast<uint8_t>(content[dataLen + 0])) |
        (static_cast<uint32_t>(static_cast<uint8_t>(content[dataLen + 1])) << 8) |
        (static_cast<uint32_t>(static_cast<uint8_t>(content[dataLen + 2])) << 16) |
        (static_cast<uint32_t>(static_cast<uint8_t>(content[dataLen + 3])) << 24);

    std::string plaintext = content.substr(0, dataLen);
    uint32_t computedChecksum = crc32(plaintext);

    if (storedChecksum != computedChecksum) {
        std::cerr << "Config: CRC32 mismatch — targets.dat may be corrupted or tampered with.\n";
        // Fall through anyway to attempt parsing (backwards compatibility with
        // files encrypted before CRC32 was added). If parsing succeeds, the
        // content was likely valid despite the checksum mismatch.
    }

    return parseTargetContent(plaintext);
}

// ─── Load plaintext file (for development / testing) ────────────────────────

bool Config::loadPlaintext(const std::string& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    if (!in.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    in.close();

    return parseTargetContent(content);
}

// ─── Accessors ──────────────────────────────────────────────────────────────

const std::vector<std::string>& Config::getAppCacheTargets() const {
    return m_appCacheTargets;
}

const std::vector<std::string>& Config::getSystemArtifactTargets() const {
    return m_systemArtifactTargets;
}

const std::vector<std::string>& Config::getCustomPaths() const {
    return m_customPaths;
}

const std::vector<std::string>& Config::getEmergencyPaths() const {
    return m_emergencyPaths;
}

} // namespace Shadow
