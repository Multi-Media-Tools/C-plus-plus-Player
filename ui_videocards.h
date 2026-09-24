#pragma once
#include "imgui.h"
#include "video_finder.h"
#include <vector>
#include <string>

void RenderVideoCards(
    const std::vector<VideoItem>& allVideos,
    const std::vector<int>& filteredIndices,
    int& selectedIndex,
    std::string& selectedPath,
    ImVec2 availableSize
);
