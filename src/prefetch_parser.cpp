#include "prefetch_parser.h"
#include <Windows.h>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace Shadow {
namespace PrefetchParser {

// ─── Ntdll decompression function signatures ────────────────────────────────

typedef NTSTATUS (WINAPI *RtlDecompressBuffer_func)(
    USHORT CompressionFormat,
    PUCHAR UncompressedBuffer,
    ULONG  UncompressedBufferSize,
    PUCHAR CompressedBuffer,
    ULONG  CompressedBufferSize,
    PULONG FinalUncompressedSize
);

typedef NTSTATUS (WINAPI *RtlDecompressBufferEx_func)(
    USHORT CompressionFormat,
    PUCHAR UncompressedBuffer,
    ULONG  UncompressedBufferSize,
    PUCHAR CompressedBuffer,
    ULONG  CompressedBufferSize,
    PULONG FinalUncompressedSize,
    PVOID  WorkSpace
);

typedef NTSTATUS (WINAPI *RtlGetCompressionWorkSpaceSize_func)(
    USHORT CompressionFormatAndEngine,
    PULONG CompressBufferWorkSpaceSize,
    PULONG CompressFragmentWorkSpaceSize
);

// Compression format for Win10+ Prefetch
#ifndef COMPRESSION_FORMAT_XPRESS_HUFF
#define COMPRESSION_FORMAT_XPRESS_HUFF 0x0004
#endif

// ─── Internal: resolve ntdll functions once ─────────────────────────────────

struct NtdllFuncs {
    RtlDecompressBuffer_func              DecompressBuffer   = nullptr;
    RtlDecompressBufferEx_func            DecompressBufferEx = nullptr;
    RtlGetCompressionWorkSpaceSize_func   GetWorkSpaceSize   = nullptr;
    bool resolved = false;
};

static NtdllFuncs& getNtdll() {
    static NtdllFuncs funcs;
    if (!funcs.resolved) {
        HMODULE hNtDll = GetModuleHandleA("ntdll.dll");
        if (hNtDll) {
            funcs.DecompressBuffer = reinterpret_cast<RtlDecompressBuffer_func>(
                GetProcAddress(hNtDll, "RtlDecompressBuffer"));
            funcs.DecompressBufferEx = reinterpret_cast<RtlDecompressBufferEx_func>(
                GetProcAddress(hNtDll, "RtlDecompressBufferEx"));
            funcs.GetWorkSpaceSize = reinterpret_cast<RtlGetCompressionWorkSpaceSize_func>(
                GetProcAddress(hNtDll, "RtlGetCompressionWorkSpaceSize"));
        }
        funcs.resolved = true;
    }
    return funcs;
}

// ─── Internal: decompress MAM-compressed buffer ─────────────────────────────

static bool decompressMAM(const std::vector<char>& rawBuffer, std::streamsize rawSize,
                          std::vector<char>& outBuffer, ULONG& outFinalSize) {
    if (rawSize < 8) return false;

    uint32_t uncompressedSize = *reinterpret_cast<const uint32_t*>(rawBuffer.data() + 4);

    // OOM guard: reject files claiming absurd decompression sizes
    if (uncompressedSize == 0 || uncompressedSize > MAX_DECOMPRESSED_SIZE) {
        return false;
    }

    outBuffer.resize(uncompressedSize);
    outFinalSize = 0;

    auto& ntdll = getNtdll();

    PUCHAR compressedData = reinterpret_cast<PUCHAR>(const_cast<char*>(rawBuffer.data()) + 8);
    ULONG  compressedLen  = static_cast<ULONG>(rawSize - 8);

    // Try RtlDecompressBufferEx first (more reliable for XPRESS_HUFF on Win10+)
    if (ntdll.DecompressBufferEx && ntdll.GetWorkSpaceSize) {
        ULONG workSpaceSize = 0;
        ULONG fragWorkSpaceSize = 0;
        NTSTATUS wsStatus = ntdll.GetWorkSpaceSize(
            COMPRESSION_FORMAT_XPRESS_HUFF, &workSpaceSize, &fragWorkSpaceSize);

        if (wsStatus == 0 && fragWorkSpaceSize > 0) {
            std::vector<BYTE> workSpace(fragWorkSpaceSize);
            NTSTATUS status = ntdll.DecompressBufferEx(
                COMPRESSION_FORMAT_XPRESS_HUFF,
                reinterpret_cast<PUCHAR>(outBuffer.data()),
                uncompressedSize,
                compressedData,
                compressedLen,
                &outFinalSize,
                workSpace.data());

            if (status == 0) return true;
        }
    }

    // Fallback to RtlDecompressBuffer
    if (ntdll.DecompressBuffer) {
        NTSTATUS status = ntdll.DecompressBuffer(
            COMPRESSION_FORMAT_XPRESS_HUFF,
            reinterpret_cast<PUCHAR>(outBuffer.data()),
            uncompressedSize,
            compressedData,
            compressedLen,
            &outFinalSize);

        return (status == 0);
    }

    return false;
}

// ─── Internal: case-insensitive ASCII search ────────────────────────────────

static std::string toLowerAscii(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

// ─── Internal: read raw file into buffer ────────────────────────────────────

static bool readFileRaw(const std::string& path, std::vector<char>& buffer, std::streamsize& size) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size < 8) return false;

