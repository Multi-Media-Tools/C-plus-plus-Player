#include "app.h"
#include "ui_sidebar.h"
#include "ui_homepage.h"
#include "ui_mediaplayer.h"
#include "ui_settings.h"
#include "worker.h"
#include "imgui.h"
#include <dwmapi.h>
#include <cmath>
#include <algorithm>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

AppState g_App;

// --- Midnight grey theme accent palette ---
// Unselected hover: 0f0f0f, Selected: 2c2c2c, Selected & Hovered: 545454
static ImVec4 s_accent       = ImVec4(44.0f / 255.0f, 44.0f / 255.0f, 44.0f / 255.0f, 1.0f); // 2c2c2c
static ImVec4 s_accentHover  = ImVec4(84.0f / 255.0f, 84.0f / 255.0f, 84.0f / 255.0f, 1.0f); // 545454
static ImVec4 s_accentActive = ImVec4(36.0f / 255.0f, 36.0f / 255.0f, 36.0f / 255.0f, 1.0f);

void App_RefreshAccentColor() {
    // Theme color is fixed to sleek midnight grey instead of Windows accent / blue.
    s_accent       = ImVec4(44.0f / 255.0f, 44.0f / 255.0f, 44.0f / 255.0f, 1.0f); // 2c2c2c
    s_accentHover  = ImVec4(84.0f / 255.0f, 84.0f / 255.0f, 84.0f / 255.0f, 1.0f); // 545454
    s_accentActive = ImVec4(36.0f / 255.0f, 36.0f / 255.0f, 36.0f / 255.0f, 1.0f);
    RequestRepaint();
}

ImVec4 App_GetAccentColor()  { return s_accent; }
ImVec4 App_GetAccentHovered() { return s_accentHover; }
ImVec4 App_GetAccentActive()  { return s_accentActive; }

