#include "fx.h"

#include <cmath>

namespace {
const char* kMeshVS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in float aExtra;
uniform mat4 model, view, proj;
uniform float spikeScale;
uniform vec4 twist;
out vec3 vN;     // view space normal
out vec3 vV;     // view space position
out vec3 vObj;   // object space position
out vec3 vObjN;  // object space normal
void main() {
    vec3 p = aPos;
    vec3 nn = aNrm;
    if (spikeScale != 1.0) p = normalize(aPos) * mix(0.55, length(aPos), spikeScale);
    if (twist != vec4(0.0)) {
        float az = aExtra * twist.x + sin(aExtra * 0.45 + twist.z) * twist.w;
        float ay = aExtra * twist.y + cos(aExtra * 0.37 + twist.z * 1.3) * twist.w * 0.7;
        mat3 rz = mat3(cos(az), sin(az), 0.0, -sin(az), cos(az), 0.0, 0.0, 0.0, 1.0);
        mat3 ry = mat3(cos(ay), 0.0, -sin(ay), 0.0, 1.0, 0.0, sin(ay), 0.0, cos(ay));
        p = ry * rz * p;
        nn = ry * rz * nn;
    }
    vec4 w = model * vec4(p, 1.0);
    vec4 v = view * w;
    vN = mat3(view) * mat3(model) * nn;
    vV = v.xyz;
    vObj = p;
    vObjN = aNrm;
    gl_Position = proj * v;
}
)";

const char* kMeshFS = R"(
in vec3 vN;
in vec3 vV;
in vec3 vObj;
in vec3 vObjN;
out vec4 o;
uniform sampler2D env;
uniform vec3 tint;
uniform float exposure, specular, alpha, param;
uniform int mode;
uniform vec3 fogColor;
uniform vec2 fogRange;
void main() {
    vec3 n = normalize(vN);
    if (!gl_FrontFacing) n = -n;
    vec3 v = normalize(-vV);
    float depth = -vV.z;
    vec3 col;
    if (mode == 1) {
        // contour bands (white outlines on black), used by the black & white part
        float k = fract(length(vObj) * param);
        float w = fwidth(length(vObj) * param) * 1.5;
        float line = smoothstep(0.5 - w - 0.12, 0.5 - 0.12, k) - smoothstep(0.5 + 0.12, 0.5 + 0.12 + w, k);
        float rim = smoothstep(0.25, 0.0, abs(dot(n, v)));
        col = vec3(max(line, rim));
        o = vec4(col * tint, alpha);
        return;
    }
    if (mode == 2) {
        float f = pow(1.0 - abs(dot(n, v)), 2.0);
        col = tint * (0.15 + f * 1.2);
    } else if (mode == 4) {
        // relief plate: flat parts carry the picture (planar projection), the relief reflects it like chrome
        vec2 puv = vec2(vObj.x * param + 0.5, 0.5 - vObj.y * param * 1.33);
        vec3 pic = texture(env, puv).rgb;
        vec2 euv = n.xy * 0.5 + 0.5;
        euv.y = 1.0 - euv.y;
        vec3 chrome = texture(env, euv).rgb * 1.2 + specular * pow(max(dot(n, normalize(v + vec3(-0.3, 0.6, 0.5))), 0.0), 40.0);
        float relief = smoothstep(0.004, 0.03, abs(vObj.z));
        col = mix(pic, chrome, relief) * tint;
    } else if (mode == 3) {
        float l = max(dot(n, normalize(vec3(0.4, 0.6, 0.7))), 0.0);
        col = tint * (0.2 + 0.8 * l);
    } else {
        // classic 90s sphere mapping from the view-space normal, plus a real highlight
        vec2 euv = n.xy * 0.5 + 0.5;
        euv.y = 1.0 - euv.y;
        col = texture(env, euv).rgb * tint;
        vec3 h = normalize(v + normalize(vec3(-0.4, 0.7, 0.6)));
        col += specular * pow(max(dot(n, h), 0.0), 60.0);
    }
    col *= exposure;
    float fog = clamp((depth - fogRange.x) / max(fogRange.y - fogRange.x, 1e-4), 0.0, 1.0);
    col = mix(col, fogColor, fog);
    o = vec4(col, alpha);
}
)";

