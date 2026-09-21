#pragma once
#include <string>

// Simulated background processing. Runs on its own std::thread so the
// UI thread stays free to hit 60 FPS. Thread-safe: call from UI thread.
void Worker_Start();          // start fake job (no-op if already running)
void Worker_Stop();           // request stop
void Worker_Shutdown();       // join thread on app exit
bool Worker_IsRunning();
float Worker_GetProgress();   // 0..1
std::string Worker_GetStatus();
