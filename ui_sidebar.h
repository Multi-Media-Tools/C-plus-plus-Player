#pragma once
#include "imgui.h"

// Draws the left sidebar. `size` is the current animated sidebar rect.
// Handles: Home chip at top, Settings button pinned at bottom,
// collapse/expand toggle. Resizing itself is handled via the splitter
// in app.cpp (drag to change % of window).
void RenderSidebar(ImVec2 size);

// True while expand/collapse animation is still running.
bool Sidebar_IsAnimating();
