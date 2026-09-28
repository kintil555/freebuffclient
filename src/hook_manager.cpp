#include "hook_manager.h"
#include "modules.h"
#include "mc_mappings.h"
#include "jni_util.h"
#include "ui.h"

#include <MinHook.h>

using namespace jni;

namespace {

    // ---- wglSwapBuffers detour ------------------------------------------------
    using wglSwapBuffers_t = BOOL(WINAPI*)(HDC);
    wglSwapBuffers_t o_wglSwapBuffers = nullptr;

    bool g_renderHookInstalled = false;
    bool g_clientReady         = false;

    // Cached JVM globals (render-thread only).
    jclass    g_mcClass    = nullptr;
    jmethodID g_getInstance = nullptr;

    void cacheGlobals(JNIEnv* env) {
        if (g_mcClass) return;
        auto c = findClass(env, mc::Minecraft);
        if (!c) return;
        g_mcClass = static_cast<jclass>(env->NewGlobalRef(*c));
        env->DeleteLocalRef(*c);
        g_getInstance = env->GetStaticMethodID(g_mcClass, "getInstance",
                                               "()Lnet/minecraft/client/Minecraft;");
        if (env->ExceptionCheck()) { env->ExceptionClear(); g_getInstance = nullptr; }
    }

    BOOL WINAPI h_wglSwapBuffers(HDC hdc) {
        static thread_local bool inFrame = false;
        if (!inFrame) {
            inFrame = true;
            if (!g_clientReady) {
                JniEnv jni = JniEnv::get();
                if (jni.env) {
                    jobject probe = hooks::getMinecraftInstance(jni.env);
                    if (probe) {
                        jni.env->DeleteLocalRef(probe);
                        g_clientReady = true;
                        hooks::onJvmReady(jni.env);
                    }
                }
            }
            if (g_clientReady) {
                hooks::onRenderFrame();     // module logic (frame-driven)
                ui::renderFrame(hdc);       // draw overlay + input
                onFrame();                  // fps counter (modules.h)
            }
            inFrame = false;
        }
        return o_wglSwapBuffers(hdc);
    }
}

namespace hooks {

    void onJvmReady(JNIEnv* env) {
        cacheGlobals(env);
        ui::onJvmReady(env);
    }

    bool installRenderHook() {
        if (g_renderHookInstalled) return true;

        if (MH_Initialize() != MH_OK) return false;

        if (MH_CreateHookApi(L"opengl32", "wglSwapBuffers",
                             reinterpret_cast<LPVOID>(&h_wglSwapBuffers),
                             reinterpret_cast<LPVOID*>(&o_wglSwapBuffers)) != MH_OK) {
            return false;
        }
        if (MH_EnableHook(reinterpret_cast<LPVOID>(&h_wglSwapBuffers)) != MH_OK) {
            return false;
        }
        g_renderHookInstalled = true;
        return true;
    }

    void uninstallAll() {
        if (g_renderHookInstalled) {
            ui::shutdown();
            MH_DisableHook(MH_ALL_HOOKS);
            MH_Uninitialize();
            g_renderHookInstalled = false;
        }
    }

    jobject getMinecraftInstance(JNIEnv* env) {
        if (!g_mcClass || !g_getInstance) cacheGlobals(env);
        if (!g_mcClass || !g_getInstance) return nullptr;
        jobject mcObj = env->CallStaticObjectMethod(g_mcClass, g_getInstance);
        if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
        return mcObj;
    }

    void onRenderFrame() {
        JniEnv jni = JniEnv::get();
        if (!jni.env) return;
        jobject mcObj = getMinecraftInstance(jni.env);
        if (!mcObj) { g_rt.inGame = false; return; }
        LocalRef mc(jni.env, mcObj);
        onClientTick(jni.env, mcObj);   // modules.h
    }
}
