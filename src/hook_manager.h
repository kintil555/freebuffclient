#pragma once
// Rendering-side hooks. MC 26.2 defaults to the OpenGL backend on Windows
// (PreferredGraphicsApi.DEFAULT -> GlBackend first), so the stable frame hook
// is wglSwapBuffers in opengl32.dll. Vulkan fallback users simply don't get
// the overlay (accepted limitation for a singleplayer utility).

#include <jni.h>

namespace hooks {
    // Install the wglSwapBuffers detour. Call once at DLL init.
    bool installRenderHook();

    // Remove everything (eject). Call on DLL_PROCESS_DETACH.
    void uninstallAll();

    // Called once when the JVM + client are first reachable (render thread).
    void onJvmReady(JNIEnv* env);

    // Runs one frame of module logic (fetches Minecraft instance internally).
    void onRenderFrame();

    // Cached Minecraft.getInstance() — returns a local ref or null.
    jobject getMinecraftInstance(JNIEnv* env);
}
