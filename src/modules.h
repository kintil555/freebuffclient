#pragma once
// Central feature state + tick loop. The UI mutates `g`, the frame logic in
// modules.cpp applies it to the live game objects every rendered frame.

#include <string>
#include <array>
#include <vector>

// Called once per rendered frame with the live Minecraft instance (JNI side).
// All module logic is frame-driven (frame rate >= tick rate in practice).
void onClientTick(void* envPtr, void* mcObj);
// FPS counter, called after each rendered frame.
void onFrame();

// Friendly names for the module list UI + HUD.
struct ModuleEntry { const char* name; bool* value; };
inline std::vector<ModuleEntry> activeModules();

enum class Category : int { Combat, Player, Movement, Visual, Exploits, COUNT };

inline const char* categoryName(Category c) {
    switch (c) {
        case Category::Combat:   return "Combat";
        case Category::Player:   return "Player";
        case Category::Movement: return "Movement";
        case Category::Visual:   return "Visual";
        case Category::Exploits: return "Exploits";
        default: return "?";
    }
}

struct Settings {
    // ---- Combat ----
    bool  killAura        = false;
    float killAuraRange   = 4.2f;
    int   killAuraCps     = 12;
    bool  autoClicker     = false;
    int   autoClickerCps  = 14;
    bool  velocity        = false;
    float velocityHPercent= 30.f;
    float velocityVPercent= 60.f;
    bool  autoHeal        = false;
    float autoHealThreshold = 12.f;
    bool  lagRange        = false;
    float lagRangeMs      = 60.f;

    // ---- Player ----
    bool  noFall          = false;
    bool  antiFire        = false;
    bool  fastPlace       = false;
    bool  fastBreak       = false;
    bool  inventoryMove   = false;
    bool  noRotate        = false;

    // ---- Movement ----
    bool  flight          = false;
    float flightSpeed     = 0.9f;
    bool  flyVertical     = true;
    bool  speed           = false;
    float speedMultiplier = 1.6f;
    bool  sprint          = true;
    bool  noSlowdown      = false;
    bool  step            = false;
    float stepHeight      = 1.0f;
    bool  longJump        = false;
    float longJumpBoost   = 1.35f;
    bool  scaffold        = false;

    // ---- Visual ----
    bool  fullbright      = false;
    float fullbrightGamma = 12.0f;
    bool  esp             = false;
    bool  espBoxes        = true;
    bool  espTracers      = false;
    bool  espNames        = false;
    float espMaxDistance  = 128.f;
    bool  freeCam         = false;
    float freeCamSpeed    = 8.f;
    bool  hud             = true;
    bool  blurMenus       = false;
    bool  zoom            = false;
    float zoomFactor      = 4.0f;

    // ---- Exploits (single-player-safe) ----
    bool  morePackets     = false;
    bool  fastInteract    = false;
    bool  creativeTabs    = false;
    bool  disjointJoint   = false;
    bool  playerDetect    = false;

    // Runtime entity cache (world-space), rebuilt each frame by the JNI pass.
    struct EspEntry { float x, y, z; float ex, ey, ez; float health; int kind; char name[32]; };
};

struct RuntimeStats {
    int   fps         = 0;
    int   ping        = 0;
    int   entityCount = 0;
    float health      = 0.f;
    bool  inGame      = false;
    bool  hasEscMenu  = false;
    std::string serverInfo = "Singleplayer";
};

inline Settings g;
inline RuntimeStats g_rt;
inline std::vector<Settings::EspEntry> g_espEntries;

struct ProjectedEsp { float sx, sy; float w, h; bool visible; float health; int kind; char name[32]; };
inline std::vector<ProjectedEsp> g_espScreen;

inline std::vector<ModuleEntry> activeModules() {
    return {
        { "Kill Aura", &g.killAura }, { "Auto Clicker", &g.autoClicker },
        { "Velocity", &g.velocity }, { "Auto Heal", &g.autoHeal },
        { "Lag Range", &g.lagRange }, { "No Fall", &g.noFall },
        { "Anti Fire", &g.antiFire }, { "Fast Place", &g.fastPlace },
        { "Fast Break", &g.fastBreak }, { "Inventory Move", &g.inventoryMove },
        { "No Rotate", &g.noRotate }, { "Flight", &g.flight },
        { "Speed", &g.speed }, { "Sprint", &g.sprint },
        { "No Slowdown", &g.noSlowdown }, { "Step", &g.step },
        { "Long Jump", &g.longJump }, { "Scaffold", &g.scaffold },
        { "Fullbright", &g.fullbright }, { "ESP", &g.esp },
        { "FreeCam", &g.freeCam }, { "Zoom", &g.zoom },
        { "Fast Interact", &g.fastInteract }, { "More Packets", &g.morePackets },
        { "Player Detect", &g.playerDetect },
    };
}
