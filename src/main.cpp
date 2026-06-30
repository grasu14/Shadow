#include "engine.h"
#include "gui.h"
#include "utils.h"
#include "modules.h"
#include "config.h"
#include "logger.h"

#include <iostream>
#include <string>
#include <windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <dwmapi.h>
#include <mmsystem.h>
#pragma comment(lib, "Winmm.lib")
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include <shellapi.h>

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_APP_ICON 1001
#define ID_TRAY_SHOW_CONTEXT_MENU_ITEM 3000
#define ID_TRAY_EXIT_CONTEXT_MENU_ITEM 3001

static NOTIFYICONDATAA nid;
static bool isMinimizedToTray = false;

static void AddTrayIcon(HWND hwnd) {
    memset(&nid, 0, sizeof(NOTIFYICONDATAA));
    nid.cbSize = sizeof(NOTIFYICONDATAA);
    nid.hWnd = hwnd;
    nid.uID = ID_TRAY_APP_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    
    // Attempt to load custom icon if user provides one in the future, fallback to default
    nid.hIcon = (HICON)LoadImageA(NULL, "C:\\Users\\ddeni\\Desktop\\Shadow\\Assests2\\Shadow.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE);
    if (!nid.hIcon) {
        nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }
    
    strcpy_s(nid.szTip, "Shadow Background Monitor");
    Shell_NotifyIconA(NIM_ADD, &nid);
}

static void RemoveTrayIcon() {
    Shell_NotifyIconA(NIM_DELETE, &nid);
}

// Global state for DX11
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

// Helper functions for DX11
bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
        return false;

    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
    return true;
}

void CleanupDeviceD3D()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) {
            // Minimize to tray
            ShowWindow(hWnd, SW_HIDE);
            isMinimizedToTray = true;
            return 0;
        }
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_CLOSE:
        // Instead of exiting, minimize to tray
        ShowWindow(hWnd, SW_HIDE);
        isMinimizedToTray = true;
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) 
            return 0;
        break;
    case WM_TRAYICON:
        if (lParam == WM_LBUTTONDBLCLK) {
            // Restore window
            ShowWindow(hWnd, SW_RESTORE);
            SetForegroundWindow(hWnd);
            isMinimizedToTray = false;
        } else if (lParam == WM_RBUTTONUP) {
            // Show Context Menu
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            InsertMenuA(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_SHOW_CONTEXT_MENU_ITEM, "Show Shadow");
            InsertMenuA(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT_CONTEXT_MENU_ITEM, "Exit");
            
            SetForegroundWindow(hWnd); // Required to make menu disappear if clicked outside
            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_SHOW_CONTEXT_MENU_ITEM) {
            ShowWindow(hWnd, SW_RESTORE);
            SetForegroundWindow(hWnd);
            isMinimizedToTray = false;
        } else if (LOWORD(wParam) == ID_TRAY_EXIT_CONTEXT_MENU_ITEM) {
            RemoveTrayIcon();
            PostQuitMessage(0);
        }
        return 0;
    case WM_DESTROY:
        RemoveTrayIcon();
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT WINAPI PanicWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rect;
        GetClientRect(hWnd, &rect);
        HBRUSH brush = CreateSolidBrush(RGB(255, 0, 0));
        FillRect(hdc, &rect, brush);
        DeleteObject(brush);
        
        HFONT hFont = CreateFontA(72, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
        SelectObject(hdc, hFont);
        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkMode(hdc, TRANSPARENT);
        DrawTextA(hdc, "Panic enabled Erasing data", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DeleteObject(hFont);
        
        EndPaint(hWnd, &ps);
        return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

void PanicThread(Shadow::Engine* engine) {
    while (true) {
        Sleep(50);
        
        auto& config = Shadow::Config::instance();
        if (config.panicKey == 0) continue;

        bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool altPressed = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        bool shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        bool keyPressed = (GetAsyncKeyState(config.panicKey) & 0x8000) != 0;

        if (keyPressed && (ctrlPressed == config.panicCtrl) && (altPressed == config.panicAlt) && (shiftPressed == config.panicShift)) {
            // Spawn Notification Window
            std::thread([]() {
                WNDCLASSEXA wc = { sizeof(WNDCLASSEXA), CS_CLASSDC, PanicWndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, "PanicWindowClass", nullptr };
                RegisterClassExA(&wc);
                int screenW = GetSystemMetrics(SM_CXSCREEN);
                int screenH = GetSystemMetrics(SM_CYSCREEN);
                HWND hwnd = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, "PanicWindowClass", "Panic", WS_POPUP | WS_VISIBLE, 0, (screenH - 200) / 2, screenW, 200, nullptr, nullptr, wc.hInstance, nullptr);
                SetLayeredWindowAttributes(hwnd, 0, 230, LWA_ALPHA);
                
                MSG msg;
                DWORD start = GetTickCount();
                while (GetTickCount() - start < 4000) {
                    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&msg);
                        DispatchMessage(&msg);
                    } else {
                        Sleep(10);
                    }
                }
                DestroyWindow(hwnd);
                UnregisterClassA("PanicWindowClass", wc.hInstance);
            }).detach();

            mciSendStringA("play \"C:\\Users\\ddeni\\Desktop\\Shadow\\Assests2\\universfield-new-notification-024-370048.mp3\"", NULL, 0, NULL);
            
            engine->execute(true);
            
            // Wait for key release to avoid repeated triggers
            while ((GetAsyncKeyState(config.panicKey) & 0x8000) != 0) {
                Sleep(100);
            }
        }
    }
}


