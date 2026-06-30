#include "gui.h"
#include "config.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include "utils.h"
#include "backup.h"
#include "updater.h"

namespace Shadow {

std::vector<LogEntry> GUI::s_logs;
std::mutex GUI::s_logsMutex;
bool GUI::s_autoScroll = true;

GUI::GUI(Engine& engine) : m_engine(engine) {
    loadTargets();
}

GUI::~GUI() {
}

void GUI::addLog(LogLevel level, const std::string& log) {
    std::lock_guard<std::mutex> lock(s_logsMutex);
    s_logs.push_back({level, log});
}

void GUI::applyTheme() {
    ImGuiStyle* style = &ImGui::GetStyle();
    
    // Modern Rounded Corners
    style->WindowRounding = 6.0f;
    style->FrameRounding = 4.0f;
    style->PopupRounding = 4.0f;
    style->ScrollbarRounding = 4.0f;
    style->GrabRounding = 4.0f;
    style->TabRounding = 4.0f;

    style->WindowBorderSize = 1.0f;
    style->FrameBorderSize = 0.0f;
    
    // Sleek Gray Theme
    ImVec4* colors = style->Colors;
    
    colors[ImGuiCol_WindowBg]       = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_ChildBg]        = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_PopupBg]        = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
    
    colors[ImGuiCol_Border]         = ImVec4(0.25f, 0.25f, 0.25f, 0.50f);
    colors[ImGuiCol_BorderShadow]   = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    
    colors[ImGuiCol_FrameBg]        = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_FrameBgActive]  = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    
    colors[ImGuiCol_TitleBg]        = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgActive]  = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]=ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
    
    colors[ImGuiCol_ScrollbarBg]    = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]  = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]= ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    
    // Checkboxes / Buttons (Light Gray / White highlights)
    colors[ImGuiCol_CheckMark]      = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_Button]         = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_ButtonHovered]  = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_ButtonActive]   = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    
    colors[ImGuiCol_Header]         = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_HeaderHovered]  = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_HeaderActive]   = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    
    colors[ImGuiCol_Tab]            = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_TabHovered]     = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_TabActive]      = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_TabUnfocused]   = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]= ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    
    colors[ImGuiCol_Text]           = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_TextDisabled]   = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
}

void GUI::loadTargets() {
    std::ifstream file("targets.txt");
    if (file.is_open()) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        m_targetsText = buffer.str();
        file.close();
    } else {
        m_targetsText = "Rainmeter-4.5.26.exe\n// Add more targets here\n";
    }
}

void GUI::saveTargets() {
    std::ofstream file("targets.txt");
    if (file.is_open()) {
        file << m_targetsText;
        file.close();
        Config::instance().loadPlaintext("targets.txt");
    }
}

