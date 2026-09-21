// main.cpp — Win32 + DirectX 11 setup + idle event loop.
// Transparent flip-model swapchain so the DWM acrylic backdrop
// (wallpaper blur, like the title bar) shows through the whole window.
#include "app.h"
#include "worker.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <tchar.h>

// Fallbacks for older SDK headers
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMSBT_TRANSIENTWINDOW
#define DWMSBT_TRANSIENTWINDOW 3 // Acrylic
#endif
#ifndef WM_DWMCOLORIZATIONCOLORCHANGED
#define WM_DWMCOLORIZATIONCOLORCHANGED 0x0320
#endif

// --- Shared idle-loop primitives (declared in app.h) ---
HANDLE g_hWakeEvent = NULL;
std::atomic<bool> g_NeedRepaint{ true };

void RequestRepaint() {
    g_NeedRepaint.store(true, std::memory_order_release);
    if (g_hWakeEvent)
        ::SetEvent(g_hWakeEvent);
}

// --- D3D11 globals ---
static ID3D11Device*           g_pd3dDevice = nullptr;
static ID3D11DeviceContext*    g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*         g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

static void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

static void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

static HRESULT g_hrDevice = S_OK;
static HRESULT g_hrFlipSwap = S_OK;
static HRESULT g_hrFallbackSwap = S_OK;
static bool g_usingTransparentSwap = false;

static bool CreateDeviceD3D(HWND hWnd) {
    // 1) Device (no swapchain yet — we try a flip-model transparent one first).
    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION,
        &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
            featureLevelArray, 2, D3D11_SDK_VERSION,
            &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    }
    g_hrDevice = res;
    if (res != S_OK)
        return false;

    // 2) Preferred: flip-model swapchain with per-pixel alpha so the DWM
    //    acrylic backdrop shows through the whole client area.
    //    NOTE: no WS_EX_NOREDIRECTIONBITMAP — that flag is only for
    //    DirectComposition swapchains and makes CreateSwapChainForHwnd fail.
    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    HRESULT hrDev = g_pd3dDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    HRESULT hrAd = hrDev == S_OK ? dxgiDevice->GetAdapter(&adapter) : hrDev;
    HRESULT hrFa = hrAd == S_OK ? adapter->GetParent(IID_PPV_ARGS(&factory)) : hrAd;

    if (SUCCEEDED(hrFa)) {
        DXGI_SWAP_CHAIN_DESC1 desc1{};
        desc1.Width = 0;
        desc1.Height = 0;
        desc1.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc1.Stereo = FALSE;
        desc1.SampleDesc.Count = 1;
        desc1.SampleDesc.Quality = 0;
        desc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc1.BufferCount = 2;
        desc1.Scaling = DXGI_SCALING_STRETCH;
        desc1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc1.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        desc1.Flags = 0;

        IDXGISwapChain1* sc1 = nullptr;
        g_hrFlipSwap = factory->CreateSwapChainForHwnd(
            (IUnknown*)g_pd3dDevice, hWnd, &desc1, nullptr, nullptr, &sc1);
        if (SUCCEEDED(g_hrFlipSwap)) {
            factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);
            sc1->QueryInterface(IID_PPV_ARGS(&g_pSwapChain));
            sc1->Release();
            g_usingTransparentSwap = true;
        }
        factory->Release();
        adapter->Release();
        dxgiDevice->Release();
        if (g_usingTransparentSwap) {
            CreateRenderTarget();
            return true;
        }
    } else {
        g_hrFlipSwap = hrFa;
        if (dxgiDevice) dxgiDevice->Release();
    }

    // 3) Fallback: classic opaque swapchain (the path that always launched).
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }

    DXGI_SWAP_CHAIN_DESC sd{};
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

    res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
        &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
            featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
            &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    }
    g_hrFallbackSwap = res;
    if (res != S_OK)
        return false;

    g_usingTransparentSwap = false;
    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

static ImVec4 g_clearColor = ImVec4(0, 0, 0, 0);
static bool   g_inSizeMove  = false;
static bool   s_inRender    = false;

