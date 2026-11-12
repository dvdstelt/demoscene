// Test scenes for the engine checker (enginetest.cpp): minimal ports of how p2, p3, p5, p6 and p7 use the engine, to
// compare it with the capture through the real timeline (tools/compare.py). They are registered under the parts'
// scene names only inside the checker. Motion from the notes' script tables (p2: notes-p2 9.1 with the measured step
// starts of 9.2; p3: notes-p3p5 3.1 / 3.2; p5: 4.2 / 4.3; p6: notes-p6p7 1, 3.1, 3.2, 3.3; p7: 1, 4.3), evaluated with
// dope::Integrator; backgrounds and labels only as far as they help to compare. Left out: p2's credits, blur and
// white fade, p5's hand-over, p6's exit fade, p7's "dope" (not the engine's). They are not the parts: the maps the
// parts build per frame (p6's shadowed light map and morph, p7's light map) are made here the plain way, on the CPU.
#include <algorithm>
#include <cmath>

#include "demo.h"
#include "dope/core.h"
#include "dope/engine.h"
#include "exe.h"

using namespace dope;

Order g_testOrder = Order::Depth;   // enginetest --painter
extern bool g_keyDepth;            // enginetest --truedepth: false

namespace {

// The camera slot, moved per retrace: its position and angles as integrators.
struct Motion {
    Integrator x, y, z, a, b, c;
    Pose at(double r) const { return Pose{x.at(r), y.at(r), z.at(r), a.at(r), b.at(r), c.at(r)}; }
    Motion& start(const Pose& p, const Pose& v) {
        x = {p.x, v.x}, y = {p.y, v.y}, z = {p.z, v.z}, a = {p.a, v.a}, b = {p.b, v.b}, c = {p.c, v.c};
        return *this;
    }
    // the script sets these before the call that starts the step at k
    Motion& vel(int k, double vx, double vy, double vz) {
        x.velocity(k, vx), y.velocity(k, vy), z.velocity(k, vz);
        return *this;
    }
    Motion& avel(int k, double va, double vb, double vc) {
        a.velocity(k, va), b.velocity(k, vb), c.velocity(k, vc);
        return *this;
    }
    Motion& set(int k, const Pose& p) {
        x.value(k, p.x), y.value(k, p.y), z.value(k, p.z), a.value(k, p.a), b.value(k, p.b), c.value(k, p.c);
        return *this;
    }
};

Engine& engine(Ctx& c) { return c.shared<Engine>(); }

// ------------------------------------------------------------------------------------------------- p2
// The discs (record 184a, mesh 1e71, f 4000 from the script) and the "C" (1884, mesh 2121, f 1300, shown from step 25)
// in the camera slot 1810's motion; env maps 6538 / 6540 (p2-envmaps), bump map 23d1 through the table at 0010:0000;
// the backdrop from step 25. The "C" is depth-tested with its key depth (Object::depthScale = 1300 / 4000).
struct P2Test : Scene {
    EngineMesh discs, cee;
    ObjectRecord rDiscs, rCee;
    BumpMap bump;
    Materials mat;
    Motion cam;
    LabelSlide label{-1, {{3543, 1}, {5195, -1}}};

    void init(Ctx& c) override {
        const DopeImage& img = c.exe->part(2);
        discs.load(img, 0x1e71);
        cee.load(img, 0x2121);
        rDiscs = ObjectRecord::read(img, 0x184a);
        rCee = ObjectRecord::read(img, 0x1884);
        bump.create(img, 0x23d1, bumpTable(img, 0x0000), EnginePart::of(2));
        const Picture& env = c.picture("p2-envmaps");
        mat.env[0] = EnvMap::picture(env, 0), mat.env[1] = EnvMap::picture(env, 1);   // 6538, 6540
        mat.env[2] = mat.env[0], mat.env[3] = mat.env[1];                            // 6d38 (never used), 6540
        mat.tex[0] = &bump;                                                          // 23d1, 0, 0, 0
        // 9.1 with 9.2's starts (steps 149..152 12 retraces late)
        cam.start({0, 0, 1152000, 0, 0, 0}, {0, 0, 4000, 0, 50, 0});
        cam.vel(438, 0, 0, 1500).vel(718, 0, 0, 0).vel(2818, 0, 0, 10000).vel(2863, 0, 0, 6000).vel(3053, 0, 0, 0);
        cam.vel(3333, 0, 0, -5500).avel(3333, 0, 50, -36).avel(3543, 0, 50, -56).vel(3683, 0, 0, -3800);
        cam.vel(4173, 0, 0, 0).vel(5405, 0, 0, 3000).vel(6137, 0, 0, 0).avel(6137, 10, 50, -76);
        cam.vel(6487, 0, 0, -2400).vel(7063, 0, 0, 0).avel(7063, 20, 50, -86).vel(7273, 0, 0, 0);
    }

