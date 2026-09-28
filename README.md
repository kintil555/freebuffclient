# NOVA — utility overlay for Minecraft 26.2 (Fabric, singleplayer)

A self-contained C++ utility client for **Minecraft 26.2 (Fabric Loader 0.19.5)**, built
exactly for that version — no yarn, no mappings needed: the game ships with official
Mojang class names and we talk to it directly over JNI.

- **100% C++** — injected DLL (`nova.dll`) + standalone injector (`nova_injector.exe`)
- **ImGui** menu with a fully custom glassy violet theme (rounded everything, animated
  gradient banner, glowing pills, ESP overlay, HUD chips)
- **MinHook** detour on `wglSwapBuffers` (MC 26.2 defaults to the OpenGL backend on Windows)
- **Injector**: one button, animated — finds `javaw.exe`, injects, reports progress

## Build

Everything builds on GitHub Actions (MSVC, windows-latest). Push to `main` and grab the
`nova-26.2-win64` artifact from the Actions run — it contains:

```
nova.dll
nova_injector.exe
```

No local toolchain needed — all compilation happens in CI.

## Use

1. Launch Minecraft 26.2 (singleplayer world).
2. Run `nova_injector.exe` (keep it next to `nova.dll`).
3. Click **inject**.
4. In game, press **Insert** to toggle the menu. **Eject** button unloads the DLL.

## Modules

| Combat | Player | Movement | Visual | Exploits |
|---|---|---|---|---|
| Kill Aura (range + CPS) | No Fall | Flight | Fullbright (gamma) | Fast Interact |
| Auto Clicker | Anti Fire (visual) | Speed | ESP (boxes/names/hp) | More Packets (SP-safe) |
| Velocity (h/v %) | Fast Place / Break | Auto Sprint | FreeCam (flag) | Player Detect |
| Auto Heal (HUD alert) | Inventory Move | No Slowdown | Zoom | |
| Lag Range | No Server Rotation | Step / Long Jump / Scaffold | HUD | |

Modules are applied **frame-driven** through the render hook — every frame the DLL
resolves `Minecraft.getInstance()` via JNI and applies enabled module state to the live
`LocalPlayer`, `Abilities`, `ClientInput`, `Options`, etc. Nothing is version-fragile:
all signatures were verified against the actual 26.2 jar (see `src/mc_mappings.h`).

## Safety

- Every JNI call is exception-guarded; a failed lookup disables the module instead of
  crashing the game.
- Fullbright/Zoom restore your original gamma/FOV when toggled off.
- **Eject** removes the WGL hook and unloads cleanly.
- Singleplayer only: the integrated server trusts the client, so client-side ability
  edits are authoritative.
