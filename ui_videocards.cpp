#include "ui_videocards.h"
#include "ui_descriptioncard.h"
#include "thumbnail_loader.h"
#include "app.h"
#include "imgui_internal.h"

#include <d3d11.h>
#include <cmath>
#include <algorithm>

static void DrawFilmstripPlaceholder(ImDrawList* drawList, ImVec2 pMin, ImVec2 pMax) {
    float boxW = pMax.x - pMin.x;
    float boxH = pMax.y - pMin.y;
    float cx = pMin.x + boxW * 0.5f;
    float cy = pMin.y + boxH * 0.5f;

    // Outer film frame
    float iconW = 48.0f;
    float iconH = 52.0f;
    ImVec2 iMin = ImVec2(cx - iconW * 0.5f, cy - iconH * 0.5f);
    ImVec2 iMax = ImVec2(cx + iconW * 0.5f, cy + iconH * 0.5f);
    ImU32 iconColor = IM_COL32(190, 190, 195, 235);

    drawList->AddRect(iMin, iMax, iconColor, 8.0f, 0, 3.0f);

    // Left and right vertical sprocket holes (4 on each side)
    float sprocW = 3.5f;
    float sprocH = 5.5f;
    float sprocRound = 1.5f;
    float startY = iMin.y + 8.0f;
    float stepY = 10.0f;

    for (int i = 0; i < 4; ++i) {
        float y0 = startY + i * stepY;
        float y1 = y0 + sprocH;

        // Left sprocket
        float lx0 = iMin.x + 4.5f;
        drawList->AddRectFilled(ImVec2(lx0, y0), ImVec2(lx0 + sprocW, y1), iconColor, sprocRound);

        // Right sprocket
        float rx1 = iMax.x - 4.5f;
        drawList->AddRectFilled(ImVec2(rx1 - sprocW, y0), ImVec2(rx1, y1), iconColor, sprocRound);
    }
}

// Splits filename into up to 2 lines for card display
static void FormatCardTitle(const std::string& name, size_t maxChars, std::string& line1, std::string& line2) {
    if (name.length() <= maxChars) {
        line1 = name;
        line2 = "";
        return;
    }

    line1 = name.substr(0, maxChars);
    std::string rest = name.substr(maxChars);
    if (rest.length() > maxChars) {
        line2 = rest.substr(0, maxChars - 3) + "...";
    } else {
        line2 = rest;
    }
}