const char* kRadialFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D src;
uniform vec2 center;     // uv space (y up)
uniform float strength;  // fraction of the distance to the centre covered by the blur
uniform float gain;
uniform float sharpMix;
void main() {
    vec2 d = uv - center;
    vec3 acc = vec3(0.0);
    float wsum = 0.0;
    float jitter = hash12(gl_FragCoord.xy) / 64.0;
    for (int i = 0; i < 64; i++) {
        float f = (float(i) / 64.0 + jitter);
        float w = exp(-2.2 * f);
        acc += texture(src, center + d * (1.0 - strength * f)).rgb * w;
        wsum += w;
    }
    vec3 blur = acc / wsum;
    vec3 sharp = texture(src, uv).rgb;
    vec3 c = mix(blur * gain, sharp + blur * (gain - 1.0), sharpMix);
    // soft shoulder so the streaks bloom toward white like the original's palette saturation
    c = c / (1.0 + 0.35 * max(c - 0.75, 0.0));
    o = vec4(c, 1.0);
}
)";

const char* kWarpFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D tex;
uniform int mode;
uniform vec2 center;
uniform float time, zoom, angle, strength, bright, fog, lines, seed, alpha, aspect, crisp, wave, contrast, shapeP;
uniform int invert;
uniform vec3 tint;
uniform vec4 rect;   // destination rect in frame units, used for the local aspect
float lineMask(vec2 p) {
    float m = 0.0;
    // straight rays through jittered points near the centre
    for (int k = 0; k < 14; k++) {
        float fk = float(k) + seed * 17.0;
        float a = hash12(vec2(fk, 1.7)) * PI;
        vec2 c0 = (vec2(hash12(vec2(fk, 3.1)), hash12(vec2(fk, 5.3))) - 0.5) * 0.25;
        vec2 dir = vec2(cos(a), sin(a));
        float d = abs(dot(p - c0, vec2(-dir.y, dir.x)));
        float w = 0.0015 + 0.003 * hash12(vec2(fk, 9.9));
        m += smoothstep(w * 2.0, 0.0, d) * (0.4 + 0.6 * hash12(vec2(fk, 7.7)));
    }
    // concentric web rings
    float r = length(p);
    float ring = abs(fract(r * 9.0 + seed) - 0.5);
    m += smoothstep(0.03, 0.0, ring) * 0.5 * smoothstep(0.9, 0.1, r);
    return clamp(m, 0.0, 1.5);
}
vec2 seamGrad(vec2 t, out vec2 dx, out vec2 dy) {
    // derivatives that ignore the atan() wrap
    vec2 t2 = vec2(fract(t.x + 0.5), t.y);
    vec2 dx1 = dFdx(t), dy1 = dFdy(t), dx2 = dFdx(t2), dy2 = dFdy(t2);
    dx = vec2(abs(dx1.x) < abs(dx2.x) ? dx1.x : dx2.x, dx1.y);
    dy = vec2(abs(dy1.x) < abs(dy2.x) ? dy1.x : dy2.x, dy1.y);
    return t;
}
void main() {
    float la = aspect * rect.z / rect.w;
    vec2 p = (uv - center) * vec2(la, 1.0);
    p = rot2(angle) * p;
    if (wave > 0.0) p += wave * 0.05 * vec2(sin(p.y * 7.0 + time * 3.1), cos(p.x * 6.0 + time * 2.3));
    float r = (shapeP == 2.0 ? length(p) : pow(pow(abs(p.x), shapeP) + pow(abs(p.y), shapeP), 1.0 / shapeP)) + 1e-5;
    float a = atan(p.y, p.x);
    vec2 t;
    float shade = 1.0;
    if (mode == 0) {            // tunnel
        t = vec2(a / (2.0 * PI) * zoom, strength * 0.25 / r + time);
        shade = mix(1.0, smoothstep(0.0, 0.35, r), fog);
    } else if (mode == 1) {     // stripes bending into a funnel
        float bend = strength * 0.06 / (r + 0.08);
        t = vec2((p.y + bend) * zoom + time, p.x * 0.05);
        shade = mix(1.0, smoothstep(0.015, 0.09, r), fog);
    } else if (mode == 2) {     // rotozoom plane
        t = p * zoom + vec2(time, time * 0.37);
    } else if (mode == 3) {     // twirl
        float aa = a + strength / (r + 0.15);
        t = vec2(aa / (2.0 * PI), r * zoom + time);
    } else if (mode == 6) {     // Julia set "flowers": stripes along the external angle / potential
        vec2 z = p * zoom;
        vec2 k = vec2(0.32 + 0.03 * sin(time * 0.37), 0.6 + 0.03 * cos(time * 0.29));  // outside M: a dust Julia set
        float it = 0.0;
        for (int i = 0; i < 24; i++) {
            z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + k;
            if (dot(z, z) > 64.0) break;
            it += 1.0;
        }
        t = vec2(atan(z.y, z.x) / (2.0 * PI) * strength + time * 0.05, log(log(dot(z, z) + 1.0) + 1.0) * 0.18 + it * 0.012);
        shade = it >= 23.0 ? 0.55 : 1.0;
    } else if (mode == 5) {     // concentric rings (stripes along the radius)
        t = vec2(r * zoom + time, a / (2.0 * PI));
        shade = mix(1.0, smoothstep(0.0, 0.08, r), fog);
    } else {                    // kaleidoscope
        float n = max(strength, 2.0);
        float seg = 2.0 * PI / n;
        float aa = abs(mod(a, seg) - seg * 0.5);
        t = vec2(cos(aa), sin(aa)) * r * zoom + vec2(time, 0.0);
    }
    vec2 dx, dy;
    seamGrad(t, dx, dy);
    vec3 c = textureGrad(tex, t, dx, dy).rgb;
    if (crisp > 0.0) {
        float l = luma(c);
        float fw = max(fwidth(l), 1e-3) * crisp;
        c = vec3(smoothstep(0.5 - fw, 0.5 + fw, l));
    }
    if (invert != 0) c = 1.0 - c;
    if (contrast != 1.0) c = pow(max(c, 0.0), vec3(contrast));
    c *= tint * bright * shade;
    if (lines > 0.0) c += vec3(lines * lineMask(p));
    o = vec4(c, alpha);
}
)";