void GUI::render() {
    if (!m_updateChecked) {
        m_updateChecked = true;
        std::thread([this]() {
            std::string version, url;
            if (Shadow::Updater::checkForUpdates(version, url)) {
                this->m_newVersion = version;
                this->m_downloadUrl = url;
                this->m_updateAvailable = true;
            }
        }).detach();
    }

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
    if (ImGui::Begin("Shadow v.1.0.8", nullptr, window_flags)) {
        if (ImGui::BeginTabBar("ShadowTabs", ImGuiTabBarFlags_None)) {
            if (ImGui::BeginTabItem("Dashboard")) {
                renderDashboardTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Targets")) {
                renderTargetsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Panic Room")) {
                renderPanicTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Changelogs")) {
                renderChangelogTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    // ── Updater Modal ────────────────────────────────────────────────────────
    if (m_updateAvailable && !ImGui::IsPopupOpen("Update Available")) {
        ImGui::OpenPopup("Update Available");
    }

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Update Available", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("A new version of Shadow is available: %s", m_newVersion.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("Would you like to download and install it now?");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (m_isDownloadingUpdate) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Downloading update... Please wait.");
        } else {
            if (ImGui::Button("Yes, Update Now", ImVec2(150, 30))) {
                m_isDownloadingUpdate = true;
                std::thread([this]() {
                    char currentExePath[MAX_PATH];
                    GetModuleFileNameA(NULL, currentExePath, MAX_PATH);
                    std::string destPath = std::string(currentExePath);
                    destPath = destPath.substr(0, destPath.find_last_of("\\/")) + "\\Shadow_update.exe";
                    
                    if (Shadow::Updater::downloadUpdate(m_downloadUrl, destPath)) {
                        Shadow::Updater::applyUpdateAndRestart(destPath);
                    } else {
                        // Failed to download
                        m_isDownloadingUpdate = false;
                        m_updateAvailable = false;
                    }
                }).detach();
            }
            ImGui::SameLine();
            if (ImGui::Button("Skip for now", ImVec2(120, 30))) {
                m_updateAvailable = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

void GUI::renderDashboardTab() {
    ImGui::Spacing();
    
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f)); // Subtle cyan hint
    ImGui::Text("CLEANUP MODULES");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Draw checkboxes for all modules
    for (auto& mod : m_engine.getModules()) {
        ImGui::Checkbox(mod.name.c_str(), &mod.enabled);
    }
    
    ImGui::Spacing(); ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("EXECUTION SETTINGS");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Draw Dry-run toggle
    ImGui::Checkbox("Dry-Run Mode (Simulation)", &m_engine.IS_DRY_RUN);
    ImGui::SameLine();
    ImGui::TextDisabled("If enabled, files are not actually deleted.");
    
    ImGui::Checkbox("Create Zip Backup Before Sweep", &m_engine.IS_BACKUP_ENABLED);
    
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 180);
    if (ImGui::Button("Restore from Backup", ImVec2(180, 0))) {
        std::string zipPath = Utils::openFileDialog(L"ZIP Archives", L"*.zip");
        if (!zipPath.empty()) {
            addLog(LogLevel::INFO, "[+] Starting restore process...");
            std::thread([zipPath, this]() {
                BackupManager::instance().restoreBackup(zipPath);
            }).detach();
        }
    }
    
    ImGui::Spacing(); ImGui::Spacing();
    
    // Sleek Execute Button
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f)); // Greenish execute
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.8f, 0.4f, 1.0f));
    
    if (ImGui::Button("INITIATE SHADOW SWEEP", ImVec2(ImGui::GetContentRegionAvail().x, 50))) {
        if (!m_isExecuting) {
            {
                std::lock_guard<std::mutex> lock(s_logsMutex);
                s_logs.clear();
            }
            saveTargets(); // Ensure targets are saved and parsed before execution
            addLog(LogLevel::INFO, "[+] Initializing Shadow Engine...");
            m_isExecuting = true;
            
            std::thread([this]() {
                m_engine.execute();
                addLog(LogLevel::INFO, "[+] Sweep Complete.");
                m_isExecuting = false;
            }).detach();
        }
    }
    ImGui::PopStyleColor(3);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Console Header with Copy Button
    ImGui::Text("LIVE CONSOLE");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 100);
    if (ImGui::Button("Copy Logs", ImVec2(100, 0))) {
        std::string allLogs;
        {
            std::lock_guard<std::mutex> lock(s_logsMutex);
            for (const auto& entry : s_logs) allLogs += entry.text + "\n";
        }
        ImGui::SetClipboardText(allLogs.c_str());
    }

    renderConsole();
}

