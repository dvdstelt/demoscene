// 0:55 - 1:12  black & white blobs under the first lyric words, the overexposed spikey with an inset,
// and statix' video with the chrome spikey.
#include <cmath>

#include "../fx.h"

namespace {
Mat4 spinModel(float t, float sx, float sy, float sz) {
    return Mat4::rotY(t * sy) * Mat4::rotX(t * sx) * Mat4::rotZ(t * sz);
}

// 0:55 - 0:58  black/white blob plasma with a contour-lined object
struct Robot : Scene {
    void render(Ctx& c, float, float) override {
        const float t = VT(c.T);
        WarpParams w;
        w.mode = 2;
        w.zoom = 0.55f;
        w.angle = 0.2f * t;
        w.time = t * 0.11f;
        w.wave = 1.2f;
        w.crisp = 1.2f;
        setBlend(Blend::None);
        drawWarp(c, c.tex("blobby#1"), w);

        // purple bars sweeping in on the second lyric word
        float bars = smoothstep(56.3f, 56.6f, t) * (1.0f - smoothstep(57.6f, 58.0f, t));
        if (bars > 0) {
            for (int i = 0; i < 9; i++) {
                float x = 0.24f + i * 0.022f + 0.01f * std::sin(t * 7 + i);
                c.rect(x, 0.0f, 0.009f, 0.55f + 0.1f * hash1(i * 3.3f), Vec3(0.55f, 0.1f, 0.45f), 0.85f * bars);
            }
        }

        // the object: spikey drawn as white contour lines on black
        Mat4 proj = Mat4::perspective(0.8f, c.aspect, 0.1f, 50.0f);
        Mat4 view = Mat4::lookAt(Vec3(0, 0, 4.2f), Vec3(0, 0, 0), Vec3(0, 1, 0));
        MeshDraw d;
        d.proj = proj;
        d.view = view;
        d.model = Mat4::translate(t < 56.4f ? -0.35f : 0.35f, t < 56.4f ? 0.15f : -0.25f, 0) * spinModel(t, 0.9f, 1.3f, 0.4f);
        d.mode = 1;
        d.param = 9.0f;
        setBlend(Blend::None);
        gl::Clear(GL_DEPTH_BUFFER_BIT);
        drawMesh(c, "spikey", d);

        // inset (top right) with an inverted view of the same plasma
        float big = smoothstep(56.2f, 56.5f, t);
        float ix = mix(0.70f, 0.60f, big), iy = mix(0.06f, 0.04f, big), iw = mix(0.12f, 0.36f, big), ih = mix(0.1f, 0.32f, big);
        c.rect(ix - 0.003f, iy - 0.004f, iw + 0.006f, ih + 0.008f, Vec3(1, 1, 1));
        WarpParams in = w;
        in.zoom = 1.3f;
        in.angle = -0.5f * t;
        in.invert = true;
        setBlend(Blend::None);
        drawWarp(c, c.tex("blobby#1"), in, ix, iy, iw, ih);

        c.flash(1.0f - smoothstep(54.7f, 55.1f, t), Vec3(1, 1, 1));
    }
};
REGISTER_SCENE(Robot, "robot");

// 0:58 - 1:05  bright spikey with zoom blur, blue inset
struct Spoon : Scene {
    void render(Ctx& c, float, float) override {
        const float t = VT(c.T);
        Target& src = c.scratch[0];
        src.bind();
        float pulse = beatPulse(t - 58.2f, kBeat, 4.0f);
        // background: the pastel plasma itself, rotozoomed
        WarpParams w;
        w.mode = 3;
        w.zoom = 0.8f;
        w.strength = 1.5f + std::sin(t) * 0.5f;
        w.time = t * 0.3f;
        w.angle = t * 0.4f;
        w.bright = 1.0f;
        w.contrast = 1.8f;
        setBlend(Blend::None);
        drawWarp(c, c.tex("blobby#0"), w);
        gl::Clear(GL_DEPTH_BUFFER_BIT);
        Mat4 proj = Mat4::perspective(1.0f, c.aspect, 0.05f, 50.0f);
        float dist = 1.9f - 0.4f * std::sin(t * 0.7f);
        Mat4 view = Mat4::lookAt(Vec3(0.3f, 0.1f, dist), Vec3(0, 0, 0), Vec3(0, 1, 0));
        MeshDraw d;
        d.proj = proj;
        d.view = view;
        d.model = spinModel(t, 0.7f, 1.1f, 0.3f);
        d.env = &c.tex("blobby#0");
        d.exposure = 1.05f + 0.35f * pulse;
        d.tint = Vec3(1.1f, 1.05f, 0.9f);
        drawMesh(c, "spikey", d);

        c.out->bind();
        setBlend(Blend::None);
        radialBlur(c, src, 0.5f + 0.1f * std::sin(t * 1.3f), 0.5f + 0.1f * std::cos(t), 0.25f + 0.35f * pulse, 1.12f, 0.6f);

        // inset: the same object, cold blue env map
        float ix = 0.60f, iy = 0.045f, iw = 0.34f, ih = 0.33f;
        c.rect(ix - 0.003f, iy - 0.004f, iw + 0.006f, ih + 0.008f, Vec3(1, 1, 1));
        c.subViewport(ix, iy, iw, ih, true, Vec3(0.12f, 0.1f, 0.35f));
        WarpParams bg;
        bg.mode = 2;
        bg.zoom = 1.6f;
        bg.time = t * 0.2f;
        bg.tint = Vec3(0.55f, 0.55f, 1.2f);
        setBlend(Blend::None);
        drawWarp(c, c.tex("tex3d#2"), bg);
        gl::Clear(GL_DEPTH_BUFFER_BIT);
        MeshDraw e = d;
        e.proj = Mat4::perspective(0.9f, iw * c.aspect / ih, 0.05f, 50.0f);
        e.view = Mat4::lookAt(Vec3(0, 0, 2.4f), Vec3(0, 0, 0), Vec3(0, 1, 0));
        e.model = spinModel(t + 3.0f, -0.6f, 0.8f, 0.2f);
        e.env = &c.tex("tex3d#2");
        e.tint = Vec3(0.8f, 0.75f, 1.3f);
        e.exposure = 1.1f;
        drawMesh(c, "spikey", e);
        c.resetViewport();

        c.flash(1.0f - smoothstep(58.1f, 58.5f, t), Vec3(1, 1, 1));
        c.flash(smoothstep(64.6f, 65.2f, t), Vec3(1, 1, 1));
    }
};
REGISTER_SCENE(Spoon, "spoon");

// 1:05 - 1:12  statix' video, chrome spikey, "statix / code"
struct AlexVideo : Scene {
    void render(Ctx& c, float, float) override {
        const float t = VT(c.T);
        float local = t - 65.3f;
        // statix' photo (alex.raw), dark and slowly drifting
        float z = 1.05f + 0.025f * local, dim = mix(0.75f, 0.3f, smoothstep(65.6f, 67.2f, t));
        setBlend(Blend::None);
        c.drawImage(c.tex("alex"), 0.5f - 0.5f * z - 0.01f * local, 0.5f - 0.5f * z, z, z, 1.0f, 0, 0, 1, 1, 0,
                    Vec3(0.85f * dim, 0.72f * dim, 0.85f * dim), true);

        gl::Clear(GL_DEPTH_BUFFER_BIT);
        MeshDraw d;
        d.proj = Mat4::perspective(0.75f, c.aspect, 0.05f, 50.0f);
        d.view = Mat4::lookAt(Vec3(0, 0, 3.0f), Vec3(0, 0, 0), Vec3(0, 1, 0));
        d.model = Mat4::translate(0.75f, -0.62f, 0.0f) * spinModel(local + 2.0f, 0.25f, 0.45f, 0.1f);
        d.env = &c.tex("phong");
        d.tint = Vec3(1.0f, 0.95f, 1.0f);
        d.exposure = 1.15f;
        d.specular = 0.8f;
        drawMesh(c, "spikey", d);

        setBlend(Blend::Alpha);
        float a1 = smoothstep(66.3f, 66.8f, t), a2 = smoothstep(68.4f, 68.9f, t);
        for (int pass = 0; pass < 2; pass++) {
            Vec3 col = pass == 0 ? Vec3(0, 0, 0) : Vec3(1, 1, 1);
            float o = pass == 0 ? 0.006f : 0.0f;
            c.drawText("credits", "3", 0.135f + o, 0.085f + o * c.aspect, 0.105f, a1 * (pass == 0 ? 0.7f : 1.0f), col);
            c.drawText("credits", "0", 0.215f + o, 0.165f + o * c.aspect, 0.1f, a2 * (pass == 0 ? 0.7f : 1.0f), col);
        }
        c.flash(1.0f - smoothstep(65.2f, 65.6f, t), Vec3(1, 1, 1));
    }
};
REGISTER_SCENE(AlexVideo, "alex");
}  // namespace