int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow) {
    // ── Updater Cleanup ──────────────────────────────────────────────────────
    char currentExePath[MAX_PATH];
    GetModuleFileNameA(NULL, currentExePath, MAX_PATH);
    std::string currentStr(currentExePath);
    std::string oldExePath = currentStr.substr(0, currentStr.find_last_of("\\/")) + "\\Shadow_old.exe";
    DeleteFileA(oldExePath.c_str());

    int argc = __argc;
    char** argv = __argv;

    if (argc == 4 && std::string(argv[1]) == "--encrypt") {
        std::string inFile = argv[2];
        std::string outFile = argv[3];
        if (Shadow::Config::instance().encryptToFile(inFile, outFile)) {
            MessageBoxA(NULL, "Successfully encrypted config.", "Shadow", MB_OK);
            return 0;
        } else {
            MessageBoxA(NULL, "Failed to encrypt config.", "Shadow", MB_ICONERROR);
            return 1;
        }
    }

#ifdef SHADOW_DEBUG
    // If we're debugging, also show a console so we can see crash logs
    Shadow::Utils::setupConsole();
    Shadow::Utils::disableQuickEdit();
#endif

    // ── Administrator check ─────────────────────────────────────────────────
    if (!Shadow::Utils::isRunningAsAdmin()) {
        MessageBoxA(NULL, "Shadow must be run as Administrator.\nRight-click the executable and select 'Run as administrator'.", "Shadow - Admin Required", MB_ICONERROR);
        return 1;
    }

    // ── Load Configuration ──────────────────────────────────────────────────
    bool configLoaded = Shadow::Config::instance().loadEncrypted("targets.dat");
    if (!configLoaded) {
        configLoaded = Shadow::Config::instance().loadPlaintext("targets.txt");
    }
    
    // ── Initialize COM (For File Dialogs) ───────────────────────────────────
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // Create application window
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ShadowClass", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Shadow v.1.0.8", WS_OVERLAPPEDWINDOW, 100, 100, 900, 600, nullptr, nullptr, wc.hInstance, nullptr);

    // Enable dark mode for the window title bar
    BOOL value = TRUE;
    ::DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &value, sizeof(value));

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Initialize Tray Icon
    AddTrayIcon(hwnd);

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // ── Engine setup ────────────────────────────────────────────────────────
    Shadow::Engine engine;
    Shadow::Modules::registerAll(engine);
    
    // Create our GUI instance
    Shadow::GUI shadowGui(engine);
    Shadow::GUI::applyTheme();

    // Start Panic Thread
    std::thread(PanicThread, &engine).detach();

    if (!configLoaded) {
        Shadow::GUI::addLog(Shadow::LogLevel::WARNING, "[!] WARNING: Could not load 'targets.dat' or 'targets.txt'.");
    }

    // Main loop
    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Render our Shadow UI
        shadowGui.render();

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Update and Render additional Platform Windows
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        g_pSwapChain->Present(1, 0); // Present with vsync
    }

    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    CoUninitialize();

    return 0;
}
