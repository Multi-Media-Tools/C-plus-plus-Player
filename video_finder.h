#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct VideoItem {
    std::string fileName;
    std::string fullPath;
    std::string category;     // "Videos", "Downloads", "Documents", "Indexed"
    uintmax_t   fileSizeBytes = 0;
    std::string sizeFormatted;
};

void VideoFinder_Init();
void VideoFinder_StartScan();
void VideoFinder_StopScan();
void VideoFinder_Shutdown();
bool VideoFinder_IsScanning();

// Returns true if a new snapshot was copied (version changed)
bool VideoFinder_GetSnapshot(uint64_t& inOutVersion, std::vector<VideoItem>& outSnapshot);