static void RenderFrame() {
    if (s_inRender)
        return;
    if (g_pd3dDeviceContext == nullptr || g_mainRenderTargetView == nullptr)
        return;
    if (ImGui::GetCurrentContext() == nullptr)
        return;

    s_inRender = true;

    // Safety recovery: if a previous frame was interrupted, cleanly end it before starting a new one
    ImGuiContext& g = *GImGui;
    if (g.FrameCount > 0 && g.FrameCountEnded != g.FrameCount) {
        ImGui::EndFrame();
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    App_RenderFrame();

    ImGui::Render();
    const float clearCol[4] = { g_clearColor.x, g_clearColor.y, g_clearColor.z, g_clearColor.w };
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearCol);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    g_pSwapChain->Present(g_App.vsync ? 1 : 0, 0);

    s_inRender = false;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_GETMINMAXINFO: {
        // Enforce minimum window dimensions so the window can never shrink into a collapsed sliver
        auto* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 420;
        mmi->ptMinTrackSize.y = 300;
        return 0;
    }
    case WM_SIZE: {
        UINT width = (UINT)LOWORD(lParam);
        UINT height = (UINT)HIWORD(lParam);
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED && width > 0 && height > 0) {
            CleanupRenderTarget();
            HRESULT hr = g_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
            if (SUCCEEDED(hr)) {
                CreateRenderTarget();
                // Live resize: render immediately so UI adapts smoothly while dragging
                if (ImGui::GetCurrentContext() != nullptr && g_mainRenderTargetView != nullptr) {
                    RenderFrame();
                }
            }
        }
        RequestRepaint();
        return 0;
    }
    case WM_ENTERSIZEMOVE:
        g_inSizeMove = true;
        ::SetTimer(hWnd, 1, 16, nullptr);
        return 0;
    case WM_EXITSIZEMOVE:
        g_inSizeMove = false;
        ::KillTimer(hWnd, 1);
        RequestRepaint();
        return 0;
    case WM_TIMER:
        if (wParam == 1 && g_inSizeMove && ImGui::GetCurrentContext() != nullptr && g_mainRenderTargetView != nullptr) {
            RenderFrame();
            return 0;
        }
        break;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::KillTimer(hWnd, 1);
        ::PostQuitMessage(0);
        return 0;
    case WM_DPICHANGED: {
        const RECT* suggested = (RECT*)lParam;
        ::SetWindowPos(hWnd, nullptr,
            suggested->left, suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        RequestRepaint();
        return 0;
    }
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        // System color changed -> refresh theme.
        App_RefreshAccentColor();
        return 0;
    default:
        break;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void EnableAcrylicBackdrop(HWND hWnd) {
    BOOL dark = TRUE;
    ::DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    int preference = DWMWCP_ROUND;
    ::DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));

    // Extend the frame over the whole client area so acrylic covers everything,
    // not just the title bar.
    MARGINS margins{ -1, -1, -1, -1 };
    ::DwmExtendFrameIntoClientArea(hWnd, &margins);

    // Win11: TransientWindow backdrop = Acrylic (wallpaper shows through).
    int backdrop = DWMSBT_TRANSIENTWINDOW;
    HRESULT hr = ::DwmSetWindowAttribute(hWnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    if (FAILED(hr)) {
        // Win10 fallback: classic blur-behind.
        DWM_BLURBEHIND bb{};
        bb.dwFlags = DWM_BB_ENABLE;
        bb.fEnable = TRUE;
        bb.hRgnBlur = nullptr;
        ::DwmEnableBlurBehindWindow(hWnd, &bb);
    }
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    g_hWakeEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"ImGuiIdleDemo";
    ::RegisterClassExW(&wc);

    // Plain window: flip-model HWND swapchains (transparent or opaque)
    // both work without WS_EX_NOREDIRECTIONBITMAP. That ex-style is only
    // for DirectComposition swapchains and makes CreateSwapChainForHwnd fail.
    HWND hwnd = ::CreateWindowW(
        wc.lpszClassName, L"ImGui Idle Demo",
        WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800,
        nullptr, nullptr, wc.hInstance, nullptr);

    EnableAcrylicBackdrop(hwnd);

    if (!CreateDeviceD3D(hwnd)) {
        wchar_t buf[512];
        swprintf_s(buf, L"Could not create D3D11 device.\n\ndevice=0x%08X flip=0x%08X fallback=0x%08X",
            (unsigned)g_hrDevice, (unsigned)g_hrFlipSwap, (unsigned)g_hrFallbackSwap);
        ::MessageBoxW(hwnd, buf, L"ImGui Idle Demo — startup failed", MB_OK | MB_ICONERROR);
        CleanupDeviceD3D();
        ::DestroyWindow(hwnd);
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, nCmdShow);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    App_Init();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Transparent clear lets the acrylic backdrop show through.
    // Opaque fallback path clears with solid midnight dark (090909) instead.
    g_clearColor = g_usingTransparentSwap
        ? ImVec4(0.0f, 0.0f, 0.0f, 0.0f)
        : ImVec4(9.0f / 255.0f, 9.0f / 255.0f, 9.0f / 255.0f, 1.0f);
    bool done = false;

    // --- Idle event loop: block when nothing is happening -> ~0% CPU ---
    while (!done) {
        bool wantContinuous = App_WantsContinuousFrames();
        DWORD timeout = wantContinuous ? 16 : INFINITE;

        DWORD waitRes = ::MsgWaitForMultipleObjects(
            1, &g_hWakeEvent, FALSE, timeout, QS_ALLINPUT);

        bool hasMsg = false;
        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            hasMsg = true;
            if (msg.message == WM_QUIT)
                done = true;
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        if (done)
            break;

        if (::IsIconic(hwnd))
            continue;

        bool wokeByWorker = (waitRes == WAIT_OBJECT_0);
        bool needFrame = hasMsg || wokeByWorker
            || g_NeedRepaint.load(std::memory_order_acquire)
            || wantContinuous;

        if (!needFrame)
            continue;

        g_NeedRepaint.store(false, std::memory_order_release);

        RenderFrame();
    }

    Worker_Shutdown();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    if (g_hWakeEvent) { ::CloseHandle(g_hWakeEvent); g_hWakeEvent = nullptr; }
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