    static double pivot(int k) {   // the discs' pivot x [1878]: steps 7..22
        if (k < 2818) return 6000;
        if (k >= 2863) return 0;
        return k < 2821 ? 5800 : 5400 - 400 * ((k - 2821) / 3);
    }

    void render(Ctx& c, double t) override {
        double r = std::clamp(c.retraces(t), 0.0, 7309.0);
        int k = int(std::floor(r));
        bool stand = k >= 3123;   // step 25: the "C", the backdrop, [194f]--
        if (stand) c.drawPicture(c.picture("p2-background"));
        Object d = Object::from(rDiscs, &discs);
        d.f = 4000;
        d.pivot[0] = pivot(k);
        Object cc = Object::from(rCee, &cee);
        cc.f = 1300;
        cc.envAdd = cc.texAdd = 1;
        cc.depthScale = g_keyDepth ? 1300.0 / 4000.0 : 1.0;
        mat.envAdd = stand ? -1 : 0;
        Engine& eng = engine(c);
        eng.begin(c, EnginePart::of(2), cam.at(r), View{}, g_testOrder);
        eng.draw(d, mat);
        if (stand) eng.draw(cc, mat);
        eng.end();
        c.drawPicture(c.picture("p2-label-blur"), labelDraw(label.position(r + 1)));
    }
};
REGISTER_SCENE(P2Test, "p2");

// ------------------------------------------------------------------------------------------------- p3
// Object 4 (record 18f8, mesh 1362, f 2500) in the camera slot's motion, the Phong map 0337 (p3-phongmap, left half),
// bump texture 2341 through the table at 0010:0000 addressed by the object-space normal with the 15-bit wrap.
struct P3Test : Scene {
    EngineMesh mesh;
    ObjectRecord rec;
    BumpMap bump;
    Materials mat;
    Motion cam;
    LabelSlide label{-1, {{1120, 1}, {3360, -1}}};

    void init(Ctx& c) override {
        const DopeImage& img = c.exe->part(3);
        mesh.load(img, 0x1362);
        rec = ObjectRecord::read(img, 0x18f8);
        bump.create(img, 0x2341, bumpTable(img, 0x0000), EnginePart::of(3));
        EnvMap phong = EnvMap::picture(c.picture("p3-phongmap"), 0);
        for (int i = 0; i < 4; i++) mat.env[i] = phong, mat.tex[i] = &bump;   // selector 0: 0337, 2341
        cam.start({0, 0, 8192000, 0, 0, 0}, {0, 0, -2600, -110, 20, 10});
        for (int i = 1; i < 8; i++) cam.avel(140 * i, -110, 20 + i, 10 + 2 * i);
        for (int i = 0; i < 7; i++) cam.avel(1120 + 140 * i, -110, 28 + i, 26 + 2 * i);
        cam.vel(2100, 0, 0, -1100).vel(2240, 0, 0, -600).vel(2380, 0, 0, 0).vel(2800, 1250, 0, -800);
        cam.vel(2940, 2650, 0, -1600);
    }

    void render(Ctx& c, double t) override {
        if (t < c.start) return c.clear(Vec3(0, 0, 3.0f / 63));   // the set-up screen: colour 0
        double r = std::clamp(c.retraces(t), 0.0, 3604.0);
        drawFrame(c, {&c.picture("p3-nebula"), &c.picture("p3-label-phong"), label.position(r + 1)}, [&] {
            Engine& eng = engine(c);
            eng.begin(c, EnginePart::of(3), cam.at(r), View{}, g_testOrder);
            eng.draw(Object::from(rec, &mesh), mat);
            eng.end();
        });
    }
};
REGISTER_SCENE(P3Test, "p3");

// ------------------------------------------------------------------------------------------------- p5
// Object 1 (record 144c, mesh 1402, f 2500) in the camera slot 1412's motion. ENV (k < 1750): material add 0: the
// gold map 03d7 (p5-envmaps left), env only. BUMP: 0101h: the red map 03df (right) through texture 1c96 with 3099's
// offsets (k - 8) A / 256, A / 256 = 3 * 32767 |sin(pi phase / 2048)| / 65536 continuous.
struct P5Test : Scene {
    EngineMesh mesh;
    ObjectRecord rec;
    BumpMap bump;
    Materials mat;
    Motion cam;
    LabelSlide label{0, {{560, 1}, {1610, -1}, {2380, 1}, {4340, -1}}};

