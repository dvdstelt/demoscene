// The redrawn pictures (art.h): art/<name>.png decoded with stb_image.
#include "paper/art.h"

#include <SDL3/SDL.h>

#include <cstdio>

#include "demo.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_FAILURE_USERMSG
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#include "stb_image.h"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace paper {

bool loadArtGrey(const Ctx& c, const std::string& name, GreyImage* out) {
    if (!c.art()) return false;
    const std::string path = c.artDir + "/" + name + ".png";
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fclose(f);
    } else {
        SDL_Log("art: no %s: using the original's pixels", path.c_str());
        return false;
    }
    int w = 0, h = 0, ch = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &ch, 1);
    if (!px) {
        SDL_Log("art: cannot read %s (%s): using the original's pixels", path.c_str(),
                stbi_failure_reason() ? stbi_failure_reason() : "unknown error");
        return false;
    }
    out->w = w;
    out->h = h;
    out->px.assign(px, px + size_t(w) * h);
    stbi_image_free(px);
    return true;
}

}  // namespace paper
