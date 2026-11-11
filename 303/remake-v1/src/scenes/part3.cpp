// 2:10 - 4:13  metaballs, wire blobs, spirals, vic, tentacle star, plasma sphere, paranoid face,
// face on the corridor, the dance, the icon credits and the MC-303 outro.
#include <algorithm>
#include <cmath>
#include <vector>

#include "../fx.h"

namespace {
Mat4 spin(float t, float sx, float sy, float sz) { return Mat4::rotY(t * sy) * Mat4::rotX(t * sx) * Mat4::rotZ(t * sz); }
void drawDuotone(Ctx& c, const Texture& t, float x, float y, float w, float h, Vec3 lo, Vec3 mid, Vec3 hi, float alpha = 1.0f);

// ---------------------------------------------------------------- sprite batch
// Quads in 3D, drawn either as soft round "tube" blobs or as square outlines.
struct Sprites {
    struct S { Vec3 p, u, v; Vec3 col; float a; };
    std::vector<S> list;
    GLuint vao = 0, vbo = 0;
    Shader sh;
    void init() {
        if (vao) return;
        gl::GenVertexArrays(1, &vao);
        gl::GenBuffers(1, &vbo);
        gl::BindVertexArray(vao);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
        gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 36, (void*)0);
        gl::VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 36, (void*)12);
        gl::VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 36, (void*)20);
        gl::EnableVertexAttribArray(0);
        gl::EnableVertexAttribArray(1);
        gl::EnableVertexAttribArray(2);
        gl::BindVertexArray(0);
        sh.build(R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aCol;
uniform mat4 vp;
out vec2 uv;
out vec4 col;
void main() { uv = aUv; col = aCol; gl_Position = vp * vec4(aPos, 1.0); }
)",
                 R"(
in vec2 uv;
in vec4 col;
out vec4 o;
uniform int style;   // 0 tube blob, 1 square outline
void main() {
    vec2 q = uv * 2.0 - 1.0;
    if (style == 1) {
        float d = 1.0 - max(abs(q.x), abs(q.y));
        float w = fwidth(d) * 1.5;
        float a = smoothstep(0.0, w, d) * (1.0 - smoothstep(0.14, 0.14 + w, d));
        o = vec4(col.rgb, col.a * a);
        return;
    }
    float r = length(q);
    if (r > 1.0) discard;
    float z = sqrt(1.0 - r * r);
    float l = 0.25 + 0.75 * max(dot(vec3(q, z), normalize(vec3(-0.4, 0.6, 0.7))), 0.0);
    float spec = pow(max(dot(vec3(q, z), normalize(vec3(-0.3, 0.5, 0.8))), 0.0), 30.0);
    float edge = smoothstep(1.0, 0.85, r);
    o = vec4(col.rgb * l + spec * 0.8, col.a * edge);
}
)",
                 "sprites");
    }
    void add(Vec3 p, Vec3 u, Vec3 v, Vec3 col, float a) { list.push_back({p, u, v, col, a}); }
    void draw(const Mat4& vp, int style) {
        init();
        std::vector<float> d;
        d.reserve(list.size() * 6 * 9);
        for (const S& s : list) {
            Vec3 c[4] = {s.p - s.u - s.v, s.p + s.u - s.v, s.p + s.u + s.v, s.p - s.u + s.v};
            float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            int idx[6] = {0, 1, 2, 0, 2, 3};
            for (int k : idx) {
                float v[9] = {c[k].x, c[k].y, c[k].z, uv[k][0], uv[k][1], s.col.x, s.col.y, s.col.z, s.a};
                d.insert(d.end(), v, v + 9);
            }
        }
        sh.use();
        sh.set("vp", vp);
        sh.set("style", style);
        gl::BindVertexArray(vao);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(d.size() * 4), d.data(), GL_DYNAMIC_DRAW);
        gl::DrawArrays(GL_TRIANGLES, 0, GLsizei(list.size() * 6));
        list.clear();
    }
};
Sprites& sprites() {
    static Sprites s;
    return s;
}

