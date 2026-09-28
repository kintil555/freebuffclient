#pragma once
// Thin, safe JNI wrappers. Everything returns Optionals/defaults on failure so
// a bad lookup can never crash the game — the module just disables itself.

#include <jni.h>
#include <windows.h>
#include <string>
#include <optional>

#include "mc_mappings.h"

namespace jni {

    using GetCreatedJavaVMsFn = jint (JNICALL *)(JavaVM **, jsize, jsize *);

    // We are injected into the JVM process: jvm.dll is already loaded, so we
    // resolve JNI_GetCreatedJavaVMs dynamically instead of linking a stub.
    inline GetCreatedJavaVMsFn resolveGetCreatedJavaVMs() {
        static GetCreatedJavaVMsFn fn = nullptr;
        if (fn) return fn;
        HMODULE jvm = GetModuleHandleA("jvm.dll");
        if (!jvm) return nullptr;
        fn = reinterpret_cast<GetCreatedJavaVMsFn>(
            reinterpret_cast<void*>(GetProcAddress(jvm, "JNI_GetCreatedJavaVMs")));
        return fn;
    }

    struct JniEnv {
        JNIEnv* env = nullptr;
        bool    attached = false;

        static JniEnv get() {
            JniEnv out;
            auto getVMs = resolveGetCreatedJavaVMs();
            if (!getVMs) return out;
            JavaVM* vm = nullptr;
            jsize count = 0;
            if (getVMs(&vm, 1, &count) != JNI_OK || count < 1 || !vm) return out;
            void* penv = nullptr;
            if (vm->GetEnv(&penv, JNI_VERSION_10) != JNI_OK || !penv) {
                if (vm->AttachCurrentThreadAsDaemon(&penv, nullptr) == JNI_OK)
                    out.attached = true;
            }
            out.env = static_cast<JNIEnv*>(penv);
            return out;
        }
        ~JniEnv() {
            if (attached && env) {
                JavaVM* vm = nullptr;
                env->GetJavaVM(&vm);
                if (vm) vm->DetachCurrentThread();
            }
        }
    };

    // RAII local-ref guard so we never leak JNI local refs in the render loop.
    struct LocalRef {
        JNIEnv* env = nullptr;
        jobject obj = nullptr;
        LocalRef() = default;
        LocalRef(JNIEnv* e, jobject o) : env(e), obj(o) {}
        LocalRef(const LocalRef&) = delete;
        LocalRef& operator=(const LocalRef&) = delete;
        LocalRef(LocalRef&& o) noexcept : env(o.env), obj(o.obj) { o.obj = nullptr; }
        LocalRef& operator=(LocalRef&& o) noexcept {
            reset();
            env = o.env; obj = o.obj; o.obj = nullptr;
            return *this;
        }
        ~LocalRef() { reset(); }

        // Unary dereference: `*ref` yields the raw jobject.
        jobject operator*() const { return obj; }
        jobject get() const { return obj; }
        bool operator==(std::nullptr_t) const { return obj == nullptr; }
        bool operator!=(std::nullptr_t) const { return obj != nullptr; }

        void reset() {
            if (env && obj) env->DeleteLocalRef(obj);
            obj = nullptr;
        }
        bool valid() const { return obj != nullptr; }
    };

    inline std::optional<jclass> findClass(JNIEnv* env, const char* name) {
        if (!env) return std::nullopt;
        jclass c = env->FindClass(name);
        if (env->ExceptionCheck()) { env->ExceptionClear(); return std::nullopt; }
        if (!c) return std::nullopt;
        return c;
    }