const char* kWarpVS = R"(
uniform vec4 rect;
out vec2 uv;
void main() {
    vec2 c = vec2(gl_VertexID & 1, (gl_VertexID >> 1) & 1);
    uv = vec2(c.x, 1.0 - c.y);
    vec2 p = rect.xy + c * rect.zw;
    gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
)";

const char* kWorldFS = R"(
in vec2 uv;
out vec4 o;
uniform vec3 eye;
uniform mat3 camRot;
uniform float tanHalf, aspect;
uniform float floorY, ceilY, texScale, fogDist, bright, time, glint;
uniform vec3 tint, fogColor;
uniform sampler2D hexTex, goldTex, plasmaTex;
uniform int nBalls, ballStyle, floorOn;
uniform vec4 balls[8];
uniform vec4 sphere;   // xyz, r
uniform float sphereGlow;

float smin(float a, float b, float k) { float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0); return mix(b, a, h) - k * h * (1.0 - h); }
float blobs(vec3 p) {
    float d = 1e9;
    for (int i = 0; i < 8; i++) {
        if (i >= nBalls) break;
        d = smin(d, length(p - balls[i].xyz) - balls[i].w, 0.45);
    }
    return d;
}
vec3 blobNormal(vec3 p) {
    vec2 e = vec2(0.002, 0.0);
    return normalize(vec3(blobs(p + e.xyy) - blobs(p - e.xyy), blobs(p + e.yxy) - blobs(p - e.yxy), blobs(p + e.yyx) - blobs(p - e.yyx)));
}
vec2 sphereMap(vec3 n) { vec3 vn = transpose(camRot) * n; return vec2(vn.x * 0.5 + 0.5, 0.5 - vn.y * 0.5); }