    buffer.resize(static_cast<size_t>(size));
    if (!file.read(buffer.data(), size)) return false;

    return true;
}

// ─── Internal: get decompressed content ─────────────────────────────────────

static bool getDecompressedContent(const std::vector<char>& buffer, std::streamsize rawSize,
                                   std::string& contentOut) {
    // Check for "MAM\x04" signature indicating compressed prefetch (Win10+)
    if (buffer.size() >= 8 &&
        buffer[0] == 'M' && buffer[1] == 'A' && buffer[2] == 'M' && buffer[3] == 0x04) {

        std::vector<char> decompressed;
        ULONG finalSize = 0;

        if (!decompressMAM(buffer, rawSize, decompressed, finalSize)) {
            return false;
        }
        contentOut.assign(decompressed.data(), finalSize);
    } else {
        // Uncompressed format (Win7/8)
        contentOut.assign(buffer.data(), buffer.size());
    }
    return true;
}

// ─── parsePrefetchFile ──────────────────────────────────────────────────────

PrefetchInfo parsePrefetchFile(const std::string& path) {
    PrefetchInfo info;

    std::vector<char> buffer;
    std::streamsize rawSize = 0;
    if (!readFileRaw(path, buffer, rawSize)) return info;

    std::string content;
    if (!getDecompressedContent(buffer, rawSize, content)) return info;

    if (content.size() < 84) return info;  // Minimum header size

    const uint8_t* data = reinterpret_cast<const uint8_t*>(content.data());

    // Prefetch header layout:
    //   Offset 0:  uint32_t version
    //   Offset 4:  char[4]  signature "SCCA"
    //   Offset 16: uint32_t fileSize
    //   Offset 20: wchar_t[30] executableName (60 bytes, UTF-16LE)

    info.version = *reinterpret_cast<const uint32_t*>(data + 0);

    // Validate SCCA signature
    if (data[4] != 'S' || data[5] != 'C' || data[6] != 'C' || data[7] != 'A') {
        return info;  // Not a valid prefetch file
    }

    // Extract executable name (UTF-16LE at offset 16, 30 wide chars = 60 bytes)
    // Note: In some versions offset is 16, in others it depends on version
    size_t nameOffset = 16;
    for (size_t i = 0; i < 30 && (nameOffset + i * 2 + 1) < content.size(); ++i) {
        wchar_t wc = *reinterpret_cast<const wchar_t*>(data + nameOffset + i * 2);
        if (wc == 0) break;
        if (wc < 128) info.filename.push_back(static_cast<char>(wc));
        else info.filename.push_back('?');
    }

    // Extract run count and last run time based on version
    switch (info.version) {
        case 17: // Windows XP
            if (content.size() >= 0x98 + 8) {
                info.lastRunTime = *reinterpret_cast<const uint64_t*>(data + 0x78);
                info.runCount = *reinterpret_cast<const uint32_t*>(data + 0x90);
            }
            break;

        case 23: // Windows Vista / 7
            if (content.size() >= 0xA0 + 4) {
                info.lastRunTime = *reinterpret_cast<const uint64_t*>(data + 0x80);
                info.runCount = *reinterpret_cast<const uint32_t*>(data + 0x98);
            }
            break;

        case 26: // Windows 8.x
            if (content.size() >= 0xD0 + 4) {
                info.lastRunTime = *reinterpret_cast<const uint64_t*>(data + 0x80);
                info.runCount = *reinterpret_cast<const uint32_t*>(data + 0xD0);
            }
            break;

        case 30: // Windows 10/11
            if (content.size() >= 0xD0 + 4) {
                // Win10 has up to 8 last-run timestamps starting at offset 0x80
                info.lastRunTime = *reinterpret_cast<const uint64_t*>(data + 0x80);
                info.runCount = *reinterpret_cast<const uint32_t*>(data + 0xD0);
            }
            break;

        default:
            // Unknown version — we still have the filename
            break;
    }

    info.valid = true;
    return info;
}