    inline std::optional<jobject> getStaticObject(JNIEnv* env, const char* cls, const char* method, const char* sig) {
        auto c = findClass(env, cls);
        if (!c) return std::nullopt;
        jmethodID m = env->GetStaticMethodID(*c, method, sig);
        if (!m || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(*c); return std::nullopt; }
        jobject r = env->CallStaticObjectMethod(*c, m);
        if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(*c); return std::nullopt; }
        env->DeleteLocalRef(*c);
        return r;
    }

    inline std::optional<jobject> getStaticObjectField(JNIEnv* env, const char* cls, const char* field, const char* sig) {
        auto c = findClass(env, cls);
        if (!c) return std::nullopt;
        jfieldID f = env->GetStaticFieldID(*c, field, sig);
        if (!f || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(*c); return std::nullopt; }
        jobject r = env->GetStaticObjectField(*c, f);
        if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(*c); return std::nullopt; }
        env->DeleteLocalRef(*c);
        return r;
    }

    inline std::optional<jobject> getObjectField(JNIEnv* env, jobject obj, const char* field, const char* sig) {
        if (!env || !obj) return std::nullopt;
        jclass c = env->GetObjectClass(obj);
        if (!c) return std::nullopt;
        jfieldID f = env->GetFieldID(c, field, sig);
        if (!f || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(c); return std::nullopt; }
        jobject r = env->GetObjectField(obj, f);
        env->DeleteLocalRef(c);
        return r;
    }

    inline std::optional<jboolean> getBoolField(JNIEnv* env, jobject obj, const char* field) {
        if (!env || !obj) return std::nullopt;
        jclass c = env->GetObjectClass(obj);
        if (!c) return std::nullopt;
        jfieldID f = env->GetFieldID(c, field, "Z");
        if (!f || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(c); return std::nullopt; }
        jboolean r = env->GetBooleanField(obj, f);
        env->DeleteLocalRef(c);
        return r;
    }

    inline void setBoolField(JNIEnv* env, jobject obj, const char* field, jboolean v) {
        if (!env || !obj) return;
        jclass c = env->GetObjectClass(obj);
        if (!c) return;
        jfieldID f = env->GetFieldID(c, field, "Z");
        if (!f || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(c); return; }
        env->SetBooleanField(obj, f, v);
        env->DeleteLocalRef(c);
    }

    inline std::optional<jint> getIntField(JNIEnv* env, jobject obj, const char* field) {
        if (!env || !obj) return std::nullopt;
        jclass c = env->GetObjectClass(obj);
        if (!c) return std::nullopt;
        jfieldID f = env->GetFieldID(c, field, "I");
        if (!f || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(c); return std::nullopt; }
        jint r = env->GetIntField(obj, f);
        env->DeleteLocalRef(c);
        return r;
    }

    inline std::optional<jfloat> getFloatField(JNIEnv* env, jobject obj, const char* field) {
        if (!env || !obj) return std::nullopt;
        jclass c = env->GetObjectClass(obj);
        if (!c) return std::nullopt;
        jfieldID f = env->GetFieldID(c, field, "F");
        if (!f || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(c); return std::nullopt; }
        jfloat r = env->GetFloatField(obj, f);
        env->DeleteLocalRef(c);
        return r;
    }

    inline std::optional<jdouble> getDoubleField(JNIEnv* env, jobject obj, const char* field) {
        if (!env || !obj) return std::nullopt;
        jclass c = env->GetObjectClass(obj);
        if (!c) return std::nullopt;
        jfieldID f = env->GetFieldID(c, field, "D");
        if (!f || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(c); return std::nullopt; }
        jdouble r = env->GetDoubleField(obj, f);
        env->DeleteLocalRef(c);
        return r;
    }

    inline void setDoubleField(JNIEnv* env, jobject obj, const char* field, jdouble v) {
        if (!env || !obj) return;
        jclass c = env->GetObjectClass(obj);
        if (!c) return;
        jfieldID f = env->GetFieldID(c, field, "D");
        if (!f || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(c); return; }
        env->SetDoubleField(obj, f, v);
        env->DeleteLocalRef(c);
    }

    inline std::optional<jdouble> callDouble(JNIEnv* env, jobject obj, const char* method, const char* sig = "()D") {
        if (!env || !obj) return std::nullopt;
        jclass c = env->GetObjectClass(obj);
        if (!c) return std::nullopt;
        jmethodID m = env->GetMethodID(c, method, sig);
        if (!m || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(c); return std::nullopt; }
        jdouble r = env->CallDoubleMethod(obj, m);
        if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(c); return std::nullopt; }
        env->DeleteLocalRef(c);
        return r;
    }

    inline std::optional<jfloat> callFloat(JNIEnv* env, jobject obj, const char* method, const char* sig = "()F") {
        if (!env || !obj) return std::nullopt;
        jclass c = env->GetObjectClass(obj);
        if (!c) return std::nullopt;
        jmethodID m = env->GetMethodID(c, method, sig);
        if (!m || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(c); return std::nullopt; }
        jfloat r = env->CallFloatMethod(obj, m);
        if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(c); return std::nullopt; }
        env->DeleteLocalRef(c);
        return r;
    }

    inline std::optional<jfloat> callFloatArg(JNIEnv* env, jobject obj, const char* method, const char* sig, jfloat arg) {
        if (!env || !obj) return std::nullopt;
        jclass c = env->GetObjectClass(obj);
        if (!c) return std::nullopt;
        jmethodID m = env->GetMethodID(c, method, sig);
        if (!m || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(c); return std::nullopt; }
        jfloat r = env->CallFloatMethod(obj, m, arg);
        if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(c); return std::nullopt; }
        env->DeleteLocalRef(c);
        return r;
    }

    inline std::optional<jint> callInt(JNIEnv* env, jobject obj, const char* method, const char* sig = "()I") {
        if (!env || !obj) return std::nullopt;
        jclass c = env->GetObjectClass(obj);
        if (!c) return std::nullopt;
        jmethodID m = env->GetMethodID(c, method, sig);
        if (!m || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(c); return std::nullopt; }
        jint r = env->CallIntMethod(obj, m);
        if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(c); return std::nullopt; }
        env->DeleteLocalRef(c);
        return r;
    }

    inline std::optional<jboolean> callBool(JNIEnv* env, jobject obj, const char* method, const char* sig = "()Z") {
        if (!env || !obj) return std::nullopt;
        jclass c = env->GetObjectClass(obj);
        if (!c) return std::nullopt;
        jmethodID m = env->GetMethodID(c, method, sig);
        if (!m || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(c); return std::nullopt; }
        jboolean r = env->CallBooleanMethod(obj, m);
        if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(c); return std::nullopt; }
        env->DeleteLocalRef(c);
        return r;
    }

    inline bool callVoid(JNIEnv* env, jobject obj, const char* method, const char* sig, ...) {
        if (!env || !obj) return false;
        jclass c = env->GetObjectClass(obj);
        if (!c) return false;
        jmethodID m = env->GetMethodID(c, method, sig);
        if (!m || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(c); return false; }
        va_list args;
        va_start(args, sig);
        env->CallVoidMethodV(obj, m, args);
        va_end(args);
        bool ok = !env->ExceptionCheck();
        if (!ok) env->ExceptionClear();
        env->DeleteLocalRef(c);
        return ok;
    }

    inline std::optional<jobject> callObject(JNIEnv* env, jobject obj, const char* method, const char* sig) {
        if (!env || !obj) return std::nullopt;
        jclass c = env->GetObjectClass(obj);
        if (!c) return std::nullopt;
        jmethodID m = env->GetMethodID(c, method, sig);
        if (!m || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(c); return std::nullopt; }
        jobject r = env->CallObjectMethod(obj, m);
        if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(c); return std::nullopt; }
        env->DeleteLocalRef(c);
        return r;
    }

    inline std::string jstringToStd(JNIEnv* env, jstring s) {
        if (!env || !s) return {};
        const char* c = env->GetStringUTFChars(s, nullptr);
        if (!c) return {};
        std::string out(c);
        env->ReleaseStringUTFChars(s, c);
        return out;
    }
}
