#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <atomic>
#include <string>
#include <windows.h>
#include "imgui.h"

// Defined in main.cpp, shared with all UI/worker files.
extern HANDLE         g_hWakeEvent;
extern std::atomic<bool> g_NeedRepaint;

// Wake the idle loop so it renders one more VSync'd frame.
void RequestRepaint();

enum class Page : int {
    Home = 0,
    Settings = 1,
};

struct AppState {
    Page  page = Page::Home;

    // Sidebar sizing. Widths are in pixels; frac is % of host width.
    bool  sidebarCollapsed = false;
    float sidebarFrac = 0.20f;        // expanded % of window (resizable via splitter)
    float sidebarWidth = 260.0f;      // animated current width
    float sidebarTargetWidth = 260.0f;// animated target width
    float collapsedWidth = 64.0f;

    bool  sidebarDragging = false;    // true while dragging the splitter

    bool  vsync = true;               // Present(1,0) when true, Present(0,0) when false
};

extern AppState g_App;

// Called once after ImGui context creation.
void App_Init();

// Called once per RENDERED frame (not once per OS message).
void App_RenderFrame();

// True while sidebar is animating or background work is active.
// The idle loop uses this to switch between INFINITE wait (0% CPU)
// and 16ms wait (~60 FPS).
bool App_WantsContinuousFrames();

// Dark theme + rounded widgets.
void App_ApplyDarkTheme();

// Theme accent color: sleek midnight grey (instead of Windows / blue).
// Call App_RefreshAccentColor() to refresh or re-apply the grey theme.
void App_RefreshAccentColor();
ImVec4 App_GetAccentColor();
ImVec4 App_GetAccentHovered();
ImVec4 App_GetAccentActive();