// flying square outlines streaming toward the camera
void flyingSquares(const Mat4& vp, float t, int n, float speed, Vec3 col) {
    Sprites& sp = sprites();
    for (int i = 0; i < n; i++) {
        float x = (hash1(i * 1.7f) - 0.5f) * 9.0f, y = (hash1(i * 3.1f) - 0.5f) * 7.0f;
        float z = std::fmod(hash1(i * 5.3f) * 20.0f + t * speed, 20.0f) - 18.0f;
        float s = 0.06f + 0.08f * hash1(i * 7.7f);
        float a = hash1(i * 2.9f) * 6.28f + t * (0.5f + hash1(i));
        Vec3 u(std::cos(a) * s, std::sin(a) * s, 0), v(-std::sin(a) * s, std::cos(a) * s, 0);
        sp.add(Vec3(x, y, z), u, v, col, 0.75f * smoothstep(-18.0f, -14.0f, z));
    }
    setBlend(Blend::Alpha);
    sp.draw(vp, 1);
}

// ---------------------------------------------------------------- 2:10 gold metaballs in the corridor
struct Blobs : Scene {
    void render(Ctx& c, float, float) override {
        const float t = VT(c.T);
        float local = t - 129.95f;
        WorldParams w;
        w.eye = Vec3(0.0f, -0.35f, 0.0f);
        w.target = Vec3(0.15f * std::sin(local * 0.2f), -0.42f, -3.0f);
        w.floorY = -1.0f;
        w.ceilY = 1.1f;
        w.fogDist = 6.5f;
        w.time = t;
        float approach = smoothstep(0.0f, 15.0f, local);
        w.balls = 3;
        for (int i = 0; i < 3; i++) {
            float ph = local * (1.6f + 0.3f * i) + i * 2.1f;
            float bounce = std::fabs(std::sin(ph)) * 0.55f;
            w.ball[i] = Vec3(0.35f * std::sin(local * 0.7f + i * 2.0f), -0.75f + bounce, -14.0f + 11.5f * approach + 0.5f * std::cos(local + i));
            w.ballR[i] = 0.32f + 0.06f * i;
        }
        setBlend(Blend::None);
        drawWorld(c, w);
        c.flash(1.0f - smoothstep(129.95f, 130.6f, t), Vec3(1, 1, 1));
    }
};
REGISTER_SCENE(Blobs, "blobs");

// 2:27 wireframe blobs over white hex lines
struct WireBlobs : Scene {
    void render(Ctx& c, float, float) override {
        const float t = VT(c.T);
        float local = t - 146.5f;
        // white hex lines over blue-grey cells with darker spots (wire.raw's lines sit at ~8% brightness)
        static Shader hexBg;
        static bool built = false;
        if (!built) {
            hexBg.build(kFullscreenVS, R"(
in vec2 uv; out vec4 o;
uniform sampler2D wire, spots; uniform float time, aspect;
void main() {
    vec2 p = (uv - 0.5) * vec2(aspect, 1.0);
    p = rot2(0.35 + time * 0.05) * p * 0.55 + vec2(time * 0.04, time * 0.015);
    float l = texture(wire, p).r * 12.0;
    float sp = texture(spots, p * 1.7).r;
    vec3 cell = vec3(0.45, 0.5, 0.62) * (0.8 + 0.2 * (1.0 - sp));
    o = vec4(mix(cell, vec3(1.0), smoothstep(0.35, 0.8, l)), 1.0);
}
)", "hexbg");
            built = true;
        }
        setBlend(Blend::None);
        hexBg.use();
        c.tex("wire").bind(0);
        c.tex("wire").setRepeat(true);
        hexBg.set("wire", 0);
        c.tex("blobby#1").bind(1);
        hexBg.set("spots", 1);
        hexBg.set("time", local);
        hexBg.set("aspect", c.aspect);
        drawFullscreen();
        WorldParams w;
        w.floorOn = false;
        w.eye = Vec3(0, 0, 3.2f);
        w.target = Vec3(0, 0, 0);
        w.fogDist = 1e6f;
        w.balls = 2;
        w.ballStyle = 1;
        w.ball[0] = Vec3(-0.3f + 0.2f * std::sin(local * 2.0f), 0.25f, 0);
        w.ball[1] = Vec3(0.3f, -0.35f + 0.2f * std::cos(local * 1.7f), 0.2f);
        w.ballR[0] = 0.6f;
        w.ballR[1] = 0.62f;
        // draw only the balls on top of the background (the shader returns fog colour elsewhere)
        setBlend(Blend::None);
        Target& tmp = c.scratch[1];
        tmp.bind();
        w.fogColor = Vec3(1, 0, 1);  // key colour
        drawWorld(c, w);
        c.out->bind();
        // composite: draw balls where the key colour is absent
        compositeKeyed(c, tmp);
        c.flash(1.0f - smoothstep(146.5f, 146.8f, t), Vec3(1, 1, 1));
    }
    void compositeKeyed(Ctx& c, const Target& src) {
        static Shader s;
        static bool built = false;
        if (!built) {
            s.build(kFullscreenVS, R"(
in vec2 uv; out vec4 o; uniform sampler2D src;
void main() { vec3 c = texture(src, uv).rgb; float k = step(0.02, distance(c, vec3(1.0, 0.0, 1.0))); o = vec4(c, k); }
)",
                    "keyed");
            built = true;
        }
        setBlend(Blend::Alpha);
        s.use();
        src.bindTex(0);
        s.set("src", 0);
        drawFullscreen();
    }
};
REGISTER_SCENE(WireBlobs, "wireblobs");