void App_ApplyDarkTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 12.0f;
    style.ChildRounding = 10.0f;
    style.FrameRounding = 8.0f;
    style.PopupRounding = 10.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.TabRounding = 8.0f;
    style.WindowBorderSize = 1.5f;
    style.ChildBorderSize = 1.5f;
    style.FrameBorderSize = 0.0f;

    // Translucent 090909 midnight dark so the window acrylic (wallpaper blur) shows through.
    // Host window itself draws nothing (NoBackground); panels below tint it.
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                  = ImVec4(0.92f, 0.92f, 0.94f, 1.00f);
    c[ImGuiCol_TextDisabled]          = ImVec4(0.46f, 0.46f, 0.50f, 1.00f);
    c[ImGuiCol_WindowBg]              = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_ChildBg]               = ImVec4(9.0f / 255.0f, 9.0f / 255.0f, 9.0f / 255.0f, 0.72f); // 090909
    c[ImGuiCol_PopupBg]               = ImVec4(9.0f / 255.0f, 9.0f / 255.0f, 9.0f / 255.0f, 0.95f); // 090909
    c[ImGuiCol_Border]                = ImVec4(44.0f / 255.0f, 44.0f / 255.0f, 44.0f / 255.0f, 1.00f); // 2c2c2c
    c[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_FrameBg]               = ImVec4(1.00f, 1.00f, 1.00f, 0.045f);
    c[ImGuiCol_FrameBgHovered]        = ImVec4(15.0f / 255.0f, 15.0f / 255.0f, 15.0f / 255.0f, 0.80f);
    c[ImGuiCol_FrameBgActive]         = ImVec4(44.0f / 255.0f, 44.0f / 255.0f, 44.0f / 255.0f, 0.90f);
    c[ImGuiCol_TitleBg]               = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TitleBgActive]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_MenuBarBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_ScrollbarGrab]         = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);
    c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(1.00f, 1.00f, 1.00f, 0.18f);
    c[ImGuiCol_ScrollbarGrabActive]   = ImVec4(1.00f, 1.00f, 1.00f, 0.25f);
    c[ImGuiCol_CheckMark]             = ImVec4(0.88f, 0.88f, 0.90f, 1.00f);
    c[ImGuiCol_SliderGrab]            = ImVec4(44.0f / 255.0f, 44.0f / 255.0f, 44.0f / 255.0f, 1.00f); // 2c2c2c
    c[ImGuiCol_SliderGrabActive]      = ImVec4(84.0f / 255.0f, 84.0f / 255.0f, 84.0f / 255.0f, 1.00f); // 545454
    c[ImGuiCol_Button]                = ImVec4(1.00f, 1.00f, 1.00f, 0.02f);
    c[ImGuiCol_ButtonHovered]         = ImVec4(32.0f / 255.0f, 32.0f / 255.0f, 32.0f / 255.0f, 1.00f); // 202020
    c[ImGuiCol_ButtonActive]          = ImVec4(36.0f / 255.0f, 36.0f / 255.0f, 36.0f / 255.0f, 1.00f);
    c[ImGuiCol_Header]                = ImVec4(44.0f / 255.0f, 44.0f / 255.0f, 44.0f / 255.0f, 1.00f); // 2c2c2c
    c[ImGuiCol_HeaderHovered]         = ImVec4(84.0f / 255.0f, 84.0f / 255.0f, 84.0f / 255.0f, 1.00f); // 545454
    c[ImGuiCol_HeaderActive]          = ImVec4(36.0f / 255.0f, 36.0f / 255.0f, 36.0f / 255.0f, 1.00f);
    c[ImGuiCol_Separator]             = ImVec4(44.0f / 255.0f, 44.0f / 255.0f, 44.0f / 255.0f, 1.00f); // 2c2c2c
    c[ImGuiCol_SeparatorHovered]      = ImVec4(84.0f / 255.0f, 84.0f / 255.0f, 84.0f / 255.0f, 0.80f);
    c[ImGuiCol_SeparatorActive]       = ImVec4(44.0f / 255.0f, 44.0f / 255.0f, 44.0f / 255.0f, 1.00f);
    c[ImGuiCol_ResizeGrip]            = ImVec4(1.00f, 1.00f, 1.00f, 0.04f);
    c[ImGuiCol_ResizeGripHovered]     = ImVec4(1.00f, 1.00f, 1.00f, 0.12f);
    c[ImGuiCol_ResizeGripActive]      = ImVec4(1.00f, 1.00f, 1.00f, 0.20f);
    c[ImGuiCol_Tab]                   = ImVec4(15.0f / 255.0f, 15.0f / 255.0f, 15.0f / 255.0f, 1.00f); // 0f0f0f
    c[ImGuiCol_TabHovered]            = ImVec4(84.0f / 255.0f, 84.0f / 255.0f, 84.0f / 255.0f, 1.00f); // 545454
    c[ImGuiCol_TabSelected]           = ImVec4(44.0f / 255.0f, 44.0f / 255.0f, 44.0f / 255.0f, 1.00f); // 2c2c2c
    c[ImGuiCol_TabDimmed]             = ImVec4(9.0f / 255.0f, 9.0f / 255.0f, 9.0f / 255.0f, 0.80f);
    c[ImGuiCol_TabDimmedSelected]     = ImVec4(28.0f / 255.0f, 28.0f / 255.0f, 28.0f / 255.0f, 1.00f);
    c[ImGuiCol_PlotLines]             = ImVec4(0.65f, 0.65f, 0.68f, 1.00f);
    c[ImGuiCol_PlotLinesHovered]      = ImVec4(0.85f, 0.85f, 0.88f, 1.00f);
    c[ImGuiCol_PlotHistogram]         = ImVec4(44.0f / 255.0f, 44.0f / 255.0f, 44.0f / 255.0f, 1.00f); // 2c2c2c
    c[ImGuiCol_PlotHistogramHovered]  = ImVec4(84.0f / 255.0f, 84.0f / 255.0f, 84.0f / 255.0f, 1.00f); // 545454
    c[ImGuiCol_TableHeaderBg]         = ImVec4(15.0f / 255.0f, 15.0f / 255.0f, 15.0f / 255.0f, 1.00f); // 0f0f0f
    c[ImGuiCol_TableBorderStrong]     = ImVec4(44.0f / 255.0f, 44.0f / 255.0f, 44.0f / 255.0f, 1.00f); // 2c2c2c
    c[ImGuiCol_TableBorderLight]      = ImVec4(44.0f / 255.0f, 44.0f / 255.0f, 44.0f / 255.0f, 0.40f);
    c[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.02f);
    c[ImGuiCol_TextSelectedBg]        = ImVec4(84.0f / 255.0f, 84.0f / 255.0f, 84.0f / 255.0f, 0.45f); // 545454
    c[ImGuiCol_NavCursor]             = ImVec4(84.0f / 255.0f, 84.0f / 255.0f, 84.0f / 255.0f, 0.80f);
}

void App_Init() {
    App_RefreshAccentColor();
    App_ApplyDarkTheme();
    g_App.sidebarWidth = 260.0f;
    g_App.sidebarTargetWidth = 260.0f;
    MediaPlayer_Init();
}

static float s_splitterAlpha = 0.0f;
static bool  s_splitterFading = false;

bool App_WantsContinuousFrames() {
    if (Worker_IsRunning())
        return true;
    if (MediaPlayer_IsScanning())
        return true;
    if (Sidebar_IsAnimating())
        return true;
    if (g_App.sidebarDragging)
        return true;
    if (s_splitterFading)
        return true;
    if (g_NeedRepaint.load())
        return true;
    return false;
}

