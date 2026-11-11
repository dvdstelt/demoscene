// The original's 3D renderer (0x3fb3 transform / light / cull, 0x43df faces + pencil edges) on the GPU, and the
// exact integer port of 0x3fb3 (see render3d.h).
#include "render3d.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace paper {

namespace {
// GL depth range of the faces (the original's own near limit is the 400-unit face cull)
constexpr double kZNear = 100.0, kZFar = 400000.0;
const float kDepthA = float((kZFar + kZNear) / (kZFar - kZNear));
const float kDepthB = float(-2.0 * kZFar * kZNear / (kZFar - kZNear));

// camera space -> clip: x_vga = 160 + 224 x / z -> x_clip = 1.4 x / z;  y_vga = 100 + 224 y / z -> y_clip = -2.24 y / z
const char* kFaceVS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aCol;
uniform vec2 uDepthAB;
flat out vec3 vCol;
void main() {
    vCol = aCol;
    gl_Position = vec4(1.4 * aPos.x, -2.24 * aPos.y, uDepthAB.x * aPos.z + uDepthAB.y, aPos.z);
}
)";
const char* kFaceFS = R"(
flat in vec3 vCol;
layout(location = 0) out vec4 oColour;
layout(location = 1) out vec4 oDark;
void main() { oColour = vec4(vCol, 1.0); oDark = vec4(0.0); }
)";
// resolved faces (premultiplied by their coverage) over the frame
const char* kCompositeFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D uFaces;
void main() { o = texelFetch(uFaces, ivec2(gl_FragCoord.xy), 0); }
)";
// ink = outline darkness + ink * (1 - face coverage)
const char* kMergeFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D uFaces;
uniform sampler2D uDark;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    o = vec4(texelFetch(uDark, p, 0).r, 0.0, 0.0, texelFetch(uFaces, p, 0).a);
}
)";

Vec2f projectCam(const Vec3d& c) {
    return {float(160.0 + Renderer3D::kFocal * c.x / c.z), float(100.0 + Renderer3D::kFocal * c.y / c.z)};
}
}  // namespace

bool Camera3D::project(const Vec3d& world, Vec2f& vga, double* z) const {
    const Vec3d c = rot * (world - pos);
    if (z) *z = c.z;
    if (c.z <= Renderer3D::kNear) return false;
    vga = projectCam(c);
    return true;
}

// ---------------------------------------------------------------- multisampled target
struct Renderer3D::Msaa {
    int w = 0, h = 0;
    GLuint fbo = 0, col = 0, dark = 0, depth = 0;
    void destroy() {
        if (fbo) gl::DeleteFramebuffers(1, &fbo);
        GLuint rb[3] = {col, dark, depth};
        gl::DeleteRenderbuffers(3, rb);
        fbo = col = dark = depth = 0;
    }
    bool create(int w_, int h_, int samples) {
        w = w_;
        h = h_;
        gl::GenFramebuffers(1, &fbo);
        gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
        auto rb = [&](GLuint& id, GLenum fmt, GLenum att) {
            gl::GenRenderbuffers(1, &id);
            gl::BindRenderbuffer(GL_RENDERBUFFER, id);
            gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, fmt, w, h);
            gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, att, GL_RENDERBUFFER, id);
        };
        rb(col, GL_RGBA8, GL_COLOR_ATTACHMENT0);
        rb(dark, GL_R16F, GL_COLOR_ATTACHMENT1);
        rb(depth, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL_ATTACHMENT);
        const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        gl::DrawBuffers(2, bufs);
        const bool ok = gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!ok) destroy();
        return ok;
    }
};

Renderer3D::Renderer3D(Ctx& c) {
    GLint maxSamples = 4;
    gl::GetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    samples_ = std::clamp(int(maxSamples), 1, 4);
    faceShader_.build(kFaceVS, kFaceFS, "3d-faces");
    compositeShader_.build(kFullscreenVS, kCompositeFS, "3d-composite");
    mergeShader_.build(kFullscreenVS, kMergeFS, "3d-merge");
    gl::GenVertexArrays(1, &vao_);
    gl::GenBuffers(1, &vbo_);
    gl::BindVertexArray(vao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(FaceVert), (void*)0);
    gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(FaceVert), (void*)(3 * sizeof(float)));
    gl::EnableVertexAttribArray(0);
    gl::EnableVertexAttribArray(1);
    gl::BindVertexArray(0);
    c.shared<PencilGpu>();
}