// 2:30 the tentacle mesh twisted into spiral arms, drawn as glowing wireframe
struct Spiral : Scene {
    void render(Ctx& c, float, float) override {
        const float t = VT(c.T);
        float local = t - 149.9f;
        Mat4 proj = Mat4::perspective(1.0f, c.aspect, 0.05f, 60.0f);
        Mat4 view = Mat4::lookAt(Vec3(0, 0, 3.0f), Vec3(0, 0, 0), Vec3(0, 1, 0));
        flyingSquares(proj * view, local, 160, 2.5f, Vec3(0.75f, 0.8f, 0.95f));
        MeshDraw d;
        d.proj = proj;
        d.view = view;
        d.model = Mat4::rotZ(local * 0.6f) * Mat4::rotX(0.35f * std::sin(local * 0.4f)) * Mat4::scale(1.6f, 1.6f, 0.5f);
        d.mode = 2;
        d.tint = Vec3(0.7f, 0.8f, 1.0f);
        d.cull = false;
        d.twistZ = 0.32f;
        d.twistY = 0.02f;
        d.wobblePhase = local * 2.0f;
        d.wobble = 0.15f;
        setBlend(Blend::Add);
        gl::DepthMask(GL_FALSE);
        drawMesh(c, "icosa", d);
        gl::DepthMask(GL_TRUE);
        c.flash(1.0f - smoothstep(149.9f, 150.2f, t), Vec3(1, 1, 1));
    }
};
REGISTER_SCENE(Spiral, "spiral");

// 2:37 vic + the bumpy icosahedron + "vic / music"
struct Vic : Scene {
    void render(Ctx& c, float, float) override {
        const float t = VT(c.T);
        float local = t - 156.6f;
        float z = 1.0f + 0.03f * local;
        setBlend(Blend::None);
        drawDuotone(c, c.tex("vic"), 0.5f - 0.5f * z, 0.5f - 0.5f * z, z, z, Vec3(0.06f, 0.04f, 0.2f), Vec3(0.25f, 0.22f, 0.56f),
                    Vec3(0.85f, 0.7f, 0.5f));
        gl::Clear(GL_DEPTH_BUFFER_BIT);
        MeshDraw d;
        d.proj = Mat4::perspective(0.8f, c.aspect, 0.05f, 50.0f);
        d.view = Mat4::lookAt(Vec3(0, 0, 2.6f), Vec3(0, 0, 0), Vec3(0, 1, 0));
        d.model = Mat4::translate(-0.82f, 0.42f, 0) * spin(local, 0.35f, 0.5f, 0.15f) * Mat4::scale(1.25f, 1.25f, 1.25f);
        d.env = &c.tex("phong");
        d.exposure = 1.25f;
        d.spikeScale = 0.22f;
        drawMesh(c, "spikey", d);
        setBlend(Blend::Alpha);
        float a1 = smoothstep(157.5f, 158.0f, t), a2 = smoothstep(159.6f, 160.1f, t);
        for (int pass = 0; pass < 2; pass++) {
            Vec3 col = pass == 0 ? Vec3(0, 0, 0) : Vec3(1, 1, 1);
            float o = pass == 0 ? 0.006f : 0.0f;
            c.drawText("credits", "4", 0.075f + o, 0.89f + o * c.aspect, 0.12f, a1 * (pass == 0 ? 0.7f : 1.0f), col);
            c.drawText("credits", "1", 0.53f + o, 0.89f + o * c.aspect, 0.11f, a2 * (pass == 0 ? 0.7f : 1.0f), col);
        }
        c.flash(1.0f - smoothstep(156.6f, 157.0f, t), Vec3(1, 1, 1));
        c.flash(smoothstep(162.5f, 162.95f, t), Vec3(1, 1, 1));
    }
};
REGISTER_SCENE(Vic, "vic");

