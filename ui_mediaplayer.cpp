#include "ui_mediaplayer.h"
#include "ui_descriptioncard.h"
#include "video_finder.h"
#include "thumbnail_loader.h"
#include "ui_titlehead.h"
#include "ui_videocards.h"
#include "app.h"

#include <vector>
#include <string>
#include <algorithm>

static std::vector<VideoItem>   s_uiVideos;
static std::vector<int>         s_filteredIndices;
static uint64_t                 s_lastVersion = 0;
static TitleHeadState           s_titleState;
static int                      s_selectedIndex = -1;
static std::string              s_selectedPath = "";
static bool                     s_needsRefilter = true;

void MediaPlayer_Init() {
    ThumbnailLoader_Init();
    VideoFinder_Init();
    DescriptionCard_Init(App_GetD3DDevice());
}

void MediaPlayer_StartScan() {
    VideoFinder_StartScan();
}

void MediaPlayer_StopScan() {
    VideoFinder_StopScan();
}

void MediaPlayer_Shutdown() {
    VideoFinder_Shutdown();
    ThumbnailLoader_Shutdown();
    DescriptionCard_Shutdown();
}

bool MediaPlayer_IsScanning() {
    return VideoFinder_IsScanning();
}

static void RefilterVideos() {
    s_filteredIndices.clear();

    std::string searchLower = s_titleState.searchFilter;
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), [](unsigned char c) {
        return (char)::tolower(c);
    });

    const char* catFilter = nullptr;
    if (s_titleState.categoryFilter == 1) catFilter = "Videos";
    else if (s_titleState.categoryFilter == 2) catFilter = "Downloads";
    else if (s_titleState.categoryFilter == 3) catFilter = "Documents";
    else if (s_titleState.categoryFilter == 4) catFilter = "Indexed";

    for (int i = 0; i < (int)s_uiVideos.size(); ++i) {
        const auto& item = s_uiVideos[i];

        if (catFilter != nullptr && item.category != catFilter)
            continue;

        if (!searchLower.empty()) {
            std::string nameLower = item.fileName;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), [](unsigned char c) {
                return (char)::tolower(c);
            });
            if (nameLower.find(searchLower) == std::string::npos)
                continue;
        }

        s_filteredIndices.push_back(i);
    }

    s_needsRefilter = false;
}

void RenderMediaPlayerPage(ImVec2 size) {
    // 1. Thread-safe snapshot synchronization with video finder
    if (VideoFinder_GetSnapshot(s_lastVersion, s_uiVideos)) {
        s_needsRefilter = true;
    }

    // 2. Render TitleHead (header, FFmpeg info, search input, folder filters, rescan controls)
    RenderTitleHead(s_titleState, (int)s_uiVideos.size(), (int)s_filteredIndices.size());

    if (s_titleState.filterChanged || s_needsRefilter) {
        RefilterVideos();
    }

    // 3. Calculate remaining vertical space and render horizontal video cards
    ImVec2 cAvail = ImGui::GetContentRegionAvail();
    RenderVideoCards(s_uiVideos, s_filteredIndices, s_selectedIndex, s_selectedPath, cAvail);
}