vec3 shadePlane(vec3 ro, vec3 rd, float t, float side) {
    vec3 hit = ro + rd * t;
    vec2 tuv = hit.xz * texScale;
    vec3 c = texture(hexTex, tuv).rgb;
    // bump from the texture's luminance, plus glints toward a light ahead of the camera
    vec2 e = vec2(1.0 / 256.0, 0.0);
    float h0 = luma(c), hx = luma(texture(hexTex, tuv + e.xy).rgb), hy = luma(texture(hexTex, tuv + e.yx).rgb);
    vec3 n = normalize(vec3((h0 - hx) * 6.0, side, (h0 - hy) * 6.0));
    float spec = pow(max(dot(reflect(rd, n), normalize(camRot * vec3(0.0, 0.0, -1.0) + vec3(0.0, 0.4 * side, 0.0))), 0.0), 24.0);
    return c * tint * (0.85 + 0.3 * h0) + glint * spec * vec3(0.85, 0.8, 1.0);
}

void main() {
    vec2 p = (uv * 2.0 - 1.0) * vec2(aspect, 1.0) * tanHalf;
    vec3 rd = normalize(camRot * vec3(p, -1.0));
    vec3 col = fogColor;
    float tHit = 1e9;
    if (floorOn != 0) {
        if (rd.y < -1e-4) { tHit = (floorY - eye.y) / rd.y; col = shadePlane(eye, rd, tHit, 1.0); }
        else if (rd.y > 1e-4) { tHit = (ceilY - eye.y) / rd.y; col = shadePlane(eye, rd, tHit, -1.0); }
        col = mix(col, fogColor, 1.0 - exp(-tHit / fogDist));
    }
    // metaballs
    if (nBalls > 0) {
        float t = 0.05;
        for (int i = 0; i < 96; i++) {
            float d = blobs(eye + rd * t);
            if (d < 0.001 || t > min(tHit, 60.0)) break;
            t += d * 0.9;
        }
        if (t < tHit && t < 60.0) {
            vec3 hp = eye + rd * t;
            vec3 n = blobNormal(hp);
            vec3 bc;
            if (ballStyle == 1) {
                // wireframe look: a triangle grid in object space
                vec3 g = abs(fract(hp * 5.0) - 0.5);
                float line = smoothstep(0.06, 0.0, min(min(g.x, g.y), g.z));
                float rim = pow(1.0 - abs(dot(n, -rd)), 2.0);
                bc = vec3(0.08, 0.1, 0.2) + vec3(0.9, 0.95, 1.0) * line + rim * 0.3;
            } else {
                bc = texture(goldTex, sphereMap(n)).rgb * 1.15;
                vec3 h = normalize(-rd + normalize(vec3(-0.3, 0.8, 0.4)));
                bc += pow(max(dot(n, h), 0.0), 80.0) * vec3(1.0, 0.95, 0.7);
            }
            col = mix(bc, fogColor, 1.0 - exp(-t / (fogDist * 1.5)));
            tHit = t;
        }
    }
    // plasma sphere
    if (sphere.w > 0.0) {
        vec3 oc = eye - sphere.xyz;
        float b = dot(oc, rd), cc = dot(oc, oc) - sphere.w * sphere.w;
        float h = b * b - cc;
        float closest = length(cross(sphere.xyz - eye, rd));
        if (h > 0.0) {
            float t = -b - sqrt(h);
            if (t > 0.0 && t < tHit) {
                vec3 hp = eye + rd * t;
                vec3 n = normalize(hp - sphere.xyz);
                vec2 su = vec2(atan(n.z, n.x) / (2.0 * PI), acos(clamp(n.y, -1.0, 1.0)) / PI);
                vec3 pl = texture(plasmaTex, su * vec2(2.0, 1.0) + vec2(time * 0.07, time * 0.03)).rgb;
                vec3 pl2 = texture(plasmaTex, su * vec2(-3.0, 1.5) + vec2(-time * 0.05, time * 0.06)).rgb;
                float fres = pow(1.0 - abs(dot(n, -rd)), 2.5);
                vec3 pm = mix(pl, pl2, 0.5);
                float lp = luma(pm);
                vec3 sc = mix(vec3(0.25, 0.3, 0.75), vec3(1.0, 0.95, 1.0), smoothstep(0.25, 0.7, lp)) * (0.6 + lp) + vec3(1.0, 0.3, 0.8) * fres * 1.4;
                col = sc * sphereGlow;
                tHit = t;
            }
        }
        // halo
        float halo = exp(-max(closest - sphere.w, 0.0) / (sphere.w * 0.35));
        col += vec3(1.0, 0.5, 0.95) * halo * 0.6 * sphereGlow;
    }
    o = vec4(col * bright, 1.0);
}
)";