// 2:43 the tentacle star: icosa.dat, every shell rotated a little further so the arms curl
struct Star : Scene {
    void render(Ctx& c, float, float) override {
        const float t = VT(c.T);
        float local = t - 162.95f;
        Mat4 proj = Mat4::perspective(1.0f, c.aspect, 0.05f, 60.0f);
        Mat4 view = Mat4::lookAt(Vec3(0, 0.2f, 3.0f), Vec3(0, 0, 0), Vec3(0, 1, 0));
        flyingSquares(proj * view, local, 140, 3.5f, Vec3(0.6f, 0.7f, 0.95f));
        gl::Clear(GL_DEPTH_BUFFER_BIT);
        MeshDraw d;
        d.proj = proj;
        d.view = view;
        d.model = Mat4::translate(0.1f * std::sin(local * 0.5f), 0.3f - 0.25f * smoothstep(0.0f, 6.0f, local), 0) * spin(local, 0.25f, 0.4f, 0.15f) *
                  Mat4::scale(1.25f, 1.25f, 1.25f);
        d.env = &c.tex("tex3d#2");
        d.tint = Vec3(0.55f, 0.75f, 1.25f);
        d.exposure = 1.1f;
        d.cull = false;
        d.twistZ = 0.06f;
        d.twistY = 0.04f;
        d.wobblePhase = local * 2.2f;
        d.wobble = 0.35f;
        drawMesh(c, "icosa", d);
        c.flash(1.0f - smoothstep(162.95f, 163.3f, t), Vec3(1, 1, 1));
    }
};
REGISTER_SCENE(Star, "star");

// 2:50 corridor flight, the plasma sphere and its burst
struct Plasma : Scene {
    void render(Ctx& c, float, float) override {
        const float t = VT(c.T);
        float local = t - 169.95f;
        WorldParams w;
        float roll = smoothstep(183.5f, 185.5f, t) * kPi * 0.5f;
        w.eye = Vec3(0.0f, -0.3f, -local * 1.2f);
        w.target = w.eye + Vec3(0.0f, -0.05f, -1.0f);
        w.up = Vec3(std::sin(roll), std::cos(roll), 0);
        w.floorY = -1.0f;
        w.ceilY = 1.1f;
        w.fogDist = 6.0f;
        w.time = t;
        // the light approaches and becomes the plasma sphere
        float grow = smoothstep(173.0f, 178.0f, t);
        float recede = smoothstep(184.0f, 189.0f, t);
        float dist = mix(16.0f, 2.1f, grow) + recede * 5.0f;
        w.sphere = w.eye + Vec3(0.05f * std::sin(local), -0.2f + 0.1f * std::sin(local * 0.7f), -dist);
        w.sphereR = mix(0.15f, 1.15f, grow) * (1.0f - 0.35f * recede);
        w.sphereGlow = 1.0f + 2.0f * smoothstep(187.5f, 190.5f, t);
        w.time = t;
        if (t < 174.0f) {  // the small golden pair far away first
            w.balls = 2;
            for (int i = 0; i < 2; i++) { w.ball[i] = w.eye + Vec3(-0.1f + 0.2f * i, -0.75f + 0.1f * std::sin(local * 3 + i), -12.0f); w.ballR[i] = 0.12f; }
            w.sphereR = 0;
        }
        setBlend(Blend::None);
        Target& src = c.scratch[0];
        src.bind();
        drawWorld(c, w);
        c.out->bind();
        float burst = smoothstep(186.5f, 190.0f, t);
        Vec3 sc = (worldProj(c, w) * worldView(w)).transformPoint(w.sphere);
        radialBlur(c, src, 0.5f + 0.5f * sc.x, 0.5f - 0.5f * sc.y, 0.15f + 0.75f * burst, 1.0f + 1.5f * burst, 1.0f - 0.6f * burst);
        if (burst > 0) {
            setBlend(Blend::Add);
            WarpParams r;
            r.mode = 2;
            r.zoom = 0;
            r.bright = 0;
            r.lines = 0.0f;
            c.drawImage(c.tex("phong1"), 0.5f + 0.5f * sc.x - 0.6f, 0.5f - 0.5f * sc.y - 0.6f * c.aspect, 1.2f, 1.2f * c.aspect, burst * 2.5f, 0, 0, 1,
                        1, 0, Vec3(1.0f, 0.75f, 0.3f), false, true);
        }
        if (burst > 0) {  // golden ray wedges from the sphere
            static Shader rays;
            static bool built = false;
            if (!built) {
                rays.build(kFullscreenVS, R"(
in vec2 uv; out vec4 o; uniform vec2 ctr; uniform float amount, aspect, time;
void main() {
    vec2 d = (uv - ctr) * vec2(aspect, 1.0);
    float a = atan(d.y, d.x), r = length(d);
    float n = 0.0;
    for (int i = 1; i <= 3; i++) n += sin(a * float(9 * i + 4) + float(i) * 1.7 + time * 0.3 * float(i)) / float(i);
    float ray = pow(clamp(0.5 + 0.5 * n, 0.0, 1.0), 3.0);
    float fall = exp(-r * 1.6) * smoothstep(0.05, 0.2, r);
    o = vec4(vec3(1.0, 0.72, 0.25) * ray * fall * amount, 1.0);
}
)", "rays");
                built = true;
            }
            setBlend(Blend::Add);
            gl::BlendFunc(GL_ONE, GL_ONE);
            rays.use();
            rays.set("ctr", 0.5f + 0.5f * sc.x, 0.5f + 0.5f * sc.y);
            rays.set("amount", burst * 2.2f);
            rays.set("aspect", c.aspect);
            rays.set("time", local);
            drawFullscreen();
        }
        if (burst > 0) {  // the burst turns golden
            setBlend(Blend::Multiply);
            c.drawImage(c.white(), 0, 0, 1, 1, 1.0f, 0, 0, 1, 1, 0, Vec3(1.0f, mix(1.0f, 0.82f, burst), mix(1.0f, 0.45f, burst)));
        }
        c.flash(smoothstep(189.8f, 191.0f, t), Vec3(1, 1, 1));
        c.flash(1.0f - smoothstep(169.95f, 170.4f, t), Vec3(1, 1, 1));
    }
};
REGISTER_SCENE(Plasma, "plasma");