// No GL calls: shared objects outlive the context (Demo is destroyed after it); the context frees the buffers.
Renderer3D::~Renderer3D() {
    for (Msaa* m : cache_) delete m;
}

Renderer3D::Msaa& Renderer3D::msaaFor(int w, int h) {
    for (size_t i = 0; i < cache_.size(); i++)
        if (cache_[i]->w == w && cache_[i]->h == h) {
            std::rotate(cache_.begin(), cache_.begin() + long(i), cache_.begin() + long(i) + 1);  // most recent first
            return *cache_[0];
        }
    if (cache_.size() >= 2) {  // window resizes: keep the two most recent sizes
        cache_.back()->destroy();
        delete cache_.back();
        cache_.pop_back();
    }
    auto* m = new Msaa;
    while (!m->create(w, h, samples_) && samples_ > 1) samples_ /= 2;
    if (!m->fbo) SDL_Log("3d: cannot create a %dx%d multisampled target", w, h);
    cache_.insert(cache_.begin(), m);
    return *m;
}

// ---------------------------------------------------------------- pass
void Renderer3D::begin(Ctx& c, Target& frame, const View3D& view) {
    c_ = &c;
    frame_ = &frame;
    view_ = view;
    cur_ = &msaaFor(frame.w, frame.h);
    gl::BindFramebuffer(GL_FRAMEBUFFER, cur_->fbo);
    gl::Viewport(0, 0, frame.w, frame.h);
    gl::ClearColor(0, 0, 0, 0);
    gl::DepthMask(GL_TRUE);
    gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer3D::group(std::initializer_list<const Object3D*> objs, const std::vector<FaceLine>& lines) {
    group(std::vector<const Object3D*>(objs), lines);
}

void Renderer3D::group(const std::vector<const Object3D*>& objs, const std::vector<FaceLine>& lines) {
    if (!cur_ || !cur_->fbo) return;
    faces_.clear();
    passes_.clear();
    for (const Object3D* o : objs)
        if (o) emitObject(*o, lines);
    gl::BindFramebuffer(GL_FRAMEBUFFER, cur_->fbo);
    gl::Viewport(0, 0, cur_->w, cur_->h);
    gl::Disable(GL_CULL_FACE);
    gl::Enable(GL_DEPTH_TEST);
    if (!faces_.empty()) {  // faces: colour, darkness 0 (they cover earlier groups' lines), depth
        setBlend(Blend::None);
        gl::DepthFunc(GL_LESS);
        gl::DepthMask(GL_TRUE);
        faceShader_.use();
        faceShader_.set("uDepthAB", kDepthA, kDepthB);
        gl::BindVertexArray(vao_);
        gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(faces_.size() * sizeof(FaceVert)), faces_.data(), GL_STREAM_DRAW);
        gl::DrawArrays(GL_TRIANGLES, 0, GLsizei(faces_.size()));
        gl::BindVertexArray(0);
    }
    if (!passes_.empty()) {  // edges: added darkness, depth-tested against the group's faces
        PencilGpu& pg = c_->shared<PencilGpu>();
        gl::BlendEquation(GL_FUNC_ADD);
        gl::BlendFunc(GL_ONE, GL_ONE);
        gl::Enable(GL_BLEND);
        gl::DepthFunc(GL_LEQUAL);
        gl::DepthMask(GL_FALSE);
        pg.strokeMrt.use();
        pg.strokeMrt.set("uDepthAB", kDepthA, kDepthB);
        pg.drawPasses(passes_, pg.strokeMrt, cur_->w, cur_->h);
    }
    setBlend(Blend::None);
    gl::DepthMask(GL_TRUE);
    gl::Clear(GL_DEPTH_BUFFER_BIT);  // the next group is painted over this one
    gl::Disable(GL_DEPTH_TEST);
}

