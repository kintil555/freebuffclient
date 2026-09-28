#include "hook_manager.h"
#include "modules.h"
#include "ui.h"

#include <MinHook.h>
#include <windows.h>
#include <thread>

extern bool g_ejectRequested;

static HMODULE g_selfModule = nullptr;

static void selfUnload() {
    // Run on our own thread: FreeLibraryAndExitThread must not run on the
    // render thread while a hook trampoline is active on it.
    hooks::uninstallAll();
    FreeLibraryAndExitThread(g_selfModule, 0);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH: {
            g_selfModule = hModule;
            DisableThreadLibraryCalls(hModule);

            // Init in a detached thread: DllMain must not load opengl32 or
            // touch GLFW (loader lock).
            std::thread([hModule]() {
                if (hooks::installRenderHook()) {
                    // Idle pump: watch for eject request from the menu.
                    while (!g_ejectRequested) Sleep(120);
                    selfUnload();
                } else {
                    MH_Uninitialize();
                }
            }).detach();
            break;
        }
        case DLL_PROCESS_DETACH:
            hooks::uninstallAll();
            break;
    }
    return TRUE;
}
