#include "worker.h"
#include "app.h"  // RequestRepaint()
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cmath>

namespace {
std::thread        s_thread;
std::mutex         s_mutex;
std::atomic<bool>  s_running{ false };
std::atomic<bool>  s_stop{ false };
float              s_progress = 0.0f;      // guarded by s_mutex
std::string        s_status = "Idle";      // guarded by s_mutex

void WorkerThreadMain() {
    using namespace std::chrono_literals;
    {
        std::lock_guard<std::mutex> lk(s_mutex);
        s_progress = 0.0f;
        s_status = "Working...";
    }
    // Simulate ~8 seconds of chunked work. Each chunk is small so we
    // wake the UI often -> smooth 60 FPS progress bar, zero UI jank.
    for (int i = 0; i <= 200; ++i) {
        if (s_stop.load())
            break;
        // Fake CPU chunk (kept tiny on purpose; real work goes here).
        volatile double acc = 0.0;
        for (int k = 0; k < 20000; ++k)
            acc += std::sin((double)k) * std::cos((double)i);
        (void)acc;

        {
            std::lock_guard<std::mutex> lk(s_mutex);
            s_progress = (float)i / 200.0f;
        }
        RequestRepaint();  // wake idle loop for one VSync'd frame
        std::this_thread::sleep_for(40ms);  // ~25 updates/sec, VSync smooths it
    }
    {
        std::lock_guard<std::mutex> lk(s_mutex);
        s_status = s_stop.load() ? "Cancelled" : "Done";
        if (!s_stop.load())
            s_progress = 1.0f;
    }
    s_running.store(false);
    RequestRepaint();
}
} // namespace

void Worker_Start() {
    bool expected = false;
    if (!s_running.compare_exchange_strong(expected, true))
        return; // already running
    s_stop.store(false);
    if (s_thread.joinable())
        s_thread.join();
    s_thread = std::thread(WorkerThreadMain);
}

void Worker_Stop() {
    s_stop.store(true);
}

void Worker_Shutdown() {
    s_stop.store(true);
    if (s_thread.joinable())
        s_thread.join();
    s_running.store(false);
}

bool Worker_IsRunning() {
    return s_running.load();
}

float Worker_GetProgress() {
    std::lock_guard<std::mutex> lk(s_mutex);
    return s_progress;
}

std::string Worker_GetStatus() {
    std::lock_guard<std::mutex> lk(s_mutex);
    return s_status;
}