const char* kQuad3DVS = R"(
uniform mat4 mvp;
uniform int flipY;
out vec2 uv;
void main() {
    vec2 c = vec2(gl_VertexID & 1, (gl_VertexID >> 1) & 1);
    uv = vec2(c.x, flipY != 0 ? c.y : 1.0 - c.y);
    gl_Position = mvp * vec4(c * 2.0 - 1.0, 0.0, 1.0);
}
)";
const char* kQuad3DFS = R"(
in vec2 uv;
out vec4 o;
uniform sampler2D tex;
uniform vec4 tint;
uniform int flipY;
uniform int bicubic;
void main() {
    vec4 c = bicubic != 0 ? texBicubic(tex, uv) : texture(tex, uv);
    o = flipY != 0 ? vec4(c.rgb * tint.rgb * tint.a, c.a * tint.a) : vec4(c.rgb * tint.rgb, tint.a);
}
)";

struct FxShaders {
    Shader mesh, radial, warp, world, quad3d;
    bool built = false;
    void build() {
        if (built) return;
        mesh.build(kMeshVS, kMeshFS, "mesh");
        radial.build(kFullscreenVS, kRadialFS, "radial");
        warp.build(kWarpVS, kWarpFS, "warp");
        world.build(kFullscreenVS, kWorldFS, "world");
        quad3d.build(kQuad3DVS, kQuad3DFS, "quad3d");
        built = true;
    }
};
FxShaders& fx() {
    static FxShaders s;
    s.build();
    return s;
}
}  // namespace

void drawMesh(Ctx& c, const std::string& name, const MeshDraw& d) {
    GpuMesh& m = c.mesh(name);
    Shader& s = fx().mesh;
    s.use();
    s.set("model", d.model);
    s.set("view", d.view);
    s.set("proj", d.proj);
    s.set("tint", d.tint);
    s.set("exposure", d.exposure);
    s.set("specular", d.specular);
    s.set("alpha", d.alpha);
    s.set("param", d.param);
    s.set("mode", d.mode);
    s.set("spikeScale", d.spikeScale);
    s.set("twist", d.twistZ, d.twistY, d.wobblePhase, d.wobble);
    s.set("fogColor", d.fogColor);
    s.set("fogRange", d.fogNear, d.fogFar);
    if (d.env) d.env->bind(0);
    s.set("env", 0);
    gl::Enable(GL_DEPTH_TEST);
    gl::DepthFunc(GL_LEQUAL);
    if (d.cull) { gl::Enable(GL_CULL_FACE); gl::CullFace(GL_BACK); }
    else gl::Disable(GL_CULL_FACE);
    if (d.mode == 2) gl::PolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    m.draw();
    if (d.mode == 2) gl::PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    gl::Disable(GL_CULL_FACE);
    gl::Disable(GL_DEPTH_TEST);
}