void RenderVideoCards(
    const std::vector<VideoItem>& allVideos,
    const std::vector<int>& filteredIndices,
    int& selectedIndex,
    std::string& selectedPath,
    ImVec2 availableSize
) {
    ID3D11Device* device = App_GetD3DDevice();

    // --- Responsive Grid Layout: Minimum width 205px, expands to eliminate side gap ---
    const float minCardW = 205.0f;
    const float spacingX = 14.0f;
    const float spacingY = 18.0f;
    const float padding = 5.0f;

    float rawAvailW = ImGui::GetContentRegionAvail().x;
    float availW = rawAvailW - 4.0f; // account for scrollbar margin
    if (availW < minCardW) availW = minCardW;

    int cols = (int)std::floor((availW + spacingX) / (minCardW + spacingX));
    if (cols < 1) cols = 1;

    // Distribute all remaining space equally among columns so cards fill 100% of the row
    float cardW = std::floor((availW - (cols - 1) * spacingX) / (float)cols);
    float thumbH = std::floor(cardW * 0.80f);
    float cardH = thumbH + 74.0f;

    const float cardRounding = 14.0f;
    const float thumbBoxRounding = 10.0f;
    const float imageRounding = 8.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacingX, spacingY));

    // Scrollable container for cards
    float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y + 14.0f;
    float scrollHeight = availableSize.y - footerHeight;
    if (scrollHeight < 150.0f) scrollHeight = 150.0f;

    ImGui::BeginChild("##VideoCardsScroll", ImVec2(0, scrollHeight), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    int totalItems = (int)filteredIndices.size();
    int hoveredSnapshotIdx = -1;
    ImVec2 hoveredCardPos(0, 0);
    ImVec2 hoveredCardSize(0, 0);

    ImGuiViewport* vp = ImGui::GetMainViewport();
    float vpLeft = vp->WorkPos.x + 10.0f;
    float vpRight = vp->WorkPos.x + vp->WorkSize.x - 10.0f;
    float vpTop = vp->WorkPos.y + 10.0f;
    float vpBottom = vp->WorkPos.y + vp->WorkSize.y - 10.0f;

    size_t charsPerLine = std::max<size_t>(18, (size_t)(cardW / 8.5f));

    for (int i = 0; i < totalItems; ++i) {
        int snapshotIdx = filteredIndices[i];
        const auto& video = allVideos[snapshotIdx];
        bool isSelected = (selectedIndex == snapshotIdx);

        ImGui::PushID(i);
        ImVec2 cardPos = ImGui::GetCursorScreenPos();
        ImVec2 cardMax = ImVec2(cardPos.x + cardW, cardPos.y + cardH);

        // Invisible button to handle clicks and hover states
        bool clicked = ImGui::InvisibleButton("##cardBtn", ImVec2(cardW, cardH));
        bool hovered = ImGui::IsItemHovered();

        if (hovered) {
            hoveredSnapshotIdx = snapshotIdx;
            hoveredCardPos = cardPos;
            hoveredCardSize = ImVec2(cardW, cardH);
        }

        if (clicked) {
            selectedIndex = snapshotIdx;
            selectedPath = video.fullPath;
            RequestRepaint();
        }

        // --- Render Card Background & Border ---
        ImU32 cardBgCol = hovered ? IM_COL32(28, 28, 32, 240) : IM_COL32(18, 18, 20, 210);
        ImU32 cardBorderCol = isSelected
            ? IM_COL32(145, 145, 155, 255)
            : (hovered ? IM_COL32(75, 75, 82, 255) : IM_COL32(38, 38, 42, 255));
        float borderThickness = isSelected ? 2.0f : 1.0f;

        drawList->AddRectFilled(cardPos, cardMax, cardBgCol, cardRounding);
        drawList->AddRect(cardPos, cardMax, cardBorderCol, cardRounding, 0, borderThickness);

        // --- Thumbnail Box ---
        ImVec2 thumbMin = ImVec2(cardPos.x + padding, cardPos.y + padding);
        ImVec2 thumbMax = ImVec2(cardPos.x + cardW - padding, cardPos.y + thumbH);
        float boxW = thumbMax.x - thumbMin.x;
        float boxH = thumbMax.y - thumbMin.y;

        // Dark box backdrop for letterboxing
        drawList->AddRectFilled(thumbMin, thumbMax, IM_COL32(10, 10, 12, 255), thumbBoxRounding);

        // Request or query thumbnail texture from FFmpeg loader with native aspect ratio
        float nativeAspect = 1.0f;
        ID3D11ShaderResourceView* srv = ThumbnailLoader_Get(video.fullPath, device, &nativeAspect);

        if (srv != nullptr) {
            // Aspect-fit calculation: preserve video proportions without stretching
            float boxAspect = boxW / boxH;
            float renderW = boxW;
            float renderH = boxH;

            if (nativeAspect > boxAspect) {
                renderH = boxW / nativeAspect;
            } else {
                renderW = boxH * nativeAspect;
            }

            float offX = (boxW - renderW) * 0.5f;
            float offY = (boxH - renderH) * 0.5f;
            ImVec2 fitMin = ImVec2(thumbMin.x + offX, thumbMin.y + offY);
            ImVec2 fitMax = ImVec2(fitMin.x + renderW, fitMin.y + renderH);

            // AddImageRounded cures sharp image corners, smoothly matching the box
            drawList->AddImageRounded((ImTextureID)srv, fitMin, fitMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, imageRounding);
        } else {
            // Draw filmstrip placeholder icon
            DrawFilmstripPlaceholder(drawList, thumbMin, thumbMax);
        }

        // Inner border for thumbnail container
        drawList->AddRect(thumbMin, thumbMax, IM_COL32(45, 45, 50, 255), thumbBoxRounding, 0, 1.0f);

        // --- Video Name (Two Lines, Clean Typography) ---
        std::string line1, line2;
        FormatCardTitle(video.fileName, charsPerLine, line1, line2);

        float textX = cardPos.x + padding + 4.0f;
        float textY = thumbMax.y + 8.0f;
        ImU32 textColor = isSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(225, 225, 230, 255);

        drawList->AddText(ImVec2(textX, textY), textColor, line1.c_str());
        if (!line2.empty()) {
            drawList->AddText(ImVec2(textX, textY + 16.0f), textColor, line2.c_str());
        }

        ImGui::PopID();

        // Horizontal grid wrapping
        if ((i + 1) % cols != 0 && (i + 1) < totalItems) {
            ImGui::SameLine();
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(); // ItemSpacing

    // --- Footer Bar (Shows Hovered or Selected Info Real-Time) ---
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    int displayIdx = (hoveredSnapshotIdx >= 0) ? hoveredSnapshotIdx : selectedIndex;
    const char* prefix = (hoveredSnapshotIdx >= 0) ? "Hovered" : "Selected";

    if (displayIdx >= 0 && displayIdx < (int)allVideos.size()) {
        const auto& item = allVideos[displayIdx];
        ImGui::TextColored(ImVec4(0.92f, 0.92f, 0.95f, 1.0f), "%s: %s", prefix, item.fileName.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", item.sizeFormatted.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("| %s", item.fullPath.c_str());
    } else {
        ImGui::TextDisabled("Hover or click a video card to view its details.");
    }

    // --- Render Acrylic Blurred Description Card for Hovered Video ---
    if (hoveredSnapshotIdx >= 0 && hoveredSnapshotIdx < (int)allVideos.size()) {
        RenderDescriptionCard(
            allVideos[hoveredSnapshotIdx],
            hoveredCardPos,
            hoveredCardSize,
            ImVec2(vpLeft, vpTop),
            ImVec2(vpRight, vpBottom)
        );
    }
}