    void init(Ctx& c) override {
        const DopeImage& img = c.exe->part(5);
        mesh.load(img, 0x1402);
        rec = ObjectRecord::read(img, 0x144c);
        bump.create(img, 0x1c96, bumpNibbles(), EnginePart::of(5));
        const Picture& env = c.picture("p5-envmaps");
        mat.env[0] = EnvMap::picture(env, 0), mat.env[1] = EnvMap::picture(env, 1);
        mat.env[2] = mat.env[0], mat.env[3] = mat.env[1];
        mat.tex[1] = &bump;   // 0, 1c96, 0, 0
        cam.start({0, -1024000, 2304000, 0, 0, 0}, {0, 6200, 14000, -80, -30, 140});
        cam.vel(140, 0, 3200, 13000).vel(280, 0, 0, -18000).vel(560, 0, 0, -10000).vel(595, 0, 0, 10000);
        cam.vel(630, 0, 0, 13000).vel(1050, 0, 0, -13000).vel(1470, 0, 0, -10000).vel(1505, 0, 3000, 12000);
        cam.vel(1540, 0, 7000, 12000).vel(1610, 0, 15000, 12000).vel(1680, 0, 27000, 12000);
        cam.set(1750, {0, -1024000, 2048000, 0, 0, 0}).avel(1750, -20, 10, 90).vel(1750, 0, 5200, 16000);
        cam.avel(1890, -20, 20, 90).vel(1890, 0, 2200, 13000).avel(2030, -20, 30, 90).vel(2030, 0, 0, -11000);
        cam.avel(2380, -30, 40, 80).vel(2380, 0, 0, -10000).vel(2415, 0, 0, 10000).avel(2450, -40, 40, 80);
        cam.vel(2450, 0, 0, 10000).avel(2870, -50, 40, 80).vel(2870, 0, 0, -10000).avel(3290, -60, 40, 80);
        cam.vel(3325, 0, 0, 10000).vel(3360, 0, 0, 13000).vel(3780, 0, 0, -13000).vel(4200, 0, 0, -10000);
        cam.vel(4235, 0, 3000, 12000).vel(4270, 0, 7000, 12000).vel(4340, 0, 15000, 12000).vel(4410, 0, 27000, 12000);
    }

    void render(Ctx& c, double t) override {
        double r = std::clamp(c.retraces(t), 0.0, 4479.0);
        bool bumpy = r >= 1750;
        double phase = r <= 1750 ? 0 : r <= 4270 ? 24 * (r - 1750) : 60480 + 20 * (r - 4270);
        mat.bumpScale = float(3.0 * 32767.0 * std::fabs(std::sin(kTwoPi / 2 * phase / 2048.0)) / 65536.0);
        const Picture& lbl = c.picture(r < 2030 ? "p5-label-env" : "p5-label-bump");
        drawFrame(c, {&c.picture("p5-chequerboard"), &lbl, label.position(r + 1)}, [&] {
            Object o = Object::from(rec, &mesh);
            o.envAdd = o.texAdd = bumpy ? 1 : 0;   // [1476]
            Engine& eng = engine(c);
            eng.begin(c, EnginePart::of(5), cam.at(r), View{}, g_testOrder);
            eng.draw(o, mat);
            eng.end();
        });
    }
};
REGISTER_SCENE(P5Test, "p5");

// ------------------------------------------------------------------------------------------------- p6
// The cat (record 165a, mesh 1cc2, F 3500, +2a 0101h: the light map 0451 with the fur 23a0) and the rings (1694, mesh
// 202d, +2a 0200h: the cloud map 0449, env only) in the group slot 1620's motion over the swirl. The light map: the
// ramp at 0449:0080 with the shadow mask ((-b) & 3fffh) >> 9 of 368a:0000 (2e05), through pal1 / pal2 mixed by the
// morph m / 4096 (3895); the fur's offsets times k / 256 (3913).
struct P6Test : Scene {
    EngineMesh cat, rings;
    ObjectRecord rCat, rRings;
    BumpMap fur, flat;
    EnvTexture light;
    Materials mat;
    Motion group;
    Palette pal1{}, pal2{};
    LabelSlide label{-1, {{140, 1}, {1680, -1}, {1890, 1}, {3290, -1}, {3430, 1}, {3710, -1}}};