void radialBlur(Ctx&, const Target& src, float cx, float cy, float strength, float gain, float sharpMix) {
    Shader& s = fx().radial;
    s.use();
    src.bindTex(0);
    s.set("src", 0);
    s.set("center", cx, 1.0f - cy);
    s.set("strength", strength);
    s.set("gain", gain);
    s.set("sharpMix", sharpMix);
    drawFullscreen();
}

void drawWarp(Ctx& c, const Texture& t, const WarpParams& p, float x, float y, float w, float h) {
    Shader& s = fx().warp;
    s.use();
    t.bind(0);
    t.setRepeat(true);
    s.set("tex", 0);
    s.set("rect", x, y, w, h);
    s.set("mode", p.mode);
    s.set("center", p.cx, 1.0f - p.cy);
    s.set("time", p.time);
    s.set("zoom", p.zoom);
    s.set("angle", p.angle);
    s.set("strength", p.strength);
    s.set("bright", p.bright);
    s.set("fog", p.fog);
    s.set("lines", p.lines);
    s.set("seed", p.seed);
    s.set("alpha", p.alpha);
    s.set("aspect", c.aspect);
    s.set("tint", p.tint);
    s.set("crisp", p.crisp);
    s.set("contrast", p.contrast);
    s.set("shapeP", p.shapeP);
    s.set("wave", p.wave);
    s.set("invert", p.invert ? 1 : 0);
    drawQuad();
}

void copyTarget(Ctx& c, const Target& src, float alpha) { c.drawTarget(src, alpha); }

Mat4 worldView(const WorldParams& w) { return Mat4::lookAt(w.eye, w.target, w.up); }
Mat4 worldProj(Ctx& c, const WorldParams& w) { return Mat4::perspective(w.fovy, c.aspect, 0.05f, 200.0f); }

void drawWorld(Ctx& c, const WorldParams& w) {
    Shader& s = fx().world;
    s.use();
    Vec3 f = (w.target - w.eye).norm();
    Vec3 r = f.cross(w.up).norm();
    Vec3 u = r.cross(f);
    float m[9] = {r.x, r.y, r.z, u.x, u.y, u.z, -f.x, -f.y, -f.z};
    gl::UniformMatrix3fv(s.loc("camRot"), 1, GL_FALSE, m);
    s.set("eye", w.eye);
    s.set("tanHalf", std::tan(w.fovy * 0.5f));
    s.set("aspect", c.aspect);
    s.set("floorY", w.floorY);
    s.set("ceilY", w.ceilY);
    s.set("texScale", w.texScale);
    s.set("fogDist", w.fogDist);
    s.set("bright", w.bright);
    s.set("time", w.time);
    s.set("glint", w.glintAmount);
    s.set("tint", w.tint);
    s.set("fogColor", w.fogColor);
    s.set("floorOn", w.floorOn ? 1 : 0);
    (w.floorTex ? *w.floorTex : c.tex("tex3d#1")).bind(0);
    s.set("hexTex", 0);
    c.tex("tex3d#0").bind(1);
    s.set("goldTex", 1);
    c.tex("tunnel#0").bind(2);
    s.set("plasmaTex", 2);
    s.set("nBalls", w.balls);
    s.set("ballStyle", w.ballStyle);
    float b[32];
    for (int i = 0; i < 8; i++) { b[i * 4] = w.ball[i].x; b[i * 4 + 1] = w.ball[i].y; b[i * 4 + 2] = w.ball[i].z; b[i * 4 + 3] = w.ballR[i]; }
    gl::Uniform4fv(s.loc("balls"), 8, b);
    s.set("sphere", w.sphere.x, w.sphere.y, w.sphere.z, w.sphereR);
    s.set("sphereGlow", w.sphereGlow);
    drawFullscreen();
}

