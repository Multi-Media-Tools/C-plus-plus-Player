#include "ui_sidebar.h"
#include "app.h"
#include <cmath>

static bool IsCollapsedMode(float width) {
    return width < 120.0f;
}

struct ButtonAnimState {
    ImVec4 currentBg     = ImVec4(15.0f / 255.0f, 15.0f / 255.0f, 15.0f / 255.0f, 0.0f);
    ImVec4 currentBorder = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    bool   initialized   = false;
};

static ButtonAnimState s_btnHome;
static ButtonAnimState s_btnSettings;
static ButtonAnimState s_btnToggle;
static bool s_buttonsFading = false;

static inline ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t
    );
}

static inline bool ColorNear(const ImVec4& a, const ImVec4& b, float eps = 0.002f) {
    return std::fabs(a.x - b.x) <= eps &&
           std::fabs(a.y - b.y) <= eps &&
           std::fabs(a.z - b.z) <= eps &&
           std::fabs(a.w - b.w) <= eps;
}

bool Sidebar_IsAnimating() {
    bool widthMoving = std::fabs(g_App.sidebarWidth - g_App.sidebarTargetWidth) > 0.5f;
    return widthMoving || s_buttonsFading;
}

static bool ChipButton(ButtonAnimState& anim, const char* id, const char* label, ImVec2 size, bool active) {
    // Exact colors:
    // Unselected idle: transparent faint
    // Unselected hover: 202020 (32, 32, 32) -> clear visible sleek grey hover
    // Selected idle: 2c2c2c (44, 44, 44)
    // Selected hover: 545454 (84, 84, 84)
    const ImVec4 kColUnselectedIdle  = ImVec4(15.0f / 255.0f, 15.0f / 255.0f, 15.0f / 255.0f, 0.0f);
    const ImVec4 kColUnselectedHover = ImVec4(32.0f / 255.0f, 32.0f / 255.0f, 32.0f / 255.0f, 1.0f); // 202020
    const ImVec4 kColSelectedIdle    = ImVec4(44.0f / 255.0f, 44.0f / 255.0f, 44.0f / 255.0f, 1.0f); // 2c2c2c
    const ImVec4 kColSelectedHover   = ImVec4(84.0f / 255.0f, 84.0f / 255.0f, 84.0f / 255.0f, 1.0f); // 545454
    const ImVec4 kColSelectedPress   = ImVec4(36.0f / 255.0f, 36.0f / 255.0f, 36.0f / 255.0f, 1.0f);
    const ImVec4 kColUnselectedPress = ImVec4(24.0f / 255.0f, 24.0f / 255.0f, 24.0f / 255.0f, 1.0f);

    const ImVec4 kBorderSelected     = ImVec4(84.0f / 255.0f, 84.0f / 255.0f, 84.0f / 255.0f, 0.50f);
    const ImVec4 kBorderUnselected   = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    if (!anim.initialized) {
        anim.currentBg = active ? kColSelectedIdle : kColUnselectedIdle;
        anim.currentBorder = active ? kBorderSelected : kBorderUnselected;
        anim.initialized = true;
    }

    ImVec2 pMin = ImGui::GetCursorScreenPos();
    ImVec2 pMax = ImVec2(pMin.x + size.x, pMin.y + size.y);
    bool isHovering = ImGui::IsMouseHoveringRect(pMin, pMax) && ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    bool isMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool isPressed = isHovering && isMouseDown;

    ImVec4 targetBg;
    if (isPressed) {
        targetBg = active ? kColSelectedPress : kColUnselectedPress;
    } else if (isHovering) {
        targetBg = active ? kColSelectedHover : kColUnselectedHover;
    } else {
        targetBg = active ? kColSelectedIdle : kColUnselectedIdle;
    }

    ImVec4 targetBorder = active ? (isHovering ? ImVec4(100.0f / 255.0f, 100.0f / 255.0f, 100.0f / 255.0f, 0.65f) : kBorderSelected) : kBorderUnselected;

    float dt = ImGui::GetIO().DeltaTime;
    if (dt <= 0.0f || dt > 0.1f) dt = 0.016f;
    float factor = 1.0f - std::exp(-15.0f * dt);

    anim.currentBg = LerpColor(anim.currentBg, targetBg, factor);
    anim.currentBorder = LerpColor(anim.currentBorder, targetBorder, factor);

    bool bgDone = ColorNear(anim.currentBg, targetBg, 0.002f);
    bool borderDone = ColorNear(anim.currentBorder, targetBorder, 0.002f);

    if (bgDone) anim.currentBg = targetBg;
    if (borderDone) anim.currentBorder = targetBorder;

    if (!bgDone || !borderDone) {
        s_buttonsFading = true;
        RequestRepaint();
    }

    ImGui::PushStyleColor(ImGuiCol_Button, anim.currentBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, anim.currentBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, isPressed ? targetBg : anim.currentBg);
    ImGui::PushStyleColor(ImGuiCol_Border, anim.currentBorder);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

    bool pressed = ImGui::Button(label, size);

    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(4);
    (void)id;
    return pressed;
}