// duotone for the greyscale face clip
void drawDuotone(Ctx& c, const Texture& t, float x, float y, float w, float h, Vec3 lo, Vec3 mid, Vec3 hi, float alpha) {
    static Shader s;
    static bool built = false;
    if (!built) {
        s.build(R"(
uniform vec4 rect;
out vec2 uv;
void main() {
    vec2 c = vec2(gl_VertexID & 1, (gl_VertexID >> 1) & 1);
    uv = c;
    vec2 p = rect.xy + c * rect.zw;
    gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
)",
                R"(
in vec2 uv; out vec4 o;
uniform sampler2D tex; uniform vec3 lo, mid, hi; uniform float alpha;
void main() {
    float l = texBicubic(tex, uv).r;
    l = clamp(l, 0.0, 1.0);
    vec3 c = l < 0.5 ? mix(lo, mid, l * 2.0) : mix(mid, hi, l * 2.0 - 1.0);
    o = vec4(c, alpha);
}
)",
                "duotone");
        built = true;
    }
    s.use();
    t.bind(0);
    s.set("tex", 0);
    s.set("rect", x, y, w, h);
    s.set("lo", lo);
    s.set("mid", mid);
    s.set("hi", hi);
    s.set("alpha", alpha);
    drawQuad();
}

// 3:11 the paranoid face
struct Paranoid : Scene {
    void render(Ctx& c, float, float) override {
        const float t = VT(c.T);
        float local = t - 191.3f;
        int n = c.videoFrames("PARANOID");
        Texture& v = c.video("PARANOID", std::fmod(local * 12.0f, float(n)));
        setBlend(Blend::None);
        drawDuotone(c, v, -0.05f, -0.05f, 1.1f, 1.1f, Vec3(0.05f, 0.12f, 0.3f), Vec3(0.45f, 0.72f, 0.85f), Vec3(1.0f, 1.0f, 1.0f));
        float big = smoothstep(192.9f, 193.5f, t);
        float ix = mix(0.28f, 0.21f, big), iy = mix(0.68f, 0.5f, big), iw = mix(0.09f, 0.22f, big), ih = mix(0.09f, 0.26f, big);
        if (t > 192.0f) {
            c.rect(ix - 0.003f, iy - 0.004f, iw + 0.006f, ih + 0.008f, Vec3(1, 1, 1));
            Texture& v2 = c.video("PARANOID", std::fmod(local * 12.0f + 20.0f, float(n)));
            setBlend(Blend::None);
            drawDuotone(c, v2, ix, iy, iw, ih, Vec3(0.02f, 0.01f, 0.0f), Vec3(0.55f, 0.32f, 0.22f), Vec3(1.0f, 0.92f, 0.8f));
        }
        c.flash(1.0f - smoothstep(191.3f, 192.3f, t), Vec3(1, 1, 1));
        c.flash(smoothstep(196.9f, 197.4f, t), Vec3(1, 1, 1));
    }
};
REGISTER_SCENE(Paranoid, "paranoid");