void Renderer3D::emitObject(const Object3D& o, const std::vector<FaceLine>& lines) {
    const Camera3D& cam = view_.cam;
    const Mat3d C = cam.rot * o.rot;
    const Vec3d T = cam.rot * (o.pos - cam.pos);
    const size_t nv = o.vert.size(), nf = o.face.size();
    cam_.resize(nv);
    scr_.resize(nv);
    for (size_t i = 0; i < nv; i++) {
        cam_[i] = C * o.vert[i] + T;
        if (cam_[i].z > kNear) scr_[i] = projectCam(cam_[i]);
    }
    // 0x3fb3 visibility: all vertices beyond z = 400 and clockwise on screen (y down)
    vis_.assign(nf, 0);
    for (size_t f = 0; f < nf; f++) {
        const Face3D& F = o.face[f];
        if (cam_[F.v[0]].z <= kNear || cam_[F.v[1]].z <= kNear || cam_[F.v[2]].z <= kNear) continue;
        const Vec2f &p0 = scr_[F.v[0]], &p1 = scr_[F.v[1]], &p2 = scr_[F.v[2]];
        vis_[f] = double(p2.y - p0.y) * double(p1.x - p0.x) > double(p2.x - p0.x) * double(p1.y - p0.y);
    }
    // light direction in object space (0x3fb3: rot^T (light - pos), |L| = 200)
    Vec3d L = o.lightOverride ? o.light : o.rot.transposed() * (view_.light - o.pos);
    L = L.norm();
    const int32_t Li[3] = {int32_t(std::nearbyint(200 * L.x)), int32_t(std::nearbyint(200 * L.y)),
                           int32_t(std::nearbyint(200 * L.z))};
    const ShadePalette& pal = view_.palette ? *view_.palette : c_->shared<PencilGpu>().defaultPalette;
    const PencilTables& tables = c_->shared<PencilGpu>().tables;
    const Mode3D mode = view_.mode;

    auto facePlane = [&](size_t f, float plane[4]) {
        const Vec3d n = C * o.normal[f];
        plane[0] = float(n.x);
        plane[1] = float(n.y);
        plane[2] = float(n.z);
        plane[3] = float(n.dot(cam_[o.face[f].v[0]]));
    };
    for (size_t f = 0; f < nf; f++) {
        if (!vis_[f]) continue;
        const Face3D& F = o.face[f];
        double index;
        int ramp;
        if (mode == Mode3D::Sketch && F.add) {  // (base + light + 0x20) leaves the ramp -> paper 0x3a
            index = ramp = 0x3a;
        } else {
            double s;
            if (view_.exactShade) s = double((F.n[0] * Li[0] + F.n[1] * Li[1] + F.n[2] * Li[2]) >> 12);
            else s = 12.5 * o.normal[f].dot(L) - 0.5;
            index = F.base + s;
            ramp = F.base;
        }
        const Vec3 col = pal.at(index, ramp);
        for (int k = 0; k < 3; k++) {
            const Vec3d& p = cam_[F.v[k]];
            faces_.push_back({float(p.x), float(p.y), float(p.z), col.x, col.y, col.z});
        }
        if (mode == Mode3D::Pencil) continue;
        uint8_t bits = F.outline;
        for (int k = 0; k < 3; k++)
            if (!vis_[F.nb[k]]) bits |= uint8_t(1 << k);  // silhouette: the face across this edge is hidden
        if (mode == Mode3D::Sketch) bits &= F.sketch;
        if (!bits) continue;
        float plane[4];
        facePlane(f, plane);
        for (int k = 0; k < 3; k++) {
            if (!(bits & (1 << k))) continue;
            const int a = F.v[k], b = F.v[(k + 1) % 3];
            const float zinv[2] = {float(1.0 / cam_[size_t(a)].z), float(1.0 / cam_[size_t(b)].z)};
            addPencilPasses(tables, PencilLine(scr_[size_t(a)], scr_[size_t(b)]), passes_, plane, zinv);
        }
    }
    for (const FaceLine& fl : lines) {
        if (fl.obj != &o || fl.face < 0 || size_t(fl.face) >= nf || !vis_[size_t(fl.face)]) continue;
        float plane[4];
        facePlane(size_t(fl.face), plane);
        const Face3D& F = o.face[size_t(fl.face)];
        const double zf = (cam_[F.v[0]].z + cam_[F.v[1]].z + cam_[F.v[2]].z) / 3.0;
        auto zAt = [&](Vec2f p) {
            const double nr = plane[0] * (p.x - 160.0) / kFocal + plane[1] * (p.y - 100.0) / kFocal + plane[2];
            const double z = std::abs(nr) > 1e-12 ? plane[3] / nr : zf;
            return float(1.0 / (z > kNear ? z : zf));
        };
        const float zinv[2] = {zAt(fl.line.a), zAt(fl.line.b)};
        addPencilPasses(tables, fl.line, passes_, plane, zinv);
    }
}