static bool AnimatedToggleButton(ButtonAnimState& anim, const char* label, ImVec2 size) {
    const ImVec4 kColIdle  = ImVec4(15.0f / 255.0f, 15.0f / 255.0f, 15.0f / 255.0f, 0.0f);
    const ImVec4 kColHover = ImVec4(32.0f / 255.0f, 32.0f / 255.0f, 32.0f / 255.0f, 1.0f); // 202020
    const ImVec4 kColPress = ImVec4(24.0f / 255.0f, 24.0f / 255.0f, 24.0f / 255.0f, 1.0f);

    if (!anim.initialized) {
        anim.currentBg = kColIdle;
        anim.initialized = true;
    }

    ImVec2 pMin = ImGui::GetCursorScreenPos();
    ImVec2 pMax = ImVec2(pMin.x + size.x, pMin.y + size.y);
    bool isHovering = ImGui::IsMouseHoveringRect(pMin, pMax) && ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    bool isPressed = isHovering && ImGui::IsMouseDown(ImGuiMouseButton_Left);

    ImVec4 targetBg = isPressed ? kColPress : (isHovering ? kColHover : kColIdle);

    float dt = ImGui::GetIO().DeltaTime;
    if (dt <= 0.0f || dt > 0.1f) dt = 0.016f;
    float factor = 1.0f - std::exp(-15.0f * dt);

    anim.currentBg = LerpColor(anim.currentBg, targetBg, factor);

    bool bgDone = ColorNear(anim.currentBg, targetBg, 0.002f);
    if (bgDone) anim.currentBg = targetBg;
    else {
        s_buttonsFading = true;
        RequestRepaint();
    }

    ImGui::PushStyleColor(ImGuiCol_Button, anim.currentBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, anim.currentBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, isPressed ? targetBg : anim.currentBg);

    bool pressed = ImGui::Button(label, size);

    ImGui::PopStyleColor(3);
    return pressed;
}

void RenderSidebar(ImVec2 size) {
    s_buttonsFading = false;
    const bool collapsed = IsCollapsedMode(size.x);
    const float pad = 10.0f;
    const float btnH = 40.0f;
    const float btnW = size.x - pad * 2.0f > 0 ? size.x - pad * 2.0f : 0;

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));

    // --- Top row: collapse toggle ---
    {
        const char* toggleLabel = collapsed ? ">>" : "<<";
        if (collapsed) {
            if (AnimatedToggleButton(s_btnToggle, toggleLabel, ImVec2(btnW, 32))) {
                g_App.sidebarCollapsed = false;
                RequestRepaint();
            }
        } else {
            if (AnimatedToggleButton(s_btnToggle, toggleLabel, ImVec2(40, 32))) {
                g_App.sidebarCollapsed = true;
                RequestRepaint();
            }
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Menu");
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Home chip at TOP ---
    {
        bool active = (g_App.page == Page::Home);
        const char* label = collapsed ? "H" : "  Home";
        if (ChipButton(s_btnHome, "##home", label, ImVec2(btnW, btnH), active)) {
            g_App.page = Page::Home;
            RequestRepaint();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Home");
    }

    // --- Settings pinned at BOTTOM ---
    {
        float curY = ImGui::GetCursorPosY();
        float bottomY = size.y - (btnH + pad + 8.0f);
        if (bottomY > curY)
            ImGui::SetCursorPosY(bottomY);

        bool active = (g_App.page == Page::Settings);
        const char* label = collapsed ? "S" : "  Settings";
        if (ChipButton(s_btnSettings, "##settings", label, ImVec2(btnW, btnH), active)) {
            g_App.page = Page::Settings;
            RequestRepaint();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Settings");
    }

    ImGui::PopStyleVar(2);
}