// 3:17 face on a spinning card down the corridor
struct FaceFloor : Scene {
    void render(Ctx& c, float, float) override {
        const float t = VT(c.T);
        float local = t - 197.4f;
        WorldParams w;
        float roll = 0.6f + local * 0.25f;
        w.eye = Vec3(0, 0, -local * 1.5f);
        w.target = w.eye + Vec3(0.2f * std::sin(local * 0.4f), 0, -1);
        w.up = Vec3(std::sin(roll), std::cos(roll), 0);
        w.floorY = -1.0f;
        w.ceilY = 1.0f;
        w.fogDist = 5.0f;
        w.bright = 1.0f - smoothstep(205.5f, 208.0f, t) * 0.8f;
        setBlend(Blend::None);
        drawWorld(c, w);
        if (t < 204.0f) {
            gl::Clear(GL_DEPTH_BUFFER_BIT);
            int n = c.videoFrames("PARANOID");
            // render the clip in duotone into a scratch target, then put it on the card
            Target& card = c.scratch[1];
            card.bind();
            drawDuotone(c, c.video("PARANOID", std::fmod(local * 12.0f, float(n))), 0, 0, 1, 1, Vec3(0.0f, 0.02f, 0.08f),
                        Vec3(0.3f, 0.55f, 0.8f), Vec3(0.95f, 1.0f, 1.0f));
            c.out->bind();
            Mat4 vp = worldProj(c, w) * worldView(w);
            Mat4 m = Mat4::translate(w.eye.x, w.eye.y, w.eye.z - 2.2f + 0.3f * std::sin(local)) * Mat4::rotZ(local * 0.8f) * Mat4::rotY(0.4f * std::sin(local * 1.3f)) *
                     Mat4::scale(0.75f, 0.6f, 1);
            Texture ct = asTexture(card);
            drawQuad3D(c, ct, vp * m, 1.0f, Vec3(1, 1, 1), false, true);
        }
        c.flash(smoothstep(203.7f, 204.1f, t) * (1.0f - smoothstep(204.1f, 205.2f, t)), Vec3(0.85f, 1.0f, 0.85f));
        c.flash(1.0f - smoothstep(197.4f, 197.8f, t), Vec3(1, 1, 1));
    }
};
REGISTER_SCENE(FaceFloor, "facefloor");

// 3:29 the dance (VS), then the people at their PCs (ALEX)
struct Dance : Scene {
    void render(Ctx& c, float, float) override {
        const float t = VT(c.T);
        float local = t - 208.4f;
        int nv = c.videoFrames("VS");
        float vsEnd = nv / 19.5f;
        setBlend(Blend::None);
        if (local < vsEnd) c.drawImage(c.video("VS", local * 19.5f), 0, 0, 1, 1, 1.0f, 0, 0, 1, 1, 0, Vec3(1, 1, 1), true);
        else c.drawImage(c.video("ALEX", (local - vsEnd) * 19.5f), 0, 0, 1, 1, 1.0f, 0, 0, 1, 1, 0, Vec3(1, 1, 1), true);
        c.flash(1.0f - smoothstep(208.4f, 208.9f, t), Vec3(0, 0, 0));
        c.flash(smoothstep(217.4f, 217.9f, t), Vec3(1, 1, 1));
    }
};
REGISTER_SCENE(Dance, "dance");

