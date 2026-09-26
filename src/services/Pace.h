// Pace.h - DwmFlush with a timeout, shared by every thread that paces itself on the compositor.
//
// DwmFlush waits for DWM's next frame and has no timeout of its own. On this machine stall.txt caught it
// blocking for 10-111 SECONDS (2026-09-26 - every HANG in errors.log that night was the render loop's
// DwmFlush). The slide workers' "three bad flushes and pace on the timer" hatch never fired, because a
// flush that does not return is never counted.
//
// One thread does the actual DwmFlush calls, only while somebody is waiting, and counts the frames it
// sees. PaceFlush(ms) waits for the next count or for ms, whichever comes first - the same pacing as a
// direct DwmFlush while the compositor keeps time, and a bounded wait when it does not. Safe to call from
// any number of threads at once.
#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <cstdint>

namespace pace_detail {
    inline std::mutex& M(){ static std::mutex m; return m; }
    inline std::condition_variable& CV(){ static std::condition_variable cv; return cv; }
    inline uint64_t g_frame=0;
    inline int  g_waiters=0;
    inline bool g_started=false;
    inline void Thread(){
        for(;;){
            { std::unique_lock<std::mutex> lk(M()); CV().wait(lk,[]{ return g_waiters>0; }); }
            DwmFlush();
            { std::lock_guard<std::mutex> lk(M()); g_frame++; }
            CV().notify_all();
        }
    }
}

// true = the compositor produced a frame; false = it did not within ms.
inline bool PaceFlush(DWORD ms=50){
    using namespace pace_detail;
    std::unique_lock<std::mutex> lk(M());
    if(!g_started){ g_started=true; std::thread(Thread).detach(); }
    const uint64_t f=g_frame;
    g_waiters++; CV().notify_all();
    bool ok=CV().wait_for(lk,std::chrono::milliseconds(ms),[&]{ return g_frame!=f; });
    g_waiters--;
    return ok;
}