void GUI::renderTargetsTab() {
    ImGui::Spacing();

    // Toolbar for automated targets
    if (ImGui::Button("Browse for .exe", ImVec2(150, 30))) {
        std::string picked = Utils::openFileDialog();
        if (!picked.empty()) {
            if (!m_targetsText.empty() && m_targetsText.back() != '\n') {
                m_targetsText += "\n";
            }
            m_targetsText += picked + "\n";
            addLog(LogLevel::INFO, "[+] Picked App: " + picked);
        }
    }
    
    ImGui::SameLine();
    
    if (ImGui::Button("Scan Running Apps", ImVec2(150, 30))) {
        std::vector<std::string> running = Utils::getRunningProcesses();
        int added = 0;
        for (const auto& proc : running) {
            // Avoid adding duplicates if it's already in the text box (simple check)
            if (m_targetsText.find(proc) == std::string::npos) {
                if (!m_targetsText.empty() && m_targetsText.back() != '\n') m_targetsText += "\n";
                m_targetsText += proc + "\n";
                added++;
            }
        }
        addLog(LogLevel::INFO, "[+] Scanner found " + std::to_string(added) + " new running processes.");
    }

    ImGui::SameLine();

    if (ImGui::Button("Add Custom File", ImVec2(150, 30))) {
        std::string picked = Utils::openFileDialog(L"All Files", L"*.*");
        if (!picked.empty()) {
            if (m_targetsText.find("[CUSTOM_PATHS]") == std::string::npos) {
                if (!m_targetsText.empty() && m_targetsText.back() != '\n') m_targetsText += "\n";
                m_targetsText += "\n[CUSTOM_PATHS]\n";
            }
            if (!m_targetsText.empty() && m_targetsText.back() != '\n') m_targetsText += "\n";
            m_targetsText += picked + "\n";
            addLog(LogLevel::INFO, "[+] Added Custom File: " + picked);
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Add Custom Folder", ImVec2(150, 30))) {
        std::string picked = Utils::openFolderDialog();
        if (!picked.empty()) {
            if (m_targetsText.find("[CUSTOM_PATHS]") == std::string::npos) {
                if (!m_targetsText.empty() && m_targetsText.back() != '\n') m_targetsText += "\n";
                m_targetsText += "\n[CUSTOM_PATHS]\n";
            }
            if (!m_targetsText.empty() && m_targetsText.back() != '\n') m_targetsText += "\n";
            m_targetsText += picked + "\n";
            addLog(LogLevel::INFO, "[+] Added Custom Folder: " + picked);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Edit your targets.txt directly here:");
    
    static char buffer[1024 * 16];
    strncpy_s(buffer, sizeof(buffer), m_targetsText.c_str(), _TRUNCATE);
    
    // Adjust height since we added a toolbar
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.08f, 0.08f, 1.0f));
    if (ImGui::InputTextMultiline("##targets", buffer, sizeof(buffer), ImVec2(-FLT_MIN, ImGui::GetContentRegionAvail().y - 45))) {
        m_targetsText = buffer;
    }
    ImGui::PopStyleColor();

    if (ImGui::Button("Save Targets", ImVec2(120, 30))) {
        saveTargets();
        addLog(LogLevel::INFO, "[+] Targets saved and reloaded.");
    }
}

void GUI::renderPanicTab() {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
    ImGui::Text("PANIC BUTTON CONFIGURATION");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Hotkey binding:");
    ImGui::Checkbox("CTRL", &Config::instance().panicCtrl); ImGui::SameLine();
    ImGui::Checkbox("ALT", &Config::instance().panicAlt); ImGui::SameLine();
    ImGui::Checkbox("SHIFT", &Config::instance().panicShift);
    ImGui::InputInt("VK Code (e.g., 80 for 'P')", &Config::instance().panicKey);
    ImGui::TextDisabled("Find VK Codes online (Virtual-Key Codes). Default: 0 (Disabled)");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Emergency Paths:");
    ImGui::TextDisabled("These paths are completely eradicated when Panic is triggered.");
    
    if (ImGui::Button("Add Emergency File", ImVec2(150, 30))) {
        std::string picked = Utils::openFileDialog(L"All Files", L"*.*");
        if (!picked.empty()) {
            size_t panicPos = m_targetsText.find("[PANIC_CONFIG]");
            if (m_targetsText.find("[EMERGENCY_PATHS]") == std::string::npos) {
                std::string insertStr = "\n[EMERGENCY_PATHS]\n" + picked + "\n";
                if (panicPos != std::string::npos) {
                    m_targetsText.insert(panicPos, insertStr + "\n");
                } else {
                    if (!m_targetsText.empty() && m_targetsText.back() != '\n') m_targetsText += "\n";
                    m_targetsText += insertStr;
                }
            } else {
                if (panicPos != std::string::npos) {
                    m_targetsText.insert(panicPos, picked + "\n");
                } else {
                    if (!m_targetsText.empty() && m_targetsText.back() != '\n') m_targetsText += "\n";
                    m_targetsText += picked + "\n";
                }
            }
            addLog(LogLevel::INFO, "[+] Added Emergency File: " + picked);
            saveTargets(); // Auto save
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Emergency Folder", ImVec2(160, 30))) {
        std::string picked = Utils::openFolderDialog();
        if (!picked.empty()) {
            size_t panicPos = m_targetsText.find("[PANIC_CONFIG]");
            if (m_targetsText.find("[EMERGENCY_PATHS]") == std::string::npos) {
                std::string insertStr = "\n[EMERGENCY_PATHS]\n" + picked + "\n";
                if (panicPos != std::string::npos) {
                    m_targetsText.insert(panicPos, insertStr + "\n");
                } else {
                    if (!m_targetsText.empty() && m_targetsText.back() != '\n') m_targetsText += "\n";
                    m_targetsText += insertStr;
                }
            } else {
                if (panicPos != std::string::npos) {
                    m_targetsText.insert(panicPos, picked + "\n");
                } else {
                    if (!m_targetsText.empty() && m_targetsText.back() != '\n') m_targetsText += "\n";
                    m_targetsText += picked + "\n";
                }
            }
            addLog(LogLevel::INFO, "[+] Added Emergency Folder: " + picked);
            saveTargets(); // Auto save
        }
    }

    ImGui::Spacing();

    if (ImGui::Button("Save Panic Config", ImVec2(150, 30))) {
        // Strip out any existing [PANIC_CONFIG] block from m_targetsText
        size_t pos = m_targetsText.find("[PANIC_CONFIG]");
        if (pos != std::string::npos) {
            m_targetsText.erase(pos);
        }
        
        // Append updated config
        if (!m_targetsText.empty() && m_targetsText.back() != '\n') m_targetsText += "\n";
        m_targetsText += "\n[PANIC_CONFIG]\n";
        m_targetsText += "KEY=" + std::to_string(Config::instance().panicKey) + "\n";
        m_targetsText += "CTRL=" + std::to_string(Config::instance().panicCtrl ? 1 : 0) + "\n";
        m_targetsText += "ALT=" + std::to_string(Config::instance().panicAlt ? 1 : 0) + "\n";
        m_targetsText += "SHIFT=" + std::to_string(Config::instance().panicShift ? 1 : 0) + "\n";
        
        saveTargets();
        addLog(LogLevel::INFO, "[+] Panic configuration saved.");
    }
}

void GUI::renderChangelogTab() {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("VERSION HISTORY & CHANGELOGS");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::BeginChild("ChangelogScrollRegion", ImVec2(0, 0), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    // v.1.0.8
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "v.1.0.8 - UI Enhancements");
    ImGui::TextDisabled("Latest Update");
    ImGui::Spacing();
    ImGui::BulletText("UI Tweaks: Fixed minor visual string bugs in the internal engine window titling.");
    ImGui::BulletText("Polishing: Small refinements to the dashboard interface to ensure accuracy.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // v.1.0.7
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "v.1.0.7 - The Auto-Updater Live!");
    ImGui::TextDisabled("Previous Update");
    ImGui::Spacing();
    ImGui::BulletText("Background Updates: Shadow now silently checks your GitHub repository for updates on launch.");
    ImGui::BulletText("Native Replacement: Completely downloads, swaps out its own executable, and cleans up without scripts.");
    ImGui::BulletText("Asset Parsing: Smartly identifies the correct raw Shadow.exe out of all release assets.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // v1.0.6
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "v1.0.6 - Stealth & Auto-Updater Prep");
    ImGui::TextDisabled("Previous Update");
    ImGui::Spacing();
    ImGui::BulletText("Background Stealth: Shadow now minimizes directly to the system tray instead of closing.");
    ImGui::BulletText("Tray Context Menu: Right-click the tray icon to quickly restore or completely exit the engine.");
    ImGui::BulletText("Changelog Integration: Added this dedicated tab to track all major version histories and updates.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // v1.0.5
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "v1.0.5 - The Panic Button");
    ImGui::TextDisabled("Major Feature");
    ImGui::Spacing();
    ImGui::BulletText("Global Hotkey: A silent background thread now constantly monitors for your secret keyboard combo.");
    ImGui::BulletText("Emergency Paths: Define specific highly-sensitive files to be targeted exclusively during a Panic execution.");
    ImGui::BulletText("Zero-Trace Protocol: Triggering Panic Mode explicitly bypasses the Backup Engine and silences all local logging.");
    ImGui::BulletText("Visual & Audio Feedback: Nuke deployments trigger a massive red screen overlay and an alert siren.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // v1.0.4
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "v1.0.4 - Custom Targets & Paths");
    ImGui::TextDisabled("Feature Update");
    ImGui::Spacing();
    ImGui::BulletText("Absolute Path Formatting: Implemented native Windows file dialogs to select custom files and folders.");
    ImGui::BulletText("Custom Path Wiping: Added engine support to securely eradicate explicitly defined directories outside of standard caches.");
    ImGui::BulletText("Configuration Auto-Save: Real-time UI input parsing ensures targets are never lost before a sweep.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // v1.0.3
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "v1.0.3 - Backup & Rollback Engine");
    ImGui::TextDisabled("Core Enhancement");
    ImGui::Spacing();
    ImGui::BulletText("Secure Archiving: Shadow now copies all targeted files to a temporary directory before deletion.");
    ImGui::BulletText("ZIP Compression: Integrates the Miniz library to compress deleted data into a secure timestamped zip archive.");
    ImGui::BulletText("Fail-Safe Restoration: Ensures users can easily recover app data if a cache wipe breaks functionality.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // v1.0.2
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "v1.0.2 - Graphical User Interface (GUI)");
    ImGui::TextDisabled("Major Overhaul");
    ImGui::Spacing();
    ImGui::BulletText("ImGui Integration: Replaced the minimal console interface with a sleek, hacker-style dark mode GUI.");
    ImGui::BulletText("Interactive Dashboard: Users can now visually toggle modules and launch Live/Dry-Run sweeps.");
    ImGui::BulletText("Real-Time Console: The CLI output is now streamed directly into the bottom panel of the application window.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // v1.0.1
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "v1.0.1 - Core Foundation");
    ImGui::TextDisabled("Initial Release");
    ImGui::Spacing();
    ImGui::BulletText("Application Caches: Capable of securely erasing cache data for Chrome, Edge, Brave, Firefox, and Discord.");
    ImGui::BulletText("System Artifacts: Parses and clears Windows Prefetch, BAM execution traces, and Registry activity.");
    ImGui::BulletText("Network Wiping: Built-in capabilities to flush ARP routing tables and DNS resolver caches.");
    
    ImGui::EndChild();
}

void GUI::renderConsole() {
    // Console window - Pure black as requested
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f)); 
    ImGui::BeginChild("ConsoleRegion", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    
    {
        std::lock_guard<std::mutex> lock(s_logsMutex);
        for (const auto& log : s_logs) {
            ImVec4 col;
            switch (log.level) {
                case LogLevel::INFO:         col = ImVec4(0.9f, 0.9f, 0.9f, 1.0f); break; // White
                case LogLevel::DEBUG_DETAIL: col = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); break; // Dim
                case LogLevel::WARNING:      col = ImVec4(0.9f, 0.8f, 0.2f, 1.0f); break; // Yellow
                case LogLevel::ERR:          col = ImVec4(0.9f, 0.2f, 0.2f, 1.0f); break; // Red
                case LogLevel::ACTION:       col = ImVec4(0.2f, 0.8f, 0.2f, 1.0f); break; // Green
                case LogLevel::SUMMARY:      col = ImVec4(0.2f, 0.8f, 0.8f, 1.0f); break; // Cyan
                default:                     col = ImVec4(0.7f, 0.7f, 0.7f, 1.0f); break;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(log.text.c_str());
            ImGui::PopStyleColor();
        }
    }
    
    if (s_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

} // namespace Shadow
