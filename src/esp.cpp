#include "esp.h"
#include "modules.h"
#include "jni_util.h"
#include "mc_mappings.h"
#include "hook_manager.h"

#include <cmath>
#include <vector>

using namespace jni;

namespace {

    struct Vec3f { float x, y, z; };

    // Build a view matrix from camera position + yaw/pitch (degrees).
    // MC convention: yaw 0 = +Z (south), positive yaw = clockwise viewed from above.
    void viewMatrix(const Vec3f& eye, float yawDeg, float pitchDeg, float out[16]) {
        const float yaw = yawDeg * 3.14159265f / 180.f;
        const float pitch = pitchDeg * 3.14159265f / 180.f;

        const float cy = std::cos(yaw), sy = std::sin(yaw);
        const float cp = std::cos(pitch), sp = std::sin(pitch);

        // Forward vector (right-handed, -Z forward in view space)
        const float fx = -sy * cp;
        const float fy = -sp;
        const float fz = cy * cp;

        // Right = normalize(cross(forward, worldUp))
        const float rx = fz;
        const float ry = 0.f;
        const float rz = -fx;
        const float rl = std::sqrt(rx * rx + rz * rz) + 1e-6f;

        // Up = cross(right, forward)
        const float ux = ry * fz - rz * fy;
        const float uy = rz * fx - rx * fz;
        const float uz = rx * fy - ry * fx;

        out[0] = rx / rl; out[4] = ry; out[8]  = ux; out[12] = -(rx / rl) * eye.x - ry * eye.y - ux * eye.z;
        out[1] = 0;       out[5] = 1.f; out[9]  = uy; out[13] = -eye.y;
        out[2] = rz / rl; out[6] = 0; out[10] = uz; out[14] = -(rz / rl) * eye.x - uz * eye.z;
        out[3] = 0;       out[7] = 0; out[11] = 0;  out[15] = 1.f;
        // Note: this row-major layout is chosen for our manual transform below.
    }

    void projMatrix(float fovDeg, float aspect, float zn, float zf, float out[16]) {
        const float f = 1.f / std::tan(fovDeg * 3.14159265f / 360.f);
        out[0] = f / aspect; out[1] = 0; out[2] = 0; out[3] = 0;
        out[4] = 0; out[5] = f; out[6] = 0; out[7] = 0;
        out[8] = 0; out[9] = 0; out[10] = (zf + zn) / (zn - zf); out[11] = -1.f;
        out[12] = 0; out[13] = 0; out[14] = (2.f * zf * zn) / (zn - zf); out[15] = 0;
    }

    bool projectPoint(const float view[16], const float proj[16],
                      float x, float y, float z,
                      float w, float h, Vec3f& outNdc, float& outW) {
        // view * [x y z 1]
        const float vx = view[0]*x + view[4]*y + view[8]*z + view[12];
        const float vy = view[1]*x + view[5]*y + view[9]*z + view[13];
        const float vz = view[2]*x + view[6]*y + view[10]*z + view[14];
        const float vw = 1.f;

        // proj * v
        const float cx = proj[0]*vx + proj[8]*vz;
        const float cy = proj[5]*vy + proj[10]*vz;
        const float cw = -(vz);   // -z (right-handed view space)
        (void)vw;

        if (cw <= 0.001f) return false;
        outNdc.x = cx / cw; outNdc.y = cy / cw; outNdc.z = 0.f;
        outW = cw;
        outNdc.x = (outNdc.x * 0.5f + 0.5f) * w;
        outNdc.y = (1.f - (outNdc.y * 0.5f + 0.5f)) * h;   // flip Y for screen
        return true;
    }

} // namespace

void esp::project() {
    g_espScreen.clear();
    if (!g.esp || g_espEntries.empty()) return;

    JniEnv jni = JniEnv::get();
    if (!jni.env) return;

    jobject mcObj = hooks::getMinecraftInstance(jni.env);
    if (!mcObj) return;
    LocalRef mc(jni.env, mcObj);

    auto grOpt = getObjectField(jni.env, *mc, "gameRenderer",
                                "Lnet/minecraft/client/renderer/GameRenderer;");
    if (!grOpt) return;
    LocalRef gr(jni.env, *grOpt);

    auto camOpt = callObject(jni.env, *gr, "mainCamera", mc::M_mainCamera);
    if (!camOpt) return;
    LocalRef cam(jni.env, *camOpt);

    auto posOpt = callObject(jni.env, *cam, "position", mc::M_camPos);
    if (!posOpt) return;
    LocalRef pos(jni.env, *posOpt);
    const auto cx = getDoubleField(jni.env, *pos, "x");
    const auto cy = getDoubleField(jni.env, *pos, "y");
    const auto cz = getDoubleField(jni.env, *pos, "z");
    if (!cx || !cy || !cz) return;

    // Entity we render from (free look safe): getCameraEntity's rotations.
    auto camEnt = callObject(jni.env, *mc, "getCameraEntity", "()Lnet/minecraft/world/entity/Entity;");
    float yaw = 0.f, pitch = 0.f;
    if (camEnt) {
        LocalRef ce(jni.env, *camEnt);
        // Entity.getXRot()/getYRot() take partial tick float; pass 1.0f.
        yaw   = callFloatArg(jni.env, *ce, "getYRot", "(F)F", 1.0f).value_or(0.f);
        pitch = callFloatArg(jni.env, *ce, "getXRot", "(F)F", 1.0f).value_or(0.f);
    }

    auto winOpt = callObject(jni.env, *mc, "getWindow", "()Lcom/mojang/blaze3d/platform/Window;");
    float w = 1.f, h = 1.f;
    if (winOpt) {
        LocalRef win(jni.env, *winOpt);
        w = static_cast<float>(callInt(jni.env, *win, "getWidth").value_or(1));
        h = static_cast<float>(callInt(jni.env, *win, "getHeight").value_or(1));
    }
    if (w < 1.f || h < 1.f) return;

    float view[16], proj[16];
    viewMatrix({ (float)*cx, (float)*cy, (float)*cz }, yaw, pitch, view);
    projMatrix(70.f, w / h, 0.05f, 512.f, proj);

    for (const auto& e : g_espEntries) {
        // Project feet and head.
        Vec3f feet{}, head{};
        float fw, hw;
        const bool okF = projectPoint(view, proj, e.x, e.y, e.z, w, h, feet, fw);
        const bool okH = projectPoint(view, proj, e.x, e.y + e.ey, e.z, w, h, head, hw);
        if (!okF || !okH) continue;

        ProjectedEsp out;
        out.sx = feet.x;
        out.sy = feet.y;
        out.w  = std::max(6.f, std::abs(head.x - feet.x) * 2.2f);
        out.h  = std::abs(feet.y - head.y);
        out.visible = out.h > 4.f;
        out.health = e.health;
        out.kind = e.kind;
        memcpy(out.name, e.name, sizeof(out.name));
        g_espScreen.push_back(out);
    }
}