// ─── containsTargetStrings ──────────────────────────────────────────────────

bool containsTargetStrings(const std::string& path, const std::vector<std::string>& targets) {
    if (targets.empty()) return false;

    std::vector<char> buffer;
    std::streamsize rawSize = 0;
    if (!readFileRaw(path, buffer, rawSize)) return false;

    std::string content;
    if (!getDecompressedContent(buffer, rawSize, content)) return false;

    // Normalize content to lowercase for case-insensitive ASCII matching
    std::string contentLower = toLowerAscii(content);

    for (const auto& target : targets) {
        std::string targetLower = toLowerAscii(target);

        // 1. Case-insensitive ASCII search
        if (contentLower.find(targetLower) != std::string::npos) {
            return true;
        }

        // 2. Proper UTF-16LE search using MultiByteToWideChar
        int wideLen = MultiByteToWideChar(CP_UTF8, 0, targetLower.c_str(),
                                          static_cast<int>(targetLower.size()), nullptr, 0);
        if (wideLen > 0) {
            std::vector<wchar_t> wideTarget(wideLen);
            MultiByteToWideChar(CP_UTF8, 0, targetLower.c_str(),
                                static_cast<int>(targetLower.size()),
                                wideTarget.data(), wideLen);

            // Build the byte pattern for the wide string (UTF-16LE, lowercased)
            std::string wideBytes;
            wideBytes.reserve(wideLen * 2);
            for (int i = 0; i < wideLen; ++i) {
                wchar_t lc = static_cast<wchar_t>(towlower(wideTarget[i]));
                wideBytes.push_back(static_cast<char>(lc & 0xFF));
                wideBytes.push_back(static_cast<char>((lc >> 8) & 0xFF));
            }

            // Build lowercased UTF-16LE version of the content for searching
            // (lowercase every other byte pair interpreted as UTF-16LE)
            std::string contentWideLower;
            contentWideLower.reserve(content.size());
            for (size_t i = 0; i + 1 < content.size(); i += 2) {
                wchar_t wc = static_cast<wchar_t>(
                    static_cast<uint8_t>(content[i]) |
                    (static_cast<uint8_t>(content[i + 1]) << 8));
                wchar_t lc = static_cast<wchar_t>(towlower(wc));
                contentWideLower.push_back(static_cast<char>(lc & 0xFF));
                contentWideLower.push_back(static_cast<char>((lc >> 8) & 0xFF));
            }

            if (contentWideLower.find(wideBytes) != std::string::npos) {
                return true;
            }
        }
    }

    return false;
}

} // namespace PrefetchParser
} // namespace Shadow
