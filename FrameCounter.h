#pragma once
#include <Stopwatch.h>

class FrameCounter {
    Stopwatch st;
public:
    float deltaTime = 0;
    size_t frameCount = 0;
    void tick() {
        deltaTime = st.tick();
        frameCount++;
    }
};
