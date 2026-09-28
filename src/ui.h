#pragma once
// Overlay UI: ImGui over the MC OpenGL context. Fully custom look — glassy,
// rounded, blurred, animated; nothing resembling vanilla Minecraft GUI.

#include <jni.h>
#include <windows.h>

namespace ui {
    // One-time init once the JVM client is confirmed alive (called on render thread).
    void onJvmReady(JNIEnv* env);

    // Called every wglSwapBuffers: draws menu + HUD, pumps input.
    void renderFrame(HDC hdc);

    // Cleanup on eject.
    void shutdown();
}

// Set by the menu's Eject button; polled by dllmain's worker thread.
void requestEject();
extern bool g_ejectRequested;
