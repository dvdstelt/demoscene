// Internal to the core library (core.cpp, glow.cpp): the shaders and buffers it shares per GL context
// (Ctx::shared<CoreGL>()). Parts use core.h.
#pragma once
#include "demo.h"

namespace jizz {

struct CoreGL {
    explicit CoreGL(Ctx& c);
    ~CoreGL();
    CoreGL(const CoreGL&) = delete;
    CoreGL& operator=(const CoreGL&) = delete;

    Shader rect;   // anti-aliased axis-aligned rectangle (aaRect, insetFrame)
    Shader fill;   // flat colour over the target (pageFade, timerFade)
    // glow.cpp
    Shader lines;  // 0x1145c strokes, instanced
    Shader scale;  // dst = src * k
    Shader gauss;  // one axis of the Gaussian that replaces the 2x2 box 0x11eb2
    Shader trail;  // 0x12111
    Shader glowFade;  // 0x11396
    GLuint lineVao = 0, lineVbo = 0;

private:
    void buildGlow();
};

}  // namespace jizz