void drawQuad3D(Ctx&, const Texture& t, const Mat4& mvp, float alpha, Vec3 tint, bool bicubic, bool fromTarget) {
    Shader& s = fx().quad3d;
    s.use();
    t.bind(0);
    s.set("tex", 0);
    s.set("mvp", mvp);
    s.set("tint", tint.x, tint.y, tint.z, alpha);
    s.set("bicubic", bicubic ? 1 : 0);
    s.set("flipY", fromTarget ? 1 : 0);
    gl::Enable(GL_DEPTH_TEST);
    gl::DepthFunc(GL_LEQUAL);
    drawQuad();
    gl::Disable(GL_DEPTH_TEST);
}

Texture asTexture(const Target& t) {
    Texture x;
    x.id = t.tex;
    x.w = t.w;
    x.h = t.h;
    return x;
}

namespace {
// Start time of every order of GARLIC.PAK (libopenmpt), plus the end of the song.
const float kOrderStart[] = {0.0f,    1.702f,   8.510f,   15.318f,  22.126f,  28.934f,  35.742f,  42.550f,
                             49.358f, 56.166f,  62.974f,  69.782f,  76.590f,  83.398f,  90.206f,  97.014f,
                             103.822f, 110.630f, 117.342f, 124.054f, 130.766f, 137.478f, 144.190f, 150.902f,
                             157.614f, 164.326f, 171.038f, 177.750f, 184.462f, 191.174f, 197.886f, 204.598f,
                             211.310f, 218.022f, 224.734f, 231.446f, 238.158f, 244.870f};
constexpr int kOrders = int(sizeof(kOrderStart) / sizeof(float)) - 1;
}  // namespace

float captureLead(float m) {
    static const float k[][2] = {{0.0f, -0.56f}, {36.0f, -0.37f}, {45.0f, -0.325f}, {70.0f, -0.196f},
                                 {88.0f, -0.10f}, {102.0f, -0.055f}, {110.0f, 0.0f}, {116.0f, 0.03f},
                                 {126.0f, 0.01f}, {148.0f, 0.035f}, {175.0f, 0.0f}, {300.0f, 0.0f}};
    const int n = int(sizeof(k) / sizeof(k[0]));
    if (m <= k[0][0]) return k[0][1];
    for (int i = 0; i + 1 < n; i++)
        if (m < k[i + 1][0]) return k[i][1] + (k[i + 1][1] - k[i][1]) * (m - k[i][0]) / (k[i + 1][0] - k[i][0]);
    return k[n - 1][1];
}

int musicPos(float T) {
    int o = 0;
    while (o + 1 < kOrders && T >= kOrderStart[o + 1]) o++;
    int rows = o == 0 ? 16 : 64;
    float rowLen = (kOrderStart[o + 1] - kOrderStart[o]) / rows;
    int r = std::clamp(int((T - kOrderStart[o]) / rowLen), 0, rows - 1);
    return 2 * (o * 64 + r) - 2;
}

float musicTimeOfPos(int pos) {
    for (int o = 0; o < kOrders; o++) {
        int rows = o == 0 ? 16 : 64;
        float rowLen = (kOrderStart[o + 1] - kOrderStart[o]) / rows;
        for (int r = 0; r < rows; r++)
            if (2 * (o * 64 + r) - 2 >= pos) return kOrderStart[o] + r * rowLen;
    }
    return kOrderStart[kOrders];
}

float beatPulse(float t, float period, float sharp) {
    float ph = std::fmod(std::max(t, 0.0f), period) / period;
    return std::exp(-ph * sharp);
}

float easeInOut(float x) {
    x = clamp01(x);
    return x * x * (3 - 2 * x);
}
