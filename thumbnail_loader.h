#pragma once
#include <string>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

void ThumbnailLoader_Init();
void ThumbnailLoader_Shutdown();
void ThumbnailLoader_Update(ID3D11Device* device);

// Returns texture SRV if thumbnail is loaded, or nullptr if pending/placeholder.
// Optionally returns native aspect ratio (width / height) through outAspectRatio.
ID3D11ShaderResourceView* ThumbnailLoader_Get(const std::string& fullPath, ID3D11Device* device, float* outAspectRatio = nullptr);

// Request thumbnail extraction for a video file
void ThumbnailLoader_Request(const std::string& fullPath);
