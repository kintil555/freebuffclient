#include "ui.h"
#include "modules.h"
#include "hook_manager.h"
#include "esp.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_win32.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <algorithm>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

    // ------------------------------------------------------------------ state
    bool  g_initialized   = false;
    HWND  g_hwnd          = nullptr;
    bool  g_menuOpen      = false;
    bool  g_prevInsert    = false;
    float g_animOpen      = 0.f;   // 0..1 menu open animation
    float g_accent[3]     = { 0.72f, 0.36f, 0.98f };  // violet accent
    int   g_selectedCat   = 2;     // Movement selected by default
    WNDPROC g_origWndProc = nullptr;

    // Toggle key handled via Win32 messages (Insert).
    bool g_toggleQueued = false;

    LRESULT WINAPI wndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        // Right Shift toggles the menu (scancode 0x36 distinguishes it from left Shift).
        if (msg == WM_KEYDOWN && wParam == VK_SHIFT &&
            ((lParam >> 16) & 0xFF) == 0x36) g_toggleQueued = true;
        if (g_initialized && g_menuOpen) {
            if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return 0;
            // Swallow game input while the menu is up.
            switch (msg) {
                case WM_KEYDOWN: case WM_KEYUP:
                case WM_SYSKEYDOWN: case WM_SYSKEYUP:
                case WM_CHAR:
                case WM_LBUTTONDOWN: case WM_LBUTTONUP:
                case WM_RBUTTONDOWN: case WM_RBUTTONUP:
                case WM_MBUTTONDOWN: case WM_MBUTTONUP:
                case WM_MOUSEMOVE: case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
                case WM_INPUT: return 0;
                default: break;
            }
        }
        return CallWindowProc(g_origWndProc, hWnd, msg, wParam, lParam);
    }

    // ------------------------------------------------------------------ theme
    void applyTheme() {
        ImGuiStyle& s = ImGui::GetStyle();
        s = ImGuiStyle{};
        s.WindowRounding    = 14.f;
        s.ChildRounding     = 12.f;
        s.FrameRounding     = 10.f;
        s.PopupRounding     = 12.f;
        s.GrabRounding      = 8.f;
        s.TabRounding       = 10.f;
        s.ScrollbarRounding = 10.f;
        s.WindowBorderSize  = 1.f;
        s.FrameBorderSize   = 0.f;
        s.WindowPadding     = ImVec2(14, 12);
        s.FramePadding      = ImVec2(10, 6);
        s.ItemSpacing       = ImVec2(10, 8);
        s.WindowTitleAlign  = ImVec2(0.5f, 0.5f);

        const ImU32 accent = IM_COL32(184, 92, 255, 255);      // violet
        const ImU32 bg     = IM_COL32(16, 12, 24, 242);        // near-black plum
        const ImU32 bgMid  = IM_COL32(28, 20, 42, 235);
        const ImU32 text   = IM_COL32(235, 228, 255, 255);
        const ImU32 dim    = IM_COL32(150, 138, 180, 255);

        ImVec4* c = s.Colors;
        c[ImGuiCol_WindowBg]         = ImGui::ColorConvertU32ToFloat4(bg);
        c[ImGuiCol_ChildBg]          = ImGui::ColorConvertU32ToFloat4(bgMid);
        c[ImGuiCol_PopupBg]          = ImGui::ColorConvertU32ToFloat4(bg);
        c[ImGuiCol_Text]             = ImGui::ColorConvertU32ToFloat4(text);
        c[ImGuiCol_TextDisabled]     = ImGui::ColorConvertU32ToFloat4(dim);
        c[ImGuiCol_Border]           = ImGui::ColorConvertU32ToFloat4(IM_COL32(184, 92, 255, 40));
        c[ImGuiCol_TitleBg]          = ImGui::ColorConvertU32ToFloat4(bg);
        c[ImGuiCol_TitleBgActive]    = ImGui::ColorConvertU32ToFloat4(bgMid);
        c[ImGuiCol_TitleBgCollapsed] = ImGui::ColorConvertU32ToFloat4(bg);
        c[ImGuiCol_CheckMark]        = ImGui::ColorConvertU32ToFloat4(accent);
        c[ImGuiCol_SliderGrab]       = ImGui::ColorConvertU32ToFloat4(accent);
        c[ImGuiCol_SliderGrabActive] = ImGui::ColorConvertU32ToFloat4(IM_COL32(210, 150, 255, 255));
        c[ImGuiCol_FrameBg]          = ImGui::ColorConvertU32ToFloat4(IM_COL32(38, 28, 56, 220));
        c[ImGuiCol_FrameBgHovered]   = ImGui::ColorConvertU32ToFloat4(IM_COL32(52, 38, 76, 230));
        c[ImGuiCol_FrameBgActive]    = ImGui::ColorConvertU32ToFloat4(IM_COL32(64, 46, 94, 235));
        c[ImGuiCol_Button]           = ImGui::ColorConvertU32ToFloat4(IM_COL32(48, 34, 72, 235));
        c[ImGuiCol_ButtonHovered]    = ImGui::ColorConvertU32ToFloat4(IM_COL32(96, 54, 150, 240));
        c[ImGuiCol_ButtonActive]     = ImGui::ColorConvertU32ToFloat4(IM_COL32(150, 84, 224, 255));
        c[ImGuiCol_ScrollbarBg]      = ImGui::ColorConvertU32ToFloat4(IM_COL32(20, 14, 30, 220));
        c[ImGuiCol_ScrollbarGrab]    = ImGui::ColorConvertU32ToFloat4(IM_COL32(70, 50, 104, 235));
        c[ImGuiCol_ScrollbarGrabHovered] = ImGui::ColorConvertU32ToFloat4(accent);
        c[ImGuiCol_ScrollbarGrabActive]  = ImGui::ColorConvertU32ToFloat4(IM_COL32(210, 150, 255, 255));
        c[ImGuiCol_Header]           = ImGui::ColorConvertU32ToFloat4(IM_COL32(48, 34, 72, 220));
        c[ImGuiCol_HeaderHovered]    = ImGui::ColorConvertU32ToFloat4(IM_COL32(96, 54, 150, 235));
        c[ImGuiCol_HeaderActive]     = ImGui::ColorConvertU32ToFloat4(IM_COL32(150, 84, 224, 255));
        c[ImGuiCol_Separator]        = ImGui::ColorConvertU32ToFloat4(IM_COL32(184, 92, 255, 30));
    }

    // Animated rainbow-violet gradient title bar.
    void drawBanner() {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const float h = 54.f;
        const float t = static_cast<float>(ImGui::GetTime());

        ImU32 c1 = ImGui::GetColorU32(ImVec4(
            0.55f + 0.25f * std::sin(t * 1.1f), 0.25f + 0.15f * std::sin(t * 1.3f + 2.f), 0.85f, 1.f));
        ImU32 c2 = ImGui::GetColorU32(ImVec4(
            0.35f + 0.2f * std::sin(t * 0.9f + 1.f), 0.2f, 0.7f + 0.2f * std::sin(t * 1.4f), 1.f));
        dl->AddRectFilledMultiColor(p, ImVec2(p.x + w, p.y + h), c1, c2, c2, c1);
        dl->AddText(ImVec2(p.x + 16.f, p.y + 16.f), IM_COL32(255, 255, 255, 235), "NOVA");
        dl->AddText(ImVec2(p.x + 74.f, p.y + 19.f), IM_COL32(220, 200, 255, 180), "utility overlay");
        ImGui::Dummy(ImVec2(w, h + 6.f));
    }

    // One toggle row: custom pill instead of ImGui checkbox (rounded + glow).
    void togglePill(const char* label, bool* v, float accent2 = 0.f) {
        ImGui::PushID(label);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float rowW = ImGui::GetContentRegionAvail().x;
        const float rowH = 30.f;

        bool clicked = false;
        ImGui::InvisibleButton("##row", ImVec2(rowW, rowH));
        clicked = ImGui::IsItemClicked();
        if (clicked) *v = !*v;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool hov = ImGui::IsItemHovered();

        // Row background
        ImU32 rowBg = *v ? ImGui::GetColorU32(ImVec4(g_accent[0]*0.25f+0.08f, g_accent[1]*0.2f+0.05f, g_accent[2]*0.3f+0.12f, 0.85f))
                         : ImGui::GetColorU32(ImVec4(0.10f, 0.08f, 0.16f, 0.65f));
        if (hov && !*v) rowBg = ImGui::GetColorU32(ImVec4(0.14f, 0.11f, 0.22f, 0.8f));
        dl->AddRectFilled(p, ImVec2(p.x + rowW, p.y + rowH), rowBg, 10.f);

        // Label
        dl->AddText(ImVec2(p.x + 12.f, p.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f),
                    *v ? IM_COL32(240, 230, 255, 255) : IM_COL32(190, 180, 215, 235), label);

        // Pill
        const float pw = 40.f, ph = 18.f;
        const float px = p.x + rowW - pw - 12.f;
        const float py = p.y + (rowH - ph) * 0.5f;
        const float anim = *v ? 1.f : 0.f;  // instant for simplicity; add easing later

        if (*v) {
            ImU32 ac = ImGui::GetColorU32(ImVec4(g_accent[0], g_accent[1], g_accent[2], 0.95f));
            dl->AddRectFilled(ImVec2(px, py), ImVec2(px + pw, py + ph), ac, 9.f);
        } else {
            dl->AddRectFilled(ImVec2(px, py), ImVec2(px + pw, py + ph), IM_COL32(45, 38, 66, 255), 9.f);
        }
        // Knob with subtle outer glow when on
        const float kx = *v ? px + pw - ph + 3.f : px + 3.f;
        if (*v) {
            ImVec4 acv = ImGui::ColorConvertU32ToFloat4(ImGui::GetColorU32(ImVec4(g_accent[0], g_accent[1], g_accent[2], 0.45f)));
            dl->AddCircleFilled(ImVec2(kx + (ph - 6.f) * 0.5f, py + ph * 0.5f), (ph - 6.f) * 0.5f + 3.f,
                                ImGui::GetColorU32(acv));
        }
        dl->AddCircleFilled(ImVec2(kx + (ph - 6.f) * 0.5f, py + ph * 0.5f), (ph - 6.f) * 0.5f,
                            IM_COL32(245, 242, 255, 255));
        (void)anim; (void)accent2;
        ImGui::PopID();
    }

    void sliderRow(const char* label, float* v, float mn, float mx, const char* fmt = "%.1f") {
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        ImGui::SetNextItemWidth(-1);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.14f, 0.11f, 0.22f, 0.9f));
        ImGui::SliderFloat("##s", v, mn, mx, fmt);
        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    void sliderRow(const char* label, int* v, int mn, int mx) {
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        ImGui::SetNextItemWidth(-1);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.14f, 0.11f, 0.22f, 0.9f));
        ImGui::SliderInt("##s", v, mn, mx);
        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    // ------------------------------------------------------------------ menu
    void drawMenu() {
        ImGuiIO& io = ImGui::GetIO();
        const float W = io.DisplaySize.x, H = io.DisplaySize.y;
        const float mw = 880.f, mh = 560.f;
        ImGui::SetNextWindowPos(ImVec2((W - mw) * 0.5f, (H - mh) * 0.5f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(mw, mh));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_animOpen);

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::Begin("##nova", nullptr, flags);
        drawBanner();
        ImGui::Spacing();

        // ---- category rail -------------------------------------------------
        ImGui::BeginChild("rail", ImVec2(190, -34), ImGuiChildFlags_Borders);
        const char* cats[] = { "Combat", "Player", "Movement", "Visual", "Exploits" };
        for (int i = 0; i < 5; ++i) {
            bool sel = (g_selectedCat == i);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            const float h = 36.f;
            ImGui::InvisibleButton(cats[i], ImVec2(w, h));
            if (ImGui::IsItemClicked()) g_selectedCat = i;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const bool hov = ImGui::IsItemHovered();
            if (sel || hov) {
                ImU32 bgc = sel ? ImGui::GetColorU32(ImVec4(g_accent[0]*0.35f, g_accent[1]*0.3f, g_accent[2]*0.4f, 0.9f))
                                : ImGui::GetColorU32(ImVec4(0.16f, 0.13f, 0.25f, 0.7f));
                dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bgc, 10.f);
                if (sel)
                    dl->AddRectFilled(p, ImVec2(p.x + 3.f, p.y + h),
                                      ImGui::GetColorU32(ImVec4(g_accent[0], g_accent[1], g_accent[2], 1.f)), 1.5f);
            }
            dl->AddText(ImVec2(p.x + 16.f, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                        sel ? IM_COL32(245, 238, 255, 255) : IM_COL32(175, 165, 205, 235), cats[i]);
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // ---- module pane ------------------------------------------------------
        ImGui::BeginChild("pane", ImVec2(0, -34), ImGuiChildFlags_Borders);
        switch (g_selectedCat) {
            case 0: // Combat
                togglePill("Kill Aura", &g.killAura);
                if (g.killAura) {
                    sliderRow("Range", &g.killAuraRange, 3.f, 6.f);
                    sliderRow("CPS cap", &g.killAuraCps, 1, 20);
                }
                togglePill("Auto Clicker", &g.autoClicker);
                if (g.autoClicker) sliderRow("CPS", &g.autoClickerCps, 1, 20);
                togglePill("Velocity", &g.velocity);
                if (g.velocity) {
                    sliderRow("Horizontal %", &g.velocityHPercent, 0.f, 100.f, "%.0f%%");
                    sliderRow("Vertical %", &g.velocityVPercent, 0.f, 100.f, "%.0f%%");
                }
                togglePill("Auto Heal (HUD)", &g.autoHeal);
                if (g.autoHeal) sliderRow("Alert below", &g.autoHealThreshold, 2.f, 19.f);
                togglePill("Lag Range", &g.lagRange);
                if (g.lagRange) sliderRow("Lag ms", &g.lagRangeMs, 10.f, 250.f, "%.0f");
                break;

            case 1: // Player
                togglePill("No Fall", &g.noFall);
                togglePill("Anti Fire (visual)", &g.antiFire);
                togglePill("Fast Place", &g.fastPlace);
                togglePill("Fast Break", &g.fastBreak);
                togglePill("Inventory Move", &g.inventoryMove);
                togglePill("No Server Rotation", &g.noRotate);
                break;

            case 2: // Movement
                togglePill("Flight", &g.flight);
                if (g.flight) sliderRow("Fly speed", &g.flightSpeed, 0.2f, 3.f);
                togglePill("Speed", &g.speed);
                if (g.speed) sliderRow("Multiplier", &g.speedMultiplier, 1.05f, 4.f);
                togglePill("Auto Sprint", &g.sprint);
                togglePill("No Slowdown", &g.noSlowdown);
                togglePill("Step", &g.step);
                if (g.step) sliderRow("Step height", &g.stepHeight, 0.6f, 2.5f);
                togglePill("Long Jump", &g.longJump);
                if (g.longJump) sliderRow("Boost", &g.longJumpBoost, 1.1f, 2.5f);
                togglePill("Scaffold", &g.scaffold);
                break;

            case 3: // Visual
                togglePill("Fullbright", &g.fullbright);
                if (g.fullbright) sliderRow("Gamma", &g.fullbrightGamma, 1.f, 24.f, "%.0f");
                togglePill("ESP", &g.esp);
                if (g.esp) {
                    togglePill("Boxes", &g.espBoxes);
                    togglePill("Names", &g.espNames);
                    sliderRow("Max distance", &g.espMaxDistance, 16.f, 256.f, "%.0f");
                }
                togglePill("FreeCam", &g.freeCam);
                if (g.freeCam) sliderRow("Cam speed", &g.freeCamSpeed, 2.f, 30.f);
                togglePill("HUD", &g.hud);
                togglePill("Zoom (hold Z)", &g.zoom);
                if (g.zoom) sliderRow("Zoom factor", &g.zoomFactor, 2.f, 10.f);
                break;

            case 4: // Exploits
                togglePill("Fast Interact", &g.fastInteract);
                togglePill("More Packets (SP safe)", &g.morePackets);
                togglePill("Player Detect", &g.playerDetect);
                ImGui::Spacing();
                ImGui::SeparatorText("about");
                ImGui::TextWrapped("Exploit modules here are single-player oriented: "
                                   "the integrated server trusts the client, so client-side "
                                   "state edits are reflected without packet tricks.");
                break;
        }
        ImGui::EndChild();

        // ---- footer ----------------------------------------------------------
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.12f, 0.25f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.2f, 0.4f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.3f, 0.5f, 1.f));
        if (ImGui::Button("Eject##nova", ImVec2(120, 0))) {
            requestEject();   // graceful unload handled in dllmain
        }
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        ImGui::TextDisabled("fps %d  |  entities %d  |  hp %.1f", g_rt.fps, g_rt.entityCount, g_rt.health);
        ImGui::End();

        ImGui::PopStyleVar();
    }

    // -------------------------------------------------------------------- HUD
    void drawHud() {
        if (!g.hud) return;
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const float pad = 14.f;
        const float x = pad, y = pad;

        // Glassy chip behind the watermark.
        const char* title = "NOVA  |  26.2";
        const ImVec2 ts = ImGui::CalcTextSize(title);
        dl->AddRectFilled(ImVec2(x - 8.f, y - 6.f), ImVec2(x + ts.x + 22.f, y + ts.y + 12.f),
                          IM_COL32(14, 10, 22, 205), 12.f);
        dl->AddRect(ImVec2(x - 8.f, y - 6.f), ImVec2(x + ts.x + 22.f, y + ts.y + 12.f),
                    IM_COL32(184, 92, 255, 60), 12.f);
        dl->AddText(ImVec2(x, y), IM_COL32(235, 225, 255, 240), title);

        // Accent dot pulse.
        const float t = static_cast<float>(ImGui::GetTime());
        const float pulse = 0.5f + 0.5f * std::sin(t * 2.2f);
        dl->AddCircleFilled(ImVec2(x + ts.x + 13.f, y + ts.y * 0.5f + 1.f), 3.2f,
                            IM_COL32(184, 92, 255, (ImU8)(120 + 120 * pulse)));

        // Active module list (right side, subtle) — driven by activeModules().
        const float dispW = ImGui::GetIO().DisplaySize.x;
        float yy = y + 8.f;
        for (const auto& mod : activeModules()) {
            if (!*mod.value) continue;
            const ImVec2 s2 = ImGui::CalcTextSize(mod.name);
            dl->AddRectFilled(ImVec2(dispW - s2.x - 26.f, yy - 4.f), ImVec2(dispW - 10.f, yy + s2.y + 8.f),
                              IM_COL32(14, 10, 22, 190), 9.f);
            dl->AddText(ImVec2(dispW - s2.x - 18.f, yy), IM_COL32(210, 190, 250, 235), mod.name);
            yy += s2.y + 14.f;
        }

        // Watermark bottom-left: fps/entities
        char buf[64];
        snprintf(buf, sizeof(buf), "%d fps  ·  %d entities", g_rt.fps, g_rt.entityCount);
        const ImVec2 bs = ImGui::CalcTextSize(buf);
        const float H = ImGui::GetIO().DisplaySize.y;
        dl->AddRectFilled(ImVec2(x - 8.f, H - bs.y - 18.f), ImVec2(x + bs.x + 20.f, H - 8.f),
                          IM_COL32(14, 10, 22, 190), 10.f);
        dl->AddText(ImVec2(x, H - bs.y - 12.f), IM_COL32(200, 190, 230, 220), buf);
    }

    // ------------------------------------------------------------------- ESP
    void drawEsp() {
        if (!g.esp) return;
        // World->screen projection uses the MC camera matrices — done via the
        // GameRenderer/Quaternionf path in esp.cpp; here we just draw whatever
        // was projected this frame (g_espScreen filled by esp::project()).
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        for (const auto& e : g_espScreen) {
            if (!e.visible) continue;
            const ImU32 col = e.kind == 0 ? IM_COL32(184, 92, 255, 235) : IM_COL32(255, 120, 160, 220);
            dl->AddRect(ImVec2(e.sx - e.w * 0.5f, e.sy - e.h), ImVec2(e.sx + e.w * 0.5f, e.sy),
                        col, 3.f, 0, 1.6f);
            if (g.espNames && e.name[0]) {
                const ImVec2 ts = ImGui::CalcTextSize(e.name);
                dl->AddText(ImVec2(e.sx - ts.x * 0.5f, e.sy - e.h - ts.y - 4.f), col, e.name);
            }
            if (e.kind == 0 && e.health > 0.f) {
                const float hfrac = std::clamp(e.health / 20.f, 0.f, 1.f);
                dl->AddRectFilled(ImVec2(e.sx - e.w * 0.5f - 4.f, e.sy - e.h),
                                  ImVec2(e.sx - e.w * 0.5f - 1.5f, e.sy), IM_COL32(0, 0, 0, 140), 2.f);
                dl->AddRectFilled(ImVec2(e.sx - e.w * 0.5f - 4.f, e.sy - e.h * hfrac),
                                  ImVec2(e.sx - e.w * 0.5f - 1.5f, e.sy),
                                  IM_COL32(120, 255, 170, 230), 2.f);
            }
        }
    }

    bool initImGui(HWND hwnd, HDC hdc) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        applyTheme();

        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplOpenGL3_Init("#version 150");

        g_hwnd = hwnd;
        g_origWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(wndProc)));
        return true;
    }

} // namespace

