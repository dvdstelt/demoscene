// Small GL helpers shared by the core library's passes (no original routine).
#pragma once
#include "gl.h"

namespace paper {

// Saves the bound framebuffer and the viewport, restores them when it goes out of scope (the core passes render
// into their own temporaries and must leave the caller's target bound).
struct GlState {
    static constexpr GLenum kFramebufferBinding = 0x8CA6, kViewport = 0x0BA2;
    GLint fbo = 0, vp[4] = {0, 0, 0, 0};
    GlState() {
        gl::GetIntegerv(kFramebufferBinding, &fbo);
        gl::GetIntegerv(kViewport, vp);
    }
    ~GlState() {
        gl::BindFramebuffer(GL_FRAMEBUFFER, GLuint(fbo));
        gl::Viewport(vp[0], vp[1], vp[2], vp[3]);
    }
    GlState(const GlState&) = delete;
    GlState& operator=(const GlState&) = delete;
};

}  // namespace paper
