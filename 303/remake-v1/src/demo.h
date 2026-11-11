// Demo framework: shared resources, scene interface, timeline.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "assets.h"
#include "exe.h"
#include "render.h"

struct Ctx;

struct Scene {
    virtual ~Scene() = default;
    virtual void init(Ctx&) {}
    // Draw into the currently bound target (ctx.out). t = seconds since cue start, d = cue length.
    virtual void render(Ctx& c, float t, float d) = 0;
};

using SceneFactory = std::function<Scene*()>;
std::map<std::string, SceneFactory>& sceneRegistry();
struct SceneRegistrar {
    SceneRegistrar(const char* name, SceneFactory f) { sceneRegistry()[name] = std::move(f); }
};
#define REGISTER_SCENE(cls, name) static SceneRegistrar s_reg_##cls(name, [] { return (Scene*)new cls; })

enum class Blend { None, Alpha, Add, Screen, Multiply, Premul };
void setBlend(Blend b);

// Glyph strip font (fixed-height bitmap with per-glyph extents measured from empty columns).
struct Font {
    Texture tex;
    int cellH = 0;
    struct Glyph { float u0, v0, u1, v1; int w; };
    std::map<char, Glyph> glyphs;
    int spaceW = 8;
};

struct Ctx {
    int w = 0, h = 0;       // render size (4:3 frame)
    float aspect = 4.0f / 3.0f;
    float T = 0;            // global music time
    float dt = 0;           // frame delta
    Pak pak;
    Exe303 exe;             // 303.EXE: lyric words, read by address (exe.h)
    Target* out = nullptr;  // current scene target
    Target scratch[3];      // same size as out, free for scenes
    Target half[2];         // half resolution scratch

    Texture& tex(const std::string& key);           // see demo.cpp for keys
    Texture& video(const std::string& name, float frame);  // ALEX/HARL/PARANOID/VS, bicubic-friendly
    int videoFrames(const std::string& name);
    GpuMesh& mesh(const std::string& name);
    const Mesh& meshData(const std::string& name);
    Font& font(const std::string& name);  // "lyrics", "outline"

    // 2D drawing in frame coordinates: x 0..1 left->right, y 0..1 top->bottom.
    void drawImage(const Texture& t, float x, float y, float w, float h, float alpha = 1.0f,
                   float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1, float angle = 0,
                   Vec3 tint = Vec3(1, 1, 1), bool bicubic = false, bool premulGray = false);
    void drawTarget(const Target& t, float alpha = 1.0f);
    // Draw a text string centred at (cx, cy) with given cap height (fraction of frame height).
    void drawText(const std::string& font, const std::string& s, float cx, float cy, float height,
                  float alpha = 1.0f, Vec3 color = Vec3(1, 1, 1), float tracking = 0.0f, float stretchX = 1.0f);
    float textWidth(const std::string& font, const std::string& s, float height, float tracking = 0.0f, float stretchX = 1.0f);
    void flash(float amount, Vec3 color = Vec3(1, 1, 1));  // full-frame overlay
    void rect(float x, float y, float w, float h, Vec3 color, float alpha = 1.0f);
    Texture& white();
    // Restrict drawing to a sub-rectangle of the current target (frame units); call resetViewport() after.
    void subViewport(float x, float y, float w, float h, bool clear = true, Vec3 clearColor = Vec3(0, 0, 0));
    void resetViewport();
    void fade(float amount, Vec3 color = Vec3(0, 0, 0)) { flash(amount, color); }

    Shader imageShader, flashShader;
    std::map<std::string, Texture> textures;
    std::map<std::string, std::unique_ptr<Video>> videos;
    std::map<std::string, Texture> videoTex;
    std::map<std::string, int> videoLoaded;
    std::map<std::string, Mesh> meshesData;
    std::map<std::string, GpuMesh> meshes;
    std::map<std::string, Font> fonts;
};

struct Cue {
    float start, end;
    std::string scene;
    Scene* inst = nullptr;
};

class Demo {
public:
    bool init(int w, int h, std::string* err);
    void resize(int w, int h);
    // Render the frame for music time T into the internal target; returns it.
    const Target& render(float T, float dt);
    float length() const;
    Ctx ctx;

private:
    std::vector<Cue> cues_;
    std::map<std::string, std::unique_ptr<Scene>> scenes_;
    Target frame_;
};
