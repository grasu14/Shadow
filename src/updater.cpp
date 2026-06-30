#include "updater.h"
#include <windows.h>
#include <winhttp.h>
#include <urlmon.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "urlmon.lib")

namespace Shadow {
namespace Updater {

    std::string HTTPGet(const std::wstring& host, const std::wstring& path) {
        std::string result;
        HINTERNET hSession = WinHttpOpen(L"Shadow Updater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (hSession) {
            HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (hConnect) {
                HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
                if (hRequest) {
                    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
                        if (WinHttpReceiveResponse(hRequest, NULL)) {
                            DWORD size = 0;
                            DWORD downloaded = 0;
                            do {
                                size = 0;
                                if (!WinHttpQueryDataAvailable(hRequest, &size)) break;
                                if (size == 0) break;
                                char* buf = new char[size + 1];
                                if (WinHttpReadData(hRequest, (LPVOID)buf, size, &downloaded)) {
                                    buf[downloaded] = 0;
                                    result.append(buf, downloaded);
                                }
                                delete[] buf;
                            } while (size > 0);
                        }
                    }
                    WinHttpCloseHandle(hRequest);
                }
                WinHttpCloseHandle(hConnect);
            }
            WinHttpCloseHandle(hSession);
        }
        return result;
    }

    bool checkForUpdates(std::string& outVersion, std::string& outDownloadUrl) {
        std::string response = HTTPGet(L"api.github.com", L"/repos/grasu14/Shadow/releases/latest");
        if (response.empty()) return false;

        // Simple string search to avoid full JSON parser
        std::string tagKey = "\"tag_name\":";
        size_t tagPos = response.find(tagKey);
        if (tagPos == std::string::npos) return false;
        
        size_t startQuote = response.find("\"", tagPos + tagKey.length());
        if (startQuote == std::string::npos) return false;
        size_t endQuote = response.find("\"", startQuote + 1);
        if (endQuote == std::string::npos) return false;

        std::string tag = response.substr(startQuote + 1, endQuote - startQuote - 1);

        if (tag == CURRENT_VERSION || tag.empty() || tag == "Debug") return false; // Already latest or error or debug

        std::string urlKey = "\"browser_download_url\":";
        size_t urlPos = 0;
        std::string finalUrl = "";

        while ((urlPos = response.find(urlKey, urlPos)) != std::string::npos) {
            startQuote = response.find("\"", urlPos + urlKey.length());
            if (startQuote != std::string::npos) {
                endQuote = response.find("\"", startQuote + 1);
                if (endQuote != std::string::npos) {
                    std::string url = response.substr(startQuote + 1, endQuote - startQuote - 1);
                    // Look specifically for Shadow.exe, ignore setup wizards or source code
                    if (url.length() >= 10 && url.substr(url.length() - 10) == "Shadow.exe") {
                        finalUrl = url;
                        break;
                    }
                }
            }
            urlPos += urlKey.length();
        }

        if (finalUrl.empty()) return false;

        outVersion = tag;
        outDownloadUrl = finalUrl;
        return true;
    }

    bool downloadUpdate(const std::string& url, const std::string& destPath) {
        HRESULT hr = URLDownloadToFileA(NULL, url.c_str(), destPath.c_str(), 0, NULL);
        return SUCCEEDED(hr);
    }

    void applyUpdateAndRestart(const std::string& downloadedFile) {
        char currentExePath[MAX_PATH];
        GetModuleFileNameA(NULL, currentExePath, MAX_PATH);

        std::string currentStr(currentExePath);
        std::string oldExePath = currentStr.substr(0, currentStr.find_last_of("\\/")) + "\\Shadow_old.exe";

        // Ensure old doesn't exist
        DeleteFileA(oldExePath.c_str()); 
        
        // Rename current to old
        if (MoveFileA(currentStr.c_str(), oldExePath.c_str())) {
            // Rename downloaded to current
            MoveFileA(downloadedFile.c_str(), currentStr.c_str());

            // Restart new one
            STARTUPINFOA si = { sizeof(si) };
            PROCESS_INFORMATION pi;
            if (CreateProcessA(currentStr.c_str(), NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
            exit(0);
        }
    }

}
}