    void init(Ctx& c) override {
        const DopeImage& img = c.exe->part(6);
        cat.load(img, 0x1cc2);
        rings.load(img, 0x202d);
        rCat = ObjectRecord::read(img, 0x165a);
        rRings = ObjectRecord::read(img, 0x1694);
        fur.create(img, 0x23a0, bumpNibbles(), EnginePart::of(6));
        flat.create(img, 0x2398, bumpNibbles(), EnginePart::of(6));
        pal1 = vgaPalette(img.span(0x0419, 0x0000, 768));
        pal2 = vgaPalette(img.csSpan(0x3565, 768));
        mat.env[0] = mat.env[2] = EnvMap::picture(c.picture("p6-envmap"));   // 0449
        mat.tex[0] = &flat, mat.tex[1] = &fur;                                 // 2398, 23a0, 0, 0
        group.start({-3072000, 0, 4864000, 0, 0, 0}, {8000, 0, -8600, -4, 128, 0});
        group.vel(280, 5000, 0, 0).vel(420, 0, 0, 6000).vel(700, 0, 0, 0).avel(910, -14, 128, 0);
        group.vel(910, 0, 0, -5000).avel(1400, 14, 128, 0).vel(1400, 0, 0, 0).avel(1680, -4, 128, 0);
        group.vel(1680, 0, 0, 6000).avel(2100, 16, 128, 0).vel(2100, 0, 0, -3000).vel(2310, 0, 0, -4000);
        group.avel(2310, -8, 128, 0).vel(2730, 0, 0, 400).avel(2730, 0, 128, 0).vel(3010, 0, 0, 0);
        group.avel(3290, -4, 128, 0).avel(3430, 8, 128, 0).vel(3710, 6000, 0, -2600);
    }

    // bump strength k and morph m after n retraces (1cc0)
    static void tables(int n, int& k, int& m) {
        int dk = 0, dm = 0;
        k = 0x100, m = 0;
        for (int i = 0; i < n; i++) {
            if (i == 2310) dk = -4, dm = 16;
            if (i == 2730) dk = 6, dm = -24;
            if (i == 3010) dk = -6, dm = 24;
            m += dm;
            if (m < 0 || m >= 0x1000) dm = 0;
            k += dk;
            if (k < 0 || k >= 0x100) k -= dk, dk = 0;
        }
    }

    void render(Ctx& c, double t) override {
        const DopeImage& img = c.exe->part(6);
        double r = std::clamp(c.retraces(t), 0.0, 3990.0);
        int n = int(std::floor(r)), k, m;
        tables(n, k, m);
        Palette pal = mixPalettes(pal1, pal2, std::clamp(m / 4096.0, 0.0, 1.0));
        // 2e05: the light map's right half with the shadow mask of the group angle b = 128 n
        int mi = ((-128 * n) & 0x3fff) >> 9;
        std::vector<uint8_t> px(128 * 128);
        for (int y = 0; y < 128; y++)
            for (int x = 0; x < 128; x++) {
                int e = img.u8(0x0449, uint16_t(y * 256 + 128 + x));
                bool lit = img.file8(DopeImage::addr(0x368a, 0) + uint32_t(mi * 4096 + (y / 2) * 64 + x / 2)) & 1;
                px[size_t(y) * 128 + size_t(x)] = uint8_t(((e - 0x10) & 0x3f) + (lit ? 0x50 : 0x90));
            }
        light.set(px, 128, 128, 128, pal);
        mat.env[1] = mat.env[3] = light.map();
        mat.bumpScale = float(k / 256.0);
        const char* lname = n < 1890 ? "p6-label-shadow" : n < 3430 ? "p6-label-morph" : "p6-label-metal";
        drawFrame(c, {&c.picture("p6-swirl"), &c.picture(lname), label.position(r + 1)}, [&] {
            Object oc = Object::from(rCat, &cat), orr = Object::from(rRings, &rings);
            oc.pose.b -= 180 * r;   // the cat's own avel (0, -180, 0)
            oc.envAdd = oc.texAdd = 1;                // [1684] = 0101h
            orr.envAdd = 0, orr.texAdd = 2;           // [16be] = 0200h
            Engine& eng = engine(c);
            eng.begin(c, EnginePart::of(6), group.at(r), View{}, g_testOrder);
            eng.draw(oc, mat);
            eng.draw(orr, mat);
            eng.end();
        });
    }
};
REGISTER_SCENE(P6Test, "p6");

// ------------------------------------------------------------------------------------------------- p7
// The tentacle object (record 18f8, mesh 23b4, F 2500) in the group slot 1810's motion over the horizon; the light
// map 13b4 (base 03b3:0000 plus five spots 37ea:0000 on the paths of 3046, from t = the retraces) as a level map
// through the DAC (the window 17b8 = offset 4040h), the wavy bump 2fe9 through the table at 0010:0000 addressed by the
// object-space normal.
struct P7Test : Scene {
    EngineMesh mesh;
    ObjectRecord rec;
    BumpMap bump;
    Texture lightTex;
    PaletteTexture palTex;
    Materials mat;
    Motion group;
    LabelSlide label{-1, {{210, 1}, {700, -1}, {910, 1}, {1890, -1}}};