void Renderer3D::end(PencilInk* ink) {
    if (!cur_ || !cur_->fbo || !c_) return;
    Ctx& c = *c_;
    Target& frame = *frame_;
    auto faceT = c.tempSized(frame.w, frame.h, false, GL_RGBA8);
    auto darkT = c.tempSized(frame.w, frame.h, false, GL_R16F);
    // resolve the samples
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, cur_->fbo);
    gl::ReadBuffer(GL_COLOR_ATTACHMENT0);
    gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, faceT->fbo);
    gl::BlitFramebuffer(0, 0, cur_->w, cur_->h, 0, 0, faceT->w, faceT->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl::ReadBuffer(GL_COLOR_ATTACHMENT1);
    gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, darkT->fbo);
    gl::BlitFramebuffer(0, 0, cur_->w, cur_->h, 0, 0, darkT->w, darkT->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl::ReadBuffer(GL_COLOR_ATTACHMENT0);
    // faces over the frame
    gl::Disable(GL_DEPTH_TEST);
    frame.bind();
    setBlend(Blend::Premul);
    compositeShader_.use();
    compositeShader_.set("uFaces", 0);
    faceT->bindTex(0);
    drawFullscreen();
    // outline darkness into the ink, covering the ink drawn before
    std::unique_ptr<PencilInk> own;
    if (!ink) {
        own = std::make_unique<PencilInk>(c, frame);
        ink = own.get();
    }
    Target& it = ink->target();
    it.bind();
    gl::BlendEquation(GL_FUNC_ADD);
    gl::BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gl::Enable(GL_BLEND);
    mergeShader_.use();
    mergeShader_.set("uFaces", 0);
    mergeShader_.set("uDark", 1);
    faceT->bindTex(0);
    darkT->bindTex(1);
    drawFullscreen();
    setBlend(Blend::None);
    gl::ActiveTexture(GL_TEXTURE0);
    if (own) own->resolve(c, frame, view_.palette);
    cur_ = nullptr;
    if (c.out) c.out->bind();
}

// ---------------------------------------------------------------- exact integer port of 0x3fb3
namespace {
int32_t mul32(int32_t a, int32_t b) { return int32_t(uint32_t(a) * uint32_t(b)); }
int32_t sub32(int32_t a, int32_t b) { return int32_t(uint32_t(a) - uint32_t(b)); }
int32_t dot32(const int32_t* r, const IVec3& v) {
    return int32_t(uint32_t(mul32(r[0], v.x)) + uint32_t(mul32(r[1], v.y)) + uint32_t(mul32(r[2], v.z)));
}
}  // namespace

