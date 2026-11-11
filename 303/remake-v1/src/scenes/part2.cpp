// 1:12 - 2:10  breakdancer, scribble web with credits, rings tunnel, Julia flowers with the chrome face,
// the face on a box over the hex floor, the first bd2.raw text picture.
#include <algorithm>
#include <cmath>

#include "../fx.h"

namespace {
Mat4 spin(float t, float sx, float sy, float sz) { return Mat4::rotY(t * sy) * Mat4::rotX(t * sx) * Mat4::rotZ(t * sz); }

// 1:12 - 1:22  Harlequin breakdancing
struct Harl : Scene {
    void render(Ctx& c, float, float) override {
        const float t = VT(c.T);
        float local = t - 71.65f;
        int n = c.videoFrames("HARL");
        float frame = std::min(float(n - 1), local * (n / 10.3f));
        setBlend(Blend::None);
        Texture& v = c.video("HARL", frame);
        c.drawImage(v, 0, 0, 1, 1, 1.0f, 0, 0, 1, 1, 0, Vec3(1, 1, 1), true);
        // the original smears a band of columns downward on the off-beats
        float g = beatPulse(local - kBeat, kBeat * 2, 6.0f);
        if (g > 0.05f) {
            setBlend(Blend::Alpha);
            for (int i = 0; i < 6; i++) {
                float x = hash1(std::floor(local / (kBeat * 2)) * 7.0f + i) * 0.9f;
                float w = 0.03f + 0.05f * hash1(i * 3.7f + std::floor(local / (kBeat * 2)));
                c.drawImage(v, x, 0.55f, w, 0.45f, g * 0.85f, x, 0.55f, x + w, 0.6f, 0, Vec3(1, 1, 1), true);
            }
        }
        c.flash(1.0f - smoothstep(71.65f, 72.0f, t), Vec3(1, 1, 1));
    }
};
REGISTER_SCENE(Harl, "harl");

// 1:22 - 1:32  scribbled wire balls on white/black with rotating outline lettering
struct Web : Scene {
    void render(Ctx& c, float, float) override {
        const float t = VT(c.T);
        float local = t - 81.95f;
        int seg = int(std::floor(local / (kBeat * 2)));
        static const int dark[] = {0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 1, 0, 1};
        bool black = dark[std::clamp(seg, 0, 12)] != 0;
        Vec3 bg = black ? Vec3(0, 0, 0) : Vec3(0.97f, 0.97f, 0.98f);
        gl::ClearColor(bg.x, bg.y, bg.z, 1);
        gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // the scribble: several wireframe copies of the bumpy ball, faint lines
        Mat4 proj = Mat4::perspective(0.9f, c.aspect, 0.1f, 50.0f);
        Mat4 view = Mat4::lookAt(Vec3(0, 0, 3.4f), Vec3(0, 0, 0), Vec3(0, 1, 0));
        setBlend(Blend::Alpha);
        for (int k = 0; k < 10; k++) {
            MeshDraw d;
            d.proj = proj;
            d.view = view;
            float s = 0.45f + 0.12f * k;
            d.model = Mat4::translate(-0.1f + 0.05f * std::sin(local + k), 0.05f, 0) * spin(local * 0.6f + k * 1.7f, 0.7f, 0.9f, 0.3f) *
                      Mat4::scale(s, s * 0.9f, s);
            d.mode = 3;
            d.tint = black ? Vec3(0.75f, 0.75f, 0.8f) : Vec3(0.05f, 0.05f, 0.08f);
            d.alpha = 0.05f;
            d.cull = false;
            gl::PolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            gl::DepthMask(GL_FALSE);
            drawMesh(c, k % 2 ? "icosa" : "spikey", d);
            gl::DepthMask(GL_TRUE);
            gl::PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }

        // rotating outline lettering, drawn into a scratch target and turned in 3D
        static const char* words[] = {"aap", "graphics", "design", "aap"};
        const char* word = words[std::clamp(seg / 3, 0, 3)];
        Target& tt = c.scratch[1];
        tt.bind();
        gl::ClearColor(0, 0, 0, 0);
        gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        setBlend(Blend::Alpha);
        c.drawText("outline", word, 0.5f, 0.5f, 0.2f, 1.0f, black ? Vec3(1.0f, 0.95f, 0.2f) : Vec3(0.15f, 0.2f, 1.0f));
        c.out->bind();
        Texture tx = asTexture(tt);
        Mat4 m = Mat4::translate(0.1f * std::sin(local * 0.8f), 0.0f, 0.3f * std::sin(local)) * Mat4::rotZ(local * 0.9f) *
                 Mat4::rotY(local * 1.7f) * Mat4::scale(c.aspect * 0.9f, 0.9f, 1.0f);
        setBlend(Blend::Premul);
        gl::Clear(GL_DEPTH_BUFFER_BIT);
        drawQuad3D(c, tx, proj * view * m, 1.0f, Vec3(1, 1, 1), false, true);
        c.flash(1.0f - smoothstep(81.95f, 82.3f, t), Vec3(1, 1, 1));
    }
};
REGISTER_SCENE(Web, "web");

// 1:32 - 1:43  squarish rings tunnel with the spikey in its throat
struct Rings : Scene {
    void render(Ctx& c, float, float) override {
        const float t = VT(c.T);
        float local = t - 92.5f;
        float cx = 0.5f + 0.06f * std::sin(local * 0.7f), cy = 0.5f + 0.05f * std::cos(local * 0.5f);
        WarpParams w;
        w.mode = 0;
        w.cx = cx;
        w.cy = cy;
        w.shapeP = 3.2f;
        w.zoom = 1.0f;
        w.strength = 0.5f;
        w.tint = Vec3(1.15f, 0.92f, 1.12f);
        w.time = -local * 0.3f;
        w.angle = 0.1f * std::sin(local * 0.3f);
        w.fog = 0.9f;
        w.bright = 1.15f;
        setBlend(Blend::None);
        drawWarp(c, c.tex("tunnel#0"), w);

        // spikey: recedes into the throat and comes back
        float depth = 3.0f + 4.0f * smoothstep(97.0f, 99.0f, t) - 3.5f * smoothstep(100.5f, 102.0f, t);
        gl::Clear(GL_DEPTH_BUFFER_BIT);
        MeshDraw d;
        d.proj = Mat4::perspective(0.8f, c.aspect, 0.05f, 50.0f);
        d.view = Mat4::lookAt(Vec3(0, 0, depth), Vec3(0, 0, 0), Vec3(0, 1, 0));
        d.model = Mat4::translate((cx - 0.5f) * 2.0f, -(cy - 0.5f) * 2.0f, 0) * spin(local, 0.4f, 0.7f, 0.25f);
        d.env = &c.tex("phong");
        d.tint = Vec3(1.0f, 0.95f, 1.15f);
        d.exposure = 1.45f;
        d.fogColor = Vec3(0.15f, 0.05f, 0.1f);
        d.fogNear = 4.0f;
        d.fogFar = 10.0f;
        drawMesh(c, "spikey", d);
        c.flash(1.0f - smoothstep(92.5f, 92.9f, t), Vec3(1, 1, 1));
        c.flash(smoothstep(102.7f, 103.0f, t), Vec3(1, 1, 1));
    }
};
REGISTER_SCENE(Rings, "rings");

// Julia flowers from the stripe texture; shared by the face and box scenes
void drawFlowers(Ctx& c, float t, float x = 0, float y = 0, float w_ = 1, float h_ = 1) {
    WarpParams w;
    w.mode = 6;
    w.cx = 0.5f + 0.15f * std::sin(t * 0.13f);
    w.cy = 0.5f + 0.1f * std::cos(t * 0.11f);
    w.zoom = 0.22f + 0.04f * std::sin(t * 0.21f);
    w.angle = t * 0.08f;
    w.strength = 1.0f;
    w.time = t;
    setBlend(Blend::None);
    drawWarp(c, c.tex("wipeout"), w, x, y, w_, h_);
}

// 1:43 - 2:00  flowers, then the chrome face reflecting them
struct Face : Scene {
    void render(Ctx& c, float, float) override {
        const float t = VT(c.T);
        float local = t - 103.0f;
        // render the flowers once into scratch[1]; used as background and as the face's environment
        Target& fl = c.scratch[1];
        fl.bind();
        drawFlowers(c, local);
        c.out->bind();
        setBlend(Blend::None);
        c.drawTarget(fl);
        // light burst with web lines at the start
        float burst = std::exp(-std::max(0.0f, local - 1.2f) * 2.5f) * smoothstep(0.6f, 1.2f, local);
        if (burst > 0.01f) {
            setBlend(Blend::Add);
            c.drawImage(c.tex("phong1"), 0.25f, 0.45f - 0.3f * c.aspect * 0.5f, 0.3f, 0.3f * c.aspect, burst * 2.0f, 0, 0, 1, 1, 0,
                        Vec3(1, 1, 1), false, true);
        }
        float faceIn = smoothstep(108.5f, 110.0f, t);
        if (faceIn > 0) {
            gl::Clear(GL_DEPTH_BUFFER_BIT);
            Texture env = asTexture(fl);
            MeshDraw d;
            d.proj = Mat4::perspective(0.7f, c.aspect, 0.05f, 50.0f);
            d.view = Mat4::lookAt(Vec3(0, 0, 1.6f - 0.35f * smoothstep(110.0f, 116.0f, t)), Vec3(0, 0, 0), Vec3(0, 1, 0));
            d.mode = 4;
            d.param = 0.6f;
            d.model = Mat4::translate(-0.15f + 0.25f * std::sin(local * 0.3f), 0.0f, 0) * Mat4::rotY(0.5f * std::sin(local * 0.45f) - 0.3f) *
                      Mat4::rotX(0.15f * std::sin(local * 0.6f)) * Mat4::rotY(kPi);
            d.cull = false;
            d.env = &env;
            d.exposure = 1.25f;
            d.specular = 1.0f;
            d.alpha = faceIn;
            if (faceIn < 1.0f) setBlend(Blend::Alpha);
            drawMesh(c, "faceo", d);
        }
        c.flash(1.0f - smoothstep(103.0f, 103.4f, t), Vec3(1, 1, 1));
        c.flash(smoothstep(105.6f, 106.0f, t) * (1.0f - smoothstep(106.0f, 106.6f, t)), Vec3(1, 1, 1));
        c.flash(smoothstep(108.8f, 109.1f, t) * (1.0f - smoothstep(109.1f, 109.8f, t)), Vec3(1, 1, 1));
    }
};
REGISTER_SCENE(Face, "face");

// 2:00 - 2:05  the face picture on a box floating over the hex floor
struct FaceBox : Scene {
    void render(Ctx& c, float, float) override {
        const float t = VT(c.T);
        float local = t - 120.0f;
        // picture for the box sides: flowers + face, rendered at the scratch resolution
        Target& pic = c.scratch[1];
        pic.bind();
        drawFlowers(c, t - 103.0f);
        {
            gl::Clear(GL_DEPTH_BUFFER_BIT);
            Texture env = asTexture(pic);
            MeshDraw d;
            d.proj = Mat4::perspective(0.7f, c.aspect, 0.05f, 50.0f);
            d.view = Mat4::lookAt(Vec3(0, 0, 1.45f), Vec3(0, 0, 0), Vec3(0, 1, 0));
            d.model = Mat4::rotY(0.3f * std::sin(local * 0.8f) + kPi);
            d.mode = 4;
            d.param = 0.6f;
            d.cull = false;
            d.env = &env;
            d.exposure = 1.25f;
            drawMesh(c, "faceo", d);
        }
        c.out->bind();
        WorldParams w;
        w.eye = Vec3(0, 0.1f, 4.0f - local * 0.6f);
        w.target = w.eye + Vec3(0.0f, -0.05f, -1.0f);
        w.floorY = -1.0f;
        w.ceilY = 1.6f;
        w.fogDist = 7.0f;
        w.time = t;
        setBlend(Blend::None);
        drawWorld(c, w);
        gl::Clear(GL_DEPTH_BUFFER_BIT);
        Mat4 vp = worldProj(c, w) * worldView(w);
        Vec3 pos(0.1f * std::sin(local), 0.15f, w.eye.z - 2.2f - local * 0.9f);
        Mat4 box = Mat4::translate(pos.x, pos.y, pos.z) * Mat4::rotY(local * 0.9f) * Mat4::rotX(0.2f * std::sin(local));
        Texture pt = asTexture(pic);
        const float s = 0.85f;
        Mat4 faces[6] = {Mat4::translate(0, 0, s), Mat4::rotY(kPi) * Mat4::translate(0, 0, s), Mat4::rotY(kPi / 2) * Mat4::translate(0, 0, s),
                         Mat4::rotY(-kPi / 2) * Mat4::translate(0, 0, s), Mat4::rotX(kPi / 2) * Mat4::translate(0, 0, s),
                         Mat4::rotX(-kPi / 2) * Mat4::translate(0, 0, s)};
        for (int i = 0; i < 6; i++) drawQuad3D(c, pt, vp * box * faces[i] * Mat4::scale(s * 1.15f, s, 1), 1.0f, Vec3(1, 1, 1), false, true);
        c.flash(smoothstep(124.5f, 124.9f, t), Vec3(1, 1, 1));
    }
};
REGISTER_SCENE(FaceBox, "facebox");

// 2:05 - 2:10  the two bd2.raw text pictures
struct Typo : Scene {
    void render(Ctx& c, float, float) override {
        const float t = VT(c.T);
        float local = t - 124.9f;
        bool lost = t > 126.85f && t < 127.4f;
        Texture& img = c.tex(lost ? "bd2#1" : "bd2#0");
        float z = 1.0f + 0.04f * std::sin(local * 3.0f) + 0.08f * beatPulse(local, kBeat, 6.0f);
        setBlend(Blend::None);
        c.drawImage(img, 0.5f - 0.5f * z, 0.5f - 0.5f * z, z, z, 1.0f, 0, 0, 1, 1, 0, Vec3(1, 1, 1), true);
        // horizontal slice displacement on the beat
        float g = beatPulse(local - kBeat * 0.5f, kBeat, 7.0f);
        setBlend(Blend::Alpha);
        for (int i = 0; i < 10; i++) {
            float y = hash1(i * 4.1f + std::floor(local / kBeat)) * 0.95f, h = 0.02f + 0.05f * hash1(i * 9.3f);
            float dx = (hash1(i * 2.3f + std::floor(local / kBeat) * 3.0f) - 0.5f) * 0.25f * g;
            c.drawImage(img, dx, y, 1.0f, h, 1.0f, 0, y, 1, y + h, 0, Vec3(1, 1, 1), true);
        }
        // white web strands
        setBlend(Blend::Add);
        WarpParams w;
        w.mode = 2;
        w.zoom = 0;
        w.lines = 0.4f;
        w.seed = std::floor(local / (kBeat * 2));
        w.cx = 0.45f;
        w.cy = 0.45f;
        w.bright = 0.0f;
        drawWarp(c, c.white(), w);
        c.flash(1.0f - smoothstep(124.9f, 125.3f, t), Vec3(1, 1, 1));
        c.flash(smoothstep(129.6f, 129.95f, t), Vec3(1, 1, 1));
    }
};
REGISTER_SCENE(Typo, "typo");
}  // namespace
