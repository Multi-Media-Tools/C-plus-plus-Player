#pragma once
#include "imgui.h"

// Media player page: displays and manages library of system video files.
void RenderMediaPlayerPage(ImVec2 size);

// Background scanner lifecycle
void MediaPlayer_Init();
void MediaPlayer_StartScan();
void MediaPlayer_StopScan();
void MediaPlayer_Shutdown();
bool MediaPlayer_IsScanning();
