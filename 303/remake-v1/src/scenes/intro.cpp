// 0:00 - 0:41  Neuron tunnel with radial blur, "statix vic acme" credits, "303" strip.
#include <cmath>

#include "../fx.h"

namespace {
struct Intro : Scene {
    void render(Ctx& c, float, float) override {
        const float t = VT(c.T);
        // vanishing point wanders (sometimes off screen, which gives the sideways streaks)
        float cx = 0.5f + 0.42f * std::sin(0.23f * t + 0.6f) + 0.12f * std::sin(0.71f * t);
        float cy = 0.5f + 0.50f * std::sin(0.17f * t + 2.3f) + 0.10f * std::sin(0.53f * t + 1.0f);
        if (t > 30.0f) { cx = mix(cx, 0.5f, smoothstep(30.0f, 33.0f, t)); cy = mix(cy, 0.5f, smoothstep(30.0f, 33.0f, t)); }
        float bright = smoothstep(1.0f, 12.0f, t) * 0.85f + 0.15f * smoothstep(1.0f, 4.0f, t);
        float beat = beatPulse(t - 0.10f, kBeat * 2, 5.0f);
        float burst = std::exp(-std::pow((t - 14.0f) / 0.9f, 2.0f)) + std::exp(-std::pow((t - 34.0f) / 1.2f, 2.0f));

        // 1) source layer: tunnel + credits image
        Target& src = c.scratch[0];
        src.bind();
        gl::ClearColor(0, 0, 0, 1);
        gl::Clear(GL_COLOR_BUFFER_BIT);
        setBlend(Blend::None);
        WarpParams w;
        w.mode = 0;
        w.cx = cx;
        w.cy = cy;
        w.time = t * 0.55f;
        w.zoom = 1.0f;
        w.angle = 0.15f * t;
        w.strength = 0.55f;
        w.fog = 0.3f;
        w.contrast = 2.2f;
        w.bright = bright * (1.5f + 0.5f * beat + 0.6f * burst);
        drawWarp(c, c.tex("neuron"), w);

        // credits flying in through the blur ("opening1": statix / vic / acme)
        if (t > 13.5f && t < 28.5f) {
            float k = smoothstep(13.5f, 19.0f, t);
            float out = smoothstep(25.5f, 28.5f, t);
            float s = mix(0.15f, 1.05f, k) * (1.0f + 1.8f * out);
            float a = smoothstep(13.5f, 15.0f, t) * (1.0f - out);
            float rot = 0.25f * std::sin(t * 0.9f) * (1.0f - k) + out * 1.4f;
            setBlend(Blend::Add);
            float wdt = 0.75f * s, hgt = 0.75f * s * c.aspect;
            for (int g = 0; g < 3; g++) {  // a few ghost copies like the original's echo
                float gs = 1.0f + g * 0.06f * (1.0f + 4.0f * out);
                c.drawImage(c.tex("opening1"), 0.5f - wdt * gs * 0.5f + 0.02f * std::sin(t * 2.1f + g),
                            0.47f - hgt * gs * 0.5f, wdt * gs, hgt * gs, a * (g == 0 ? 0.95f : 0.35f), 0, 0, 1, 1,
                            rot * (1.0f + 0.5f * g), Vec3(0.85f, 1.0f, 0.95f), true, true);
            }
        }

        // 2) radial blur toward the vanishing point
        c.out->bind();
        setBlend(Blend::None);
        float strength = 0.22f + 0.2f * beat + 0.6f * burst;
        radialBlur(c, src, cx, cy, std::min(strength, 0.92f), 1.1f + 0.5f * burst, 0.45f);

        // 3) the "303" strip slides across with horizontal motion blur
        if (t > 32.8f && t < 40.6f) {
            float a = smoothstep(32.8f, 33.6f, t) * (1.0f - smoothstep(39.6f, 40.6f, t));
            float h = 0.135f, wdt = h * 512.0f / 19.0f / c.aspect;
            float x = 0.40f - wdt * 0.66f - (t - 36.5f) * 0.05f;
            setBlend(Blend::Add);
            for (int g = 0; g < 7; g++) {
                float off = (g - 3) * 0.012f * (1.0f + 2.0f * beat);
                c.drawImage(c.tex("303b"), x + off, 0.43f, wdt, h, a * (g == 3 ? 1.0f : 0.22f), 0, 0, 1, 1, 0,
                            Vec3(0.9f, 1.0f, 1.0f), true, true);
            }
        }
        // fade out to white into the next part
        c.flash(smoothstep(39.4f, 40.8f, t), Vec3(1, 1, 1));
    }
};
REGISTER_SCENE(Intro, "intro");
}  // namespace
