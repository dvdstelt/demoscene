// OpenGL 3.3 core loader: fetches every entry point listed in gl.h through SDL_GL_GetProcAddress.
#include "gl.h"

#include <SDL3/SDL.h>

namespace gl {
#define GL_DEFINE(ret, name, args) ret(GLAPIENTRY* name) args = nullptr;
GL_FUNCS(GL_DEFINE)
#undef GL_DEFINE

bool load() {
    bool ok = true;
#define GL_LOAD(ret, name, args)                                                     \
    name = reinterpret_cast<ret(GLAPIENTRY*) args>(SDL_GL_GetProcAddress("gl" #name)); \
    if (!name) { SDL_Log("missing GL function gl%s", #name); ok = false; }
    GL_FUNCS(GL_LOAD)
#undef GL_LOAD
    return ok;
}
}  // namespace gl