    void init(Ctx& c) override {
        const DopeImage& img = c.exe->part(7);
        mesh.load(img, 0x23b4);
        rec = ObjectRecord::read(img, 0x18f8);
        bump.create(img, 0x2fe9, bumpTable(img, 0x0000), EnginePart::of(7));
        Palette pal = vgaPalette(img.span(0x5a84, 0x0000, 720));
        palTex.set(pal);
        for (int i = 0; i < 4; i++) mat.tex[i] = &bump;   // 2fe9 (2ff1 for selector 1, unused)
        group.start({0, 2304000, 3584000, 0, 0, 0}, {0, -4000, 0, 0, 0, 48});
        group.vel(210, 0, -2000, 0).vel(700, 0, -1200, -3000).avel(700, -18, 7, 58).vel(770, 700, -500, -3000);
        group.avel(910, -18, 7, 68).vel(1330, 2400, 0, 0).avel(1330, -18, 7, 78);
    }

    void render(Ctx& c, double t) override {
        const DopeImage& img = c.exe->part(7);
        double r = std::clamp(c.retraces(t), 0.0, 2100.0);
        int tt = int(std::floor(r));
        // 3070: the light map
        auto S = [&](int i) { return int(sineEntry(img, kSineQuarter[7], i & 2047)); };
        auto C = [&](int i) { return int(sineEntry(img, kSineQuarter[7], (i & 2047) + 512)); };
        int i1 = (tt & 0xffe) / 2, i2 = ((tt << 3) & 0xffe) / 2, i3 = ((tt << 2) & 0xffe) / 2, i4 = ((tt << 1) & 0xffe) / 2;
        int j5 = ((3 * tt) & 0xffe) / 2, i5 = (S(j5) >> 4) & 0x7ff;
        int pos[5][2] = {{64, 64 + (S(i1) >> 9)},
                         {64 + (C(i2) >> 9), 64},
                         {64 + (S(i3) >> 9), 64 + (C(i3) >> 9)},
                         {64 + (C(i4) >> 9), 64 + (S(i4) >> 9)},
                         {64 + (S(i5) >> 9), 64 + (C(i5) >> 9)}};
        std::vector<float> lm(256 * 256);
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 256; x++) lm[size_t(y) * 256 + size_t(x)] = img.u8(0x03b3, uint16_t(y * 256 + x));
        for (auto& p : pos)
            for (int y = 0; y < 128; y++)
                for (int x = 0; x < 128; x++) {
                    int yy = p[0] + y, xx = p[1] + x;
                    if (yy >= 0 && yy < 256 && xx >= 0 && xx < 256)
                        lm[size_t(yy) * 256 + size_t(xx)] += img.u8(0x37ea, uint16_t(y * 128 + x));
                }
        for (float& v : lm) v /= 255.0f;
        if (!lightTex.id) lightTex.createRaw(256, 256, GL_R16F, GL_RED, GL_FLOAT, lm.data(), true, false, true);
        else {
            gl::BindTexture(GL_TEXTURE_2D, lightTex.id);
            gl::TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256, GL_RED, GL_FLOAT, lm.data());
            gl::GenerateMipmap(GL_TEXTURE_2D);
        }
        EnvMap lmap = EnvMap::levelMap(lightTex.id, 256, 256, 64, 64, 128, &palTex, 0, 255);
        lmap.clamp = false;
        for (auto& e : mat.env) e = lmap;
        const char* lname = tt < 910 ? "p7-label-light" : "p7-label-world";
        drawFrame(c, {&c.picture("p7-horizon"), &c.picture(lname), label.position(r + 1)}, [&] {
            Engine& eng = engine(c);
            eng.begin(c, EnginePart::of(7), group.at(r), View{}, g_testOrder);
            eng.draw(Object::from(rec, &mesh), mat);
            eng.end();
        });
    }
};
REGISTER_SCENE(P7Test, "p7");

}  // namespace
