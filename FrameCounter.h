#pragma once
#include <Stopwatch.h>

class FrameCounter {
    Stopwatch st;
    double totalFrameTime_ = 0;
    double deltaTime_ = 0;
    size_t frameCount_ = 0;
public:
    void tick() {
        const double now = st.ping();
        deltaTime_ = now - totalFrameTime_;
        frameCount_++;
        totalFrameTime_ = now;
    }
    size_t frameCount() const { return frameCount_; }
    double frameTimeTotal() const { return totalFrameTime_; }
    double frameTimeAvg() const { return totalFrameTime_ / frameCount_; }
    double fpsAvg() const { return 1000 * frameCount_ / totalFrameTime_; }
};
