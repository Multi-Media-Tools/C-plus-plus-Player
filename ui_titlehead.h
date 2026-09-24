#pragma once
#include "imgui.h"

struct TitleHeadState {
    char searchFilter[128] = "";
    int  categoryFilter = 0; // 0: All, 1: Videos, 2: Downloads, 3: Documents, 4: Indexed
    bool filterChanged = false;
};

void RenderTitleHead(TitleHeadState& state, int totalVideos, int filteredVideos);
