#include "prefetch_parser.h"
#include <Windows.h>
#include <fstream>

namespace Shadow {
namespace PrefetchParser {

typedef NTSTATUS (WINAPI *RtlDecompressBuffer_func)(
    USHORT CompressionFormat,
    PUCHAR UncompressedBuffer,
    ULONG  UncompressedBufferSize,
    PUCHAR CompressedBuffer,
    ULONG  CompressedBufferSize,
    PULONG FinalUncompressedSize
);

// Compression format for Win10+ Prefetch
#ifndef COMPRESSION_FORMAT_XPRESS_HUFF
#define COMPRESSION_FORMAT_XPRESS_HUFF 0x0004
#endif

bool containsTargetStrings(const std::string& path, const std::vector<std::string>& targets) {
    if (targets.empty()) return false;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size < 8) return false;

    std::vector<char> buffer(static_cast<size_t>(size));
    if (!file.read(buffer.data(), size)) return false;

    std::string contentToScan;

    // Check for "MAM\x04" signature indicating compressed prefetch
    if (buffer[0] == 'M' && buffer[1] == 'A' && buffer[2] == 'M' && buffer[3] == 0x04) {
        
        HMODULE hNtDll = GetModuleHandleA("ntdll.dll");
        if (!hNtDll) return false;

        auto RtlDecompressBuffer = reinterpret_cast<RtlDecompressBuffer_func>(
            GetProcAddress(hNtDll, "RtlDecompressBuffer"));

        if (!RtlDecompressBuffer) return false;

        // Read uncompressed size from offset 4
        uint32_t uncompressedSize = *reinterpret_cast<uint32_t*>(buffer.data() + 4);
        
        std::vector<char> uncompressedBuffer(uncompressedSize);
        ULONG finalSize = 0;

        // Compressed payload starts at offset 8
        NTSTATUS status = RtlDecompressBuffer(
            COMPRESSION_FORMAT_XPRESS_HUFF,
            reinterpret_cast<PUCHAR>(uncompressedBuffer.data()),
            uncompressedSize,
            reinterpret_cast<PUCHAR>(buffer.data() + 8),
            static_cast<ULONG>(size - 8),
            &finalSize
        );

        if (status == 0) { // STATUS_SUCCESS
            contentToScan.assign(uncompressedBuffer.data(), finalSize);
        } else {
            return false;
        }
    } else {
        // Uncompressed (Win7/8 or uncompressed format)
        contentToScan.assign(buffer.data(), buffer.size());
    }

    // Now simply scan the uncompressed buffer
    for (const auto& target : targets) {
        if (contentToScan.find(target) != std::string::npos) {
            return true;
        }

        // Search for wide-string equivalent as well since prefetch heavily uses UTF-16
        std::string wideTarget;
        for (char c : target) {
            wideTarget.push_back(c);
            wideTarget.push_back('\0');
        }

        if (contentToScan.find(wideTarget) != std::string::npos) {
            return true;
        }
    }

    return false;
}

} // namespace PrefetchParser
} // namespace Shadow