void App_RenderFrame() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    bool hostVisible = ImGui::Begin("##Host", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar(3);

    if (!hostVisible) {
        ImGui::End();
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float hostW = avail.x;
    float hostH = avail.y;

    if (hostW < 60.0f || hostH < 60.0f) {
        ImGui::End();
        return;
    }

    // --- Recompute + animate sidebar target ---
    float maxSbW = std::max(60.0f, hostW * 0.40f);
    float minSbW = std::min(150.0f, maxSbW);
    float wantTarget = g_App.sidebarCollapsed
        ? g_App.collapsedWidth
        : std::clamp(g_App.sidebarFrac * hostW, minSbW, maxSbW);
    if (g_App.sidebarCollapsed)
        wantTarget = std::clamp(wantTarget, 48.0f, 120.0f);
    g_App.sidebarTargetWidth = wantTarget;

    float diff = g_App.sidebarTargetWidth - g_App.sidebarWidth;
    if (std::fabs(diff) > 0.5f) {
        g_App.sidebarWidth += diff * 0.22f;
        if (std::fabs(g_App.sidebarTargetWidth - g_App.sidebarWidth) <= 0.5f)
            g_App.sidebarWidth = g_App.sidebarTargetWidth;
        RequestRepaint();
    } else {
        g_App.sidebarWidth = g_App.sidebarTargetWidth;
    }
    float sbW = g_App.sidebarWidth;

    // --- Sidebar panel ---
    ImGui::BeginChild("##Sidebar", ImVec2(sbW, 0),
        ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    RenderSidebar(ImVec2(sbW, hostH));
    ImGui::EndChild();

    // --- Splitter: drag to resize % of window ---
    ImGui::SameLine(0, 4);

    // Release drag when left mouse button is released
    if (g_App.sidebarDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        g_App.sidebarDragging = false;
        RequestRepaint();
    }

    ImVec2 splitMin = ImGui::GetCursorScreenPos();
    ImVec2 splitMax = ImVec2(splitMin.x + 6.0f, splitMin.y + hostH);
    bool splitHovered = ImGui::IsMouseHoveringRect(splitMin, splitMax) && ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

    // Two-stage highlight with smooth fade:
    // Stage 1 (hovered, not dragging): cursor on top -> 0.14f
    // Stage 2 (dragging): stays highlighted at 0.22f even if cursor lags away
    float targetSplitterAlpha = 0.0f;
    if (g_App.sidebarDragging) {
        targetSplitterAlpha = 0.22f;
    } else if (splitHovered) {
        targetSplitterAlpha = 0.14f;
    }

    float dt = ImGui::GetIO().DeltaTime;
    if (dt <= 0.0f || dt > 0.1f) dt = 0.016f;
    float sFactor = 1.0f - std::exp(-15.0f * dt);
    s_splitterAlpha += (targetSplitterAlpha - s_splitterAlpha) * sFactor;
    s_splitterFading = std::fabs(targetSplitterAlpha - s_splitterAlpha) > 0.002f;
    if (!s_splitterFading) {
        s_splitterAlpha = targetSplitterAlpha;
    } else {
        RequestRepaint();
    }

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, s_splitterAlpha));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, s_splitterAlpha));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, s_splitterAlpha));
    ImGui::Button("##splitter", ImVec2(6, -1));
    ImGui::PopStyleColor(3);

    if (ImGui::IsItemActive()) {
        g_App.sidebarDragging = true;
    }

    if (g_App.sidebarDragging) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        float dx = ImGui::GetIO().MouseDelta.x;
        if (dx != 0.0f) {
            float newTarget = sbW + dx;
            float maxTarget = std::max(150.0f, hostW * 0.40f);
            float minTarget = std::min(150.0f, maxTarget);
            newTarget = std::clamp(newTarget, minTarget, maxTarget);
            g_App.sidebarCollapsed = false;
            g_App.sidebarWidth = newTarget;
            g_App.sidebarTargetWidth = newTarget;
            if (hostW > 1.0f)
                g_App.sidebarFrac = std::clamp(newTarget / hostW, 0.12f, 0.40f);
            RequestRepaint();
        }
    } else if (splitHovered || ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        g_App.sidebarCollapsed = !g_App.sidebarCollapsed;
        g_App.sidebarDragging = false;
        RequestRepaint();
    }

    // --- Content panel (empty for now) ---
    ImGui::SameLine(0, 4);
    ImGui::BeginChild("##Content", ImVec2(0, 0),
        ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImVec2 cAvail = ImGui::GetContentRegionAvail();
    if (g_App.page == Page::Home)
        RenderHomepage(cAvail);
    else if (g_App.page == Page::MediaPlayer)
        RenderMediaPlayerPage(cAvail);
    else
        RenderSettingsPage(cAvail);
    ImGui::EndChild();

    ImGui::End(); // ##Host
}
