#include "ui_titlehead.h"
#include "video_finder.h"
#include "ffmpeg_wrapper.h"
#include "app.h"

#include <string>

void RenderTitleHead(TitleHeadState& state, int totalVideos, int filteredVideos) {
    state.filterChanged = false;
    bool scanning = VideoFinder_IsScanning();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

    // --- Row 1: Title + FFmpeg Version + Scan Control Button ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Media Player");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    static std::string s_ffVer = std::string("FFmpeg v") + av_version_info();
    ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.58f, 1.0f), "%s", s_ffVer.c_str());
    ImGui::EndGroup();

    float availW = ImGui::GetContentRegionAvail().x;
    ImGui::SameLine(availW - 190.0f > 150.0f ? availW - 190.0f : 150.0f);

    if (scanning) {
        if (ImGui::Button("Stop Scan", ImVec2(90, 26))) {
            VideoFinder_StopScan();
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Scanning...");
    } else {
        if (ImGui::Button("Rescan", ImVec2(80, 26))) {
            VideoFinder_StartScan();
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Ready");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Row 2: Search Input & Category Filters ---
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Filter:");
    ImGui::SameLine();

    ImGui::SetNextItemWidth(230.0f);
    if (ImGui::InputTextWithHint("##search", "Search videos by name...", state.searchFilter, sizeof(state.searchFilter))) {
        state.filterChanged = true;
    }

    ImGui::SameLine(0, 16);
    ImGui::TextUnformatted("Folder:");
    ImGui::SameLine();

    const char* categories[] = { "All", "Videos", "Downloads", "Documents", "Indexed" };
    for (int i = 0; i < 5; ++i) {
        bool selected = (state.categoryFilter == i);
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(44.0f / 255.0f, 44.0f / 255.0f, 44.0f / 255.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(84.0f / 255.0f, 84.0f / 255.0f, 84.0f / 255.0f, 0.8f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(15.0f / 255.0f, 15.0f / 255.0f, 15.0f / 255.0f, 0.3f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        }

        if (ImGui::Button(categories[i])) {
            state.categoryFilter = i;
            state.filterChanged = true;
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
    }

    ImGui::NewLine();
    ImGui::PopStyleVar(); // ItemSpacing

    // Count summary
    ImGui::TextDisabled("Found %d video file%s (Showing %d)",
        totalVideos,
        totalVideos == 1 ? "" : "s",
        filteredVideos
    );

    ImGui::Spacing();
}