// Eject flag from menu; dllmain polls it.
bool g_ejectRequested = false;
void requestEject() { g_ejectRequested = true; }

namespace ui {

    void onJvmReady(JNIEnv* env) {
        (void)env;
        // Window discovery: GLFW window handle is fetched lazily in renderFrame.
    }

    void renderFrame(HDC hdc) {
        if (g_ejectRequested) return;   // dllmain tears down

        HWND hwnd = WindowFromDC(hdc);
        if (!hwnd) return;

        if (!g_initialized) {
            if (!initImGui(hwnd, hdc)) return;
            g_initialized = true;
        }

        // Window may have been recreated (fullscreen toggle) — re-hook wndproc.
        if (hwnd != g_hwnd) {
            g_hwnd = hwnd;
            g_origWndProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(wndProc)));
        }

        ImGuiIO& io = ImGui::GetIO();

        // ---- menu toggle (Right Shift) ----
        const bool rsNow = (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
        if (rsNow && !g_prevInsert) g_menuOpen = !g_menuOpen;
        g_prevInsert = rsNow;
        if (g_toggleQueued) { g_menuOpen = !g_menuOpen; g_toggleQueued = false; }

        // ---- open/close animation (smoothstep) ----
        const float target = g_menuOpen ? 1.f : 0.f;
        g_animOpen += (target - g_animOpen) * std::clamp(io.DeltaTime * 14.f, 0.f, 1.f);
        if (g_animOpen < 0.001f) g_animOpen = 0.f;

        // Input while menu open flows through the hooked WndProc
        // (ImGui_ImplWin32_WndProcHandler); the game's cursor is released there.
        io.MouseDrawCursor = g_menuOpen;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        esp::project();   // world -> screen for the ESP cache

        drawEsp();
        drawHud();
        if (g_menuOpen && g_animOpen > 0.02f) drawMenu();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void shutdown() {
        if (!g_initialized) return;
        if (g_hwnd && g_origWndProc) {
            SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_origWndProc));
        }
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_initialized = false;
    }
}