void transformExact(const Object3D& o, const IMat3& R, const IVec3& pos, const ExactCamera& cam, ExactFrame& out) {
    const IMat3 C = imatMul(cam.rot, R);
    const IVec3 T = imulVec(cam.rot, IVec3(sub32(pos.x, cam.pos.x), sub32(pos.y, cam.pos.y), sub32(pos.z, cam.pos.z)));
    const IVec3 L = imulTVecShr16(R, IVec3(sub32(cam.light.x, pos.x), sub32(cam.light.y, pos.y), sub32(cam.light.z, pos.z)));
    const long double len =
        std::sqrt(std::fabs((long double)L.x * L.x + (long double)L.y * L.y + (long double)L.z * L.z)) / 200.0L;
    IVec3 Lo;
    if (len > 0)
        for (int k = 0; k < 3; k++) Lo[k] = int32_t(std::nearbyint((long double)L[k] / len));
    out.lightObj = Lo;
    const size_t nv = o.ivert.size(), nf = o.face.size();
    out.cz.assign(nv, 0);
    out.sx.assign(nv, 0);
    out.sy.assign(nv, 0);
    constexpr int32_t kZ400 = 0x1900000;
    for (size_t i = 0; i < nv; i++) {
        const IVec3& v = o.ivert[i];
        const int32_t z = int32_t(uint32_t(dot32(&C.m[6], v)) + uint32_t(T.z));
        out.cz[i] = z;
        if (z <= kZ400) continue;
        const int32_t e = 0xe00000 / (z >> 16);
        const int32_t x = int32_t(uint32_t(dot32(&C.m[0], v)) + uint32_t(T.x));
        const int32_t y = int32_t(uint32_t(dot32(&C.m[3], v)) + uint32_t(T.y));
        out.sx[i] = int32_t(uint32_t(int32_t((int64_t(x) * e) >> 27)) + 0x1400u);
        out.sy[i] = int32_t(uint32_t(int32_t((int64_t(y) * e) >> 27)) + 0xc80u);
    }
    out.flags.assign(nf, 0);
    out.colour.assign(nf, 0);
    out.bucket.assign(nf, -1);
    std::vector<uint8_t> vis(nf, 0);
    for (size_t f = 0; f < nf; f++) {
        const Face3D& F = o.face[f];
        const int a = F.v[0], b = F.v[1], c = F.v[2];
        if (out.cz[size_t(a)] <= kZ400 || out.cz[size_t(b)] <= kZ400 || out.cz[size_t(c)] <= kZ400) continue;
        const int32_t e1x = sub32(out.sx[size_t(b)], out.sx[size_t(a)]), e1y = sub32(out.sy[size_t(b)], out.sy[size_t(a)]);
        const int32_t e2x = sub32(out.sx[size_t(c)], out.sx[size_t(a)]), e2y = sub32(out.sy[size_t(c)], out.sy[size_t(a)]);
        if (!(mul32(e2y, e1x) > mul32(e2x, e1y))) continue;
        vis[f] = 1;
        const int32_t s = int32_t(uint32_t(F.n[0] * Lo.x) + uint32_t(F.n[1] * Lo.y) + uint32_t(F.n[2] * Lo.z)) >> 12;
        uint8_t al = uint8_t(s + F.base);
        const uint8_t ah = al >> 5;
        if (cam.mode == 2) al = uint8_t(al + F.add);
        out.colour[f] = (al >> 5) != ah ? 0x3a : al;
    }
    for (size_t f = 0; f < nf; f++) {
        const Face3D& F = o.face[f];
        uint8_t fl = uint8_t(F.outline << 3);
        if (!vis[f]) { out.flags[f] = fl | 0x80; continue; }
        for (int k = 0; k < 3; k++)
            if (!vis[F.nb[k]]) fl |= uint8_t(1 << k);
        out.flags[f] = fl;
        const int32_t sum = (out.cz[F.v[0]] >> 16) + (out.cz[F.v[1]] >> 16) + (out.cz[F.v[2]] >> 16) - 1200;
        out.bucket[f] = std::clamp(sum >> cam.bucketShift, 0, 1023);
    }
}

std::vector<std::pair<int, int>> paintersOrder(const std::vector<const ExactFrame*>& group) {
    std::vector<std::vector<std::pair<int, int>>> buckets(1024);
    for (size_t oi = 0; oi < group.size(); oi++)
        for (size_t f = 0; f < group[oi]->bucket.size(); f++)
            if (group[oi]->bucket[f] >= 0) buckets[size_t(group[oi]->bucket[f])].push_back({int(oi), int(f)});
    std::vector<std::pair<int, int>> order;
    for (int b = 1023; b >= 0; b--)
        for (auto it = buckets[size_t(b)].rbegin(); it != buckets[size_t(b)].rend(); ++it) order.push_back(*it);
    return order;
}

}  // namespace paper
