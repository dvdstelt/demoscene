// 0:41 - 0:55  Aap's flat-colour stripe art: the wipeout stripes through strong 2D warps, a new
// composition every two beats, fine line webs and an inset window.
#include <cmath>

#include "../fx.h"

namespace {
WarpParams pick(int k, float local, bool inset) {
    WarpParams w;
    float h1 = hash1(k * 12.9898f + (inset ? 78.233f : 0.0f));
    float h2 = hash1(k * 39.3468f + 11.135f + (inset ? 5.1f : 0.0f));
    float h3 = hash1(k * 73.156f + 2.71f + (inset ? 9.3f : 0.0f));
    static const int modes[] = {5, 5, 1, 5, 0, 5, 1, 5};
    w.mode = modes[(k * 3 + (inset ? 5 : 0)) % 8];
    w.cx = 0.1f + 0.8f * h1;
    w.cy = 0.1f + 0.8f * h2;
    w.zoom = w.mode == 5 ? 0.18f + 0.3f * h3 : (w.mode == 1 ? 0.55f + 0.5f * h3 : 0.5f);
    w.strength = w.mode == 0 ? 0.35f + 0.6f * h2 : 0.7f + h1;
    w.angle = (h3 - 0.5f) * 1.2f;
    w.time = local * (w.mode == 1 ? 0.08f : 0.18f) + h1 * 3.0f;
    w.fog = w.mode == 1 ? 1.0f : 0.25f;
    w.seed = k + (inset ? 0.5f : 0.0f);
    w.lines = inset ? 0.15f : 0.3f;
    w.bright = 1.0f;
    if (inset) w.tint = Vec3(0.85f, 1.05f, 1.1f);
    return w;
}

struct Aap : Scene {
    void render(Ctx& c, float, float) override {
        const float t = VT(c.T);
        const float seg = 0.855f;
        int k = int(std::floor((t - 40.95f) / seg));
        float local = t - 40.95f - k * seg;
        Texture& stripes = c.tex("wipeout");

        setBlend(Blend::None);
        WarpParams m = pick(k, local, false);
        drawWarp(c, stripes, m);

        // bloom flash on the vanishing point at the start of each composition
        float flash = std::exp(-local * 6.0f) * (k % 3 == 0 ? 1.0f : 0.45f);
        if (flash > 0.01f) {
            setBlend(Blend::Add);
            c.drawImage(c.tex("phong1"), m.cx - 0.25f, m.cy - 0.25f * c.aspect, 0.5f, 0.5f * c.aspect, flash * 1.5f, 0, 0, 1, 1, 0,
                        Vec3(1, 1, 1), false, true);
        }

        // inset window with a white border
        float ix = 0.05f + 0.55f * hash1(k * 5.17f + 1.0f), iy = 0.08f + 0.5f * hash1(k * 3.91f + 2.0f);
        float iw = 0.34f, ih = 0.34f;
        drawBorder(c, ix, iy, iw, ih);
        setBlend(Blend::None);
        drawWarp(c, stripes, pick(k + 101, local, true), ix, iy, iw, ih);

        // fade from the white of the intro, and out at the end
        c.flash(1.0f - smoothstep(40.8f, 41.25f, t), Vec3(1, 1, 1));
        c.flash(smoothstep(54.3f, 54.75f, t), Vec3(1, 1, 1));
    }

    void drawBorder(Ctx& c, float x, float y, float w, float h) {
        float bw = 0.0035f, bh = bw * c.aspect;
        setBlend(Blend::Alpha);
        Texture& t = c.white();
        c.drawImage(t, x - bw, y - bh, w + 2 * bw, bh, 1.0f);
        c.drawImage(t, x - bw, y + h, w + 2 * bw, bh, 1.0f);
        c.drawImage(t, x - bw, y, bw, h, 1.0f);
        c.drawImage(t, x + w, y, bw, h, 1.0f);
    }

};
REGISTER_SCENE(Aap, "aap");
}  // namespace