// 3:38 icon wallpaper with the credit lines
struct Icons : Scene {
    void render(Ctx& c, float, float) override {
        const float t = VT(c.T);
        float local = t - 217.9f;
        Texture& f = c.tex("final");
        const float H = 350.0f;
        bool inverted = t > 229.6f && t < 230.4f;
        gl::ClearColor(inverted ? 1.0f : 0.0f, inverted ? 1.0f : 0.0f, inverted ? 1.0f : 0.0f, 1);
        gl::Clear(GL_COLOR_BUFFER_BIT);
        // tiled icon rows, scrolling sideways in alternate directions
        float rowH = 0.21f, iconW = rowH * (320.0f / 36.0f) / c.aspect;
        float bright = mix(0.3f, 1.0f, smoothstep(224.0f, 226.0f, t)) + (inverted ? 0.6f : 0.0f);
        setBlend(Blend::Alpha);
        for (int r = -1; r < 9; r++) {
            float y = r * rowH;
            float off = std::fmod(local * (r % 2 ? 0.03f : -0.03f) + r * 0.37f, iconW);
            for (float x = -iconW + off; x < 1.0f + iconW; x += iconW)
                c.drawImage(f, x, y, iconW, rowH, bright, 0, 71.0f / H, 1, 107.0f / H, 0,
                            inverted ? Vec3(0.1f, 0.1f, 0.1f) : Vec3(0.75f, 0.75f, 0.8f), true, true);
        }
        // the credit lines slide in with horizontal smear
        struct L { float t0, t1, v0, v1, w; };
        static const L lines[] = {{221.0f, 224.3f, 1, 26, 0.55f}, {224.4f, 227.6f, 35, 61, 0.85f}, {227.6f, 229.6f, 125, 148, 0.85f}};
        for (const L& l : lines) {
            if (t < l.t0 || t > l.t1) continue;
            float in = smoothstep(l.t0, l.t0 + 0.5f, t), out = 1.0f - smoothstep(l.t1 - 0.3f, l.t1, t);
            float hh = (l.v1 - l.v0) / 26.0f * 0.13f, ww = std::min(1.0f, l.w * 1.3f);
            float x = 0.5f - ww * 0.5f + (1.0f - in) * 0.3f;
            c.rect(0, 0.465f - hh * 0.7f, 1, hh * 1.4f, Vec3(0, 0, 0), 0.85f * in * out);
            for (int g = -3; g <= 3; g++)
                c.drawImage(f, x + g * 0.02f * (1.0f - in + 0.15f), 0.465f - hh * 0.5f, ww, hh, (g == 0 ? 1.0f : 0.18f) * out, 0, l.v0 / H, 1, l.v1 / H,
                            0, Vec3(1, 1, 1), true, true);
        }
        c.flash(1.0f - smoothstep(217.9f, 218.6f, t), Vec3(1, 1, 1));
    }
};
REGISTER_SCENE(Icons, "icons");

// 3:51 the MC-303
struct Outro : Scene {
    void render(Ctx& c, float, float) override {
        const float t = VT(c.T);
        float local = t - 230.6f;
        Texture& f = c.tex("final");
        float z = 1.12f + 0.012f * local;
        float flicker = (t > 235.6f && t < 236.2f) ? 1.0f : 0.0f;
        setBlend(Blend::None);
        c.drawImage(f, 0.5f - 0.5f * z, 0.5f - 0.5f * z, z, z, 1.0f, 0, 155.0f / 350.0f, 1, 1.0f, -0.03f, Vec3(0.82f, 0.86f, 0.95f), true, false);
        // vignette
        static Shader v;
        static bool built = false;
        if (!built) {
            v.build(kFullscreenVS, R"(
in vec2 uv; out vec4 o; uniform float amount;
void main() { vec2 q = uv - 0.5; float d = dot(q, q) * 2.6; o = vec4(0.0, 0.0, 0.0, clamp(d * d * amount, 0.0, 1.0)); }
)",
                    "vignette");
            built = true;
        }
        setBlend(Blend::Alpha);
        v.use();
        v.set("amount", 1.6f);
        drawFullscreen();
        c.flash(1.0f - smoothstep(230.6f, 231.6f, t), Vec3(1, 1, 1));
        c.flash(flicker * 0.9f, Vec3(0.92f, 0.94f, 0.96f));
        c.flash(smoothstep(237.6f, 237.8f, t) * (1.0f - smoothstep(237.8f, 238.4f, t)) * 0.6f, Vec3(0.3f, 0.32f, 0.35f));
        c.fade(smoothstep(242.0f, 252.5f, t));
    }
};
REGISTER_SCENE(Outro, "outro");
}  // namespace
