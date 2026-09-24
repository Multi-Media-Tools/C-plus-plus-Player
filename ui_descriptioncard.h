#pragma once
#include "video_finder.h"
#include "imgui.h"

struct ID3D11Device;

// Lifecycle for description card resources (shaders, blur textures, noise texture)
void DescriptionCard_Init(ID3D11Device* device);
void DescriptionCard_Shutdown();

// Renders the boundary-aware acrylic blurred description card for a hovered video item
void RenderDescriptionCard(
    const VideoItem& video,
    ImVec2 cardPos,
    ImVec2 cardSize,
    ImVec2 viewportMin,
    ImVec2 viewportMax
);
