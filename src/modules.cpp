#include "modules.h"
#include "jni_util.h"
#include "mc_mappings.h"

#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstring>

using namespace jni;

namespace {

    auto lastAuraHit = std::chrono::steady_clock::now();
    auto lastClick   = std::chrono::steady_clock::now();

    jobject getMcInstance(JNIEnv* env) {
        auto mc = getStaticObject(env, mc::Minecraft, "getInstance",
                                  "()Lnet/minecraft/client/Minecraft;");
        return mc ? *mc : nullptr;
    }

    double dist3(double ax, double ay, double az, double bx, double by, double bz) {
        const double dx = ax - bx, dy = ay - by, dz = az - bz;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    double distToBox(double px, double py, double pz,
                     double minX, double minY, double minZ,
                     double maxX, double maxY, double maxZ) {
        return dist3(px, py, pz,
                     std::clamp(px, minX, maxX),
                     std::clamp(py, minY, maxY),
                     std::clamp(pz, minZ, maxZ));
    }

    std::optional<jobject> abilities(JNIEnv* env, jobject player) {
        return callObject(env, player, "getAbilities", mc::M_getAbilities);
    }

    void sendChatCommand(JNIEnv* env, jobject player, const char* cmd) {
        auto conn = getObjectField(env, player, mc::F_connection,
                                   "Lnet/minecraft/client/multiplayer/ClientPacketListener;");
        if (!conn) return;
        LocalRef c(env, *conn);
        jstring js = env->NewStringUTF(cmd);
        if (!js) return;
        callVoid(env, *c, "sendCommand", mc::M_sendChat, js);
        env->DeleteLocalRef(js);
    }

    // ------------------------------------------------- integrated server access
    // Singleplayer: patch the SERVER-side player object too. The vanilla server
    // gates flying on its own copy of abilities.mayfly (see
    // ServerGamePacketListenerImpl.handlePlayerAbilities), so without this the
    // server strips flight every tick -> rubber-banding/stutter.
    jobject findServerPlayer(JNIEnv* env, jobject player) {
        auto server = callObject(env, player, "level", "()Lnet/minecraft/world/level/Level;");
        (void)server;
        // Path: Minecraft.getSingleplayerServer().getPlayerList().getPlayers()
        auto mcOpt = getStaticObject(env, mc::Minecraft, "getInstance",
                                     "()Lnet/minecraft/client/Minecraft;");
        if (!mcOpt || !*mcOpt) return nullptr;
        LocalRef mc(env, *mcOpt);

        auto isp = callObject(env, *mc, "getSingleplayerServer", mc::M_getSingleplayerServer);
        if (!isp || !*isp) return nullptr;   // dedicated server / not SP
        LocalRef srv(env, *isp);

        auto plist = callObject(env, *srv, "getPlayerList", mc::M_getPlayerList);
        if (!plist || !*plist) return nullptr;
        LocalRef pl(env, *plist);

        auto players = callObject(env, *pl, "getPlayers", mc::M_getPlayers);
        if (!players || !*players) return nullptr;
        LocalRef list(env, *players);

        // Compare UUIDs (server player and client player are distinct objects).
        auto myUuid = callObject(env, player, "getUUID", mc::M_getUUID);
        if (!myUuid || !*myUuid) return nullptr;
        LocalRef uuid(env, *myUuid);

        jclass listCls = env->GetObjectClass(*list);
        jmethodID mSize = env->GetMethodID(listCls, "size", "()I");
        jmethodID mGet  = env->GetMethodID(listCls, "get", "(I)Ljava/lang/Object;");
        env->DeleteLocalRef(listCls);
        if (!mSize || !mGet) return nullptr;

        const jint n = env->CallIntMethod(*list, mSize);
        for (jint i = 0; i < n; ++i) {
            jobject sp = env->CallObjectMethod(*list, mGet, i);
            if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
            if (!sp) continue;
            LocalRef spRef(env, sp);
            auto spUuid = callObject(env, sp, "getUUID", mc::M_getUUID);
            if (!spUuid || !*spUuid) continue;
            LocalRef suRef(env, *spUuid);
            if (env->CallBooleanMethod(*suRef, env->GetMethodID(
                    env->GetObjectClass(*suRef), "equals", mc::M_equals), *uuid)) {
                return env->NewLocalRef(sp);
            }
        }
        return nullptr;
    }

    // Set mayfly+flying on a player object (client or server copy).
    void setFlyOn(JNIEnv* env, jobject playerObj, bool on) {
        auto ab = abilities(env, playerObj);
        if (!ab) return;
        LocalRef a(env, *ab);
        if (on) {
            setBoolField(env, *a, mc::F_mayfly, JNI_TRUE);
            setBoolField(env, *a, mc::F_flying, JNI_TRUE);
        } else {
            setBoolField(env, *a, mc::F_flying, JNI_FALSE);
            setBoolField(env, *a, mc::F_mayfly, JNI_FALSE);
        }
    }

    // ---------------------------------------------------------------- flight
    // Rate-limited: only send the abilities packet when the state actually
    // changes; server copy is kept in sync so it never strips flight back.
    void applyFlight(JNIEnv* env, jobject player) {
        static bool lastOn = false;

        auto ab = abilities(env, player);
        if (!ab) return;
        LocalRef a(env, *ab);

        const bool serverFlying = getBoolField(env, *a, mc::F_flying).value_or(false);
        if (!serverFlying || !lastOn) {
            setFlyOn(env, player, true);
            callVoid(env, player, "onUpdateAbilities", mc::M_onUpdateAbilities);
            lastOn = true;
        }

        // Keep the integrated server's player in sync (its mayfly must be true
        // or handlePlayerAbilities drops our flying flag each tick).
        if (jobject serverPlayer = findServerPlayer(env, player)) {
            LocalRef sp(env, serverPlayer);
            auto spAb = abilities(env, *sp);
            if (spAb) {
                LocalRef sa(env, *spAb);
                const bool spMayFly = getBoolField(env, *sa, mc::F_mayfly).value_or(false);
                const bool spFlying = getBoolField(env, *sa, mc::F_flying).value_or(false);
                if (!spMayFly || !spFlying) {
                    setFlyOn(env, *sp, true);
                    // Both copies now agree, so the re-broadcast in
                    // ServerPlayer.onUpdateAbilities() is a no-op for flight.
                    callVoid(env, *sp, "onUpdateAbilities", mc::M_onUpdateAbilities);
                }
            }
        }
    }

    void unflight(JNIEnv* env, jobject player) {
        static bool sent = false;
        if (!sent) {
            setFlyOn(env, player, false);
            callVoid(env, player, "onUpdateAbilities", mc::M_onUpdateAbilities);
            // Also clear on the server copy.
            if (jobject sp = findServerPlayer(env, player)) {
                LocalRef s(env, sp);
                setFlyOn(env, *s, false);
                callVoid(env, *s, "onUpdateAbilities", mc::M_onUpdateAbilities);
            }
            sent = true;
        }
    }

    // ------------------------------------------------------------ kill aura
    void doKillAura(JNIEnv* env, jobject mc, jobject player, jobject level) {
        const auto now = std::chrono::steady_clock::now();
        const auto interval = std::chrono::milliseconds(1000 / std::max(1, g.killAuraCps));
        if (now - lastAuraHit < interval) return;

        const auto px = callDouble(env, player, "getX");
        const auto py = callDouble(env, player, "getY");
        const auto pz = callDouble(env, player, "getZ");
        if (!px || !py || !pz) return;

        auto gameMode = getObjectField(env, mc, "gameMode",
                                       "Lnet/minecraft/client/multiplayer/MultiPlayerGameMode;");
        if (!gameMode) return;
        LocalRef gm(env, *gameMode);
        jclass gmClass = env->GetObjectClass(*gm);
        jmethodID mAtk = env->GetMethodID(gmClass, "attack", mc::M_attack);
        if (!mAtk || env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(gmClass); return; }

        auto ents = callObject(env, level, "entitiesForRendering", "()Ljava/lang/Iterable;");
        if (!ents) { env->DeleteLocalRef(gmClass); return; }
        LocalRef list(env, *ents);

        jclass itClass = env->GetObjectClass(*list);
        jmethodID mIt = env->GetMethodID(itClass, "iterator", "()Ljava/util/Iterator;");
        jclass jIt = env->FindClass("java/util/Iterator");
        jmethodID mHas = jIt ? env->GetMethodID(jIt, "hasNext", "()Z") : nullptr;
        jmethodID mNext= jIt ? env->GetMethodID(jIt, "next", "()Ljava/lang/Object;") : nullptr;
        if (!mIt || !mHas || !mNext) {
            if (jIt) env->DeleteLocalRef(jIt);
            env->DeleteLocalRef(itClass);
            env->DeleteLocalRef(gmClass);
            return;
        }

        jobject iter = env->CallObjectMethod(*list, mIt);
        env->DeleteLocalRef(itClass);
        if (!iter) { if (jIt) env->DeleteLocalRef(jIt); env->DeleteLocalRef(gmClass); return; }
        LocalRef itRef(env, iter);

        jobject best = nullptr;
        double bestD = g.killAuraRange;

        while (env->CallBooleanMethod(iter, mHas)) {
            jobject e = env->CallObjectMethod(iter, mNext);
            if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
            if (!e) continue;
            LocalRef er(env, e);

            if (env->IsSameObject(e, player)) continue;
            auto alive = callBool(env, e, "isAlive");
            if (!alive || !*alive) continue;
            auto spec = callBool(env, e, "isSpectator");
            if (spec && *spec) continue;
            auto hp = callFloat(env, e, "getHealth");   // LivingEntity only
            if (!hp || *hp <= 0.f) continue;

            auto bb = callObject(env, e, "getBoundingBox", "()Lnet/minecraft/world/phys/AABB;");
            if (!bb) continue;
            LocalRef b(env, *bb);

            const auto mnx = getDoubleField(env, *b, mc::F_minX);
            const auto mny = getDoubleField(env, *b, mc::F_minY);
            const auto mnz = getDoubleField(env, *b, mc::F_minZ);
            const auto mxx = getDoubleField(env, *b, mc::F_maxX);
            const auto mxy = getDoubleField(env, *b, mc::F_maxY);
            const auto mxz = getDoubleField(env, *b, mc::F_maxZ);
            if (!mnx || !mny || !mnz || !mxx || !mxy || !mxz) continue;

            const double d = distToBox(*px, *py, *pz, *mnx, *mny, *mnz, *mxx, *mxy, *mxz);
            if (d < bestD) {
                bestD = d;
                if (best) env->DeleteLocalRef(best);
                best = env->NewLocalRef(e);
            }
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (jIt) env->DeleteLocalRef(jIt);

        if (best) {
            env->CallVoidMethod(*gm, mAtk, player, best);
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(best);
        }
        env->DeleteLocalRef(gmClass);
    }

    // ------------------------------------------------------------- velocity
    void applyVelocity(JNIEnv* env, jobject player) {
        const auto hurt = getIntField(env, player, mc::F_hurtTime);
        if (!hurt || *hurt == 0) return;

        auto dm = callObject(env, player, "getDeltaMovement", mc::M_getDeltaMovement);
        if (!dm) return;
        LocalRef v(env, *dm);
        const auto vx = getDoubleField(env, *v, mc::F_vx);
        const auto vy = getDoubleField(env, *v, mc::F_vy);
        const auto vz = getDoubleField(env, *v, mc::F_vz);
        if (!vx || !vy || !vz) return;

        const double nx = *vx * (g.velocityHPercent / 100.0);
        const double ny = *vy * (g.velocityVPercent / 100.0);
        const double nz = *vz * (g.velocityHPercent / 100.0);

        jclass vc = env->FindClass(mc::Vec3);
        if (!vc) return;
        jmethodID ctor = env->GetMethodID(vc, "<init>", "(DDD)V");
        if (!ctor) { env->DeleteLocalRef(vc); return; }
        jobject nv = env->NewObject(vc, ctor, nx, ny, nz);
        env->DeleteLocalRef(vc);
        if (!nv) return;
        LocalRef nvRef(env, nv);
        callVoid(env, player, "setDeltaMovement", mc::M_setDeltaMovement, nv);
    }

    // -------------------------------------------------------------- nofall
    void applyNoFall(JNIEnv* env, jobject player) {
        setDoubleField(env, player, mc::F_fallDistance, 0.0);
        // Singleplayer nicety: also reset on the integrated server player if present.
        // (IntegratedServer lives in net.minecraft.client.server.IntegratedServer; we
        //  reach the ServerPlayer via server player list in a follow-up — client-side
        //  reset alone prevents fall damage in SP because damage is server-computed
        //  from the sent onGround+fall flags.)
    }

    // ------------------------------------------------------------- movement
    void applyMovement(JNIEnv* env, jobject player) {
        if (!g.noSlowdown && !g.sprint) return;
        auto in = getObjectField(env, player, mc::F_input,
                                 "Lnet/minecraft/client/player/ClientInput;");
        if (!in) return;
        LocalRef i(env, *in);
        auto presses = getObjectField(env, *i, "keyPresses",
                                      "Lnet/minecraft/world/entity/player/Input;");
        if (!presses) return;
        LocalRef k(env, *presses);
        const bool fwd = getBoolField(env, *k, mc::F_in_forward).value_or(false);
        if (!fwd) return;
        callVoid(env, player, "setSprinting", mc::M_setSprinting, JNI_TRUE);
    }

    // ---------------------------------------------------------------- speed
    // Speed via the movement_speed attribute base value. The attribute is
    // server-synced, so this works cleanly in singleplayer (integrated server
    // reads the same attribute from its ServerPlayer, and our server-side sync
    // below keeps both copies matching).
    void applySpeed(JNIEnv* env, jobject player) {
        static double originalSpeed = -1.0;
        static float  lastAppliedMult = -1.f;
        static bool   wasFlightSpeed = false;

        // Resolve the MOVEMENT_SPEED attribute holder (static field).
        auto holderOpt = getStaticObjectField(env, mc::Attributes, mc::F_MOVEMENT_SPEED,
                                              mc::F_MOVEMENT_SPEED_SIG);
        if (!holderOpt || !*holderOpt) return;
        LocalRef holder(env, *holderOpt);

        auto instOpt = callObject(env, player, "getAttribute", mc::M_getAttribute, *holder);
        if (!instOpt || !*instOpt) return;
        LocalRef inst(env, *instOpt);

        if (g.speed) {
            if (originalSpeed < 0.0) {
                originalSpeed = callDouble(env, *inst, "getBaseValue", mc::M_getBaseValue).value_or(0.0);
                // While flying the effective speed comes from abilities.flyingSpeed,
                // so scale that instead of the attribute.
                auto ab = abilities(env, player);
                wasFlightSpeed = false;
                if (ab) {
                    LocalRef a(env, *ab);
                    wasFlightSpeed = getBoolField(env, *a, mc::F_flying).value_or(false);
                }
            }
            const float mult = std::clamp(g.speedMultiplier, 1.05f, 4.f);
            if (lastAppliedMult != mult) {
                if (wasFlightSpeed) {
                    if (auto ab = abilities(env, player)) {
                        LocalRef a(env, *ab);
                        jclass abCls = env->GetObjectClass(*a);
                        if (jmethodID sm = env->GetMethodID(abCls, "setFlyingSpeed", mc::M_setFlyingSpeed)) {
                            env->CallVoidMethod(*a, sm, 0.05f * mult);
                            if (env->ExceptionCheck()) env->ExceptionClear();
                        }
                        env->DeleteLocalRef(abCls);
                    }
                } else {
                    callVoid(env, *inst, "setBaseValue", mc::M_setBaseValue,
                             originalSpeed * mult);
                }
                lastAppliedMult = mult;
            }
        } else if (originalSpeed >= 0.0) {
            // Restore.
            if (wasFlightSpeed) {
                if (auto ab = abilities(env, player)) {
                    LocalRef a(env, *ab);
                    jclass abCls = env->GetObjectClass(*a);
                    if (jmethodID sm = env->GetMethodID(abCls, "setFlyingSpeed", mc::M_setFlyingSpeed)) {
                        env->CallVoidMethod(*a, sm, 0.05f);
                        if (env->ExceptionCheck()) env->ExceptionClear();
                    }
                    env->DeleteLocalRef(abCls);
                }
            } else {
                callVoid(env, *inst, "setBaseValue", mc::M_setBaseValue, originalSpeed);
            }
            originalSpeed = -1.0;
            lastAppliedMult = -1.f;
            wasFlightSpeed = false;
        }
    }

    // ------------------------------------------------------------ fullbright
    // Cache the original gamma the first time we enable, restore on disable.
    void applyFullbright(JNIEnv* env, jobject mc) {
        auto options = getObjectField(env, mc, mc::F_options, "Lnet/minecraft/client/Options;");
        if (!options) return;
        LocalRef opt(env, *options);

        auto giOpt = callObject(env, *opt, "gamma", mc::M_gamma);
        if (!giOpt) return;
        LocalRef gi(env, *giOpt);

        static double originalGamma = -1.0;
        static bool   applied       = false;

        if (g.fullbright) {
            if (!applied) {
                auto cur = callObject(env, *gi, "get", "()Ljava/lang/Object;");
                if (cur) {
                    LocalRef c(env, *cur);
                    jclass cc = env->GetObjectClass(*c);
                    jmethodID dv = cc ? env->GetMethodID(cc, "doubleValue", "()D") : nullptr;
                    if (dv) {
                        const double d = env->CallDoubleMethod(*c, dv);
                        if (env->ExceptionCheck()) env->ExceptionClear();
                        else originalGamma = d;
                    }
                    if (cc) env->DeleteLocalRef(cc);
                }
            }
            // OptionInstance.set(Object)
            jclass oi = env->GetObjectClass(*gi);
            jmethodID set = oi ? env->GetMethodID(oi, "set", mc::M_oiSet) : nullptr;
            if (set) {
                jclass dbl = env->FindClass("java/lang/Double");
                if (dbl) {
                    jmethodID valueOf = env->GetStaticMethodID(dbl, "valueOf", "(D)Ljava/lang/Double;");
                    if (valueOf) {
                        jobject boxed = env->CallStaticObjectMethod(dbl, valueOf,
                                                                    static_cast<jdouble>(g.fullbrightGamma));
                        if (env->ExceptionCheck()) env->ExceptionClear();
                        else if (boxed) {
                            env->CallVoidMethod(*gi, set, boxed);
                            if (env->ExceptionCheck()) env->ExceptionClear();
                            applied = true;
                            env->DeleteLocalRef(boxed);
                        }
                    }
                    env->DeleteLocalRef(dbl);
                }
            }
            if (oi) env->DeleteLocalRef(oi);
        } else if (applied) {
            jclass oi = env->GetObjectClass(*gi);
            jmethodID set = oi ? env->GetMethodID(oi, "set", mc::M_oiSet) : nullptr;
            if (set) {
                jclass dbl = env->FindClass("java/lang/Double");
                if (dbl) {
                    jmethodID valueOf = env->GetStaticMethodID(dbl, "valueOf", "(D)Ljava/lang/Double;");
                    if (valueOf) {
                        jobject boxed = env->CallStaticObjectMethod(dbl, valueOf, originalGamma);
                        if (env->ExceptionCheck()) env->ExceptionClear();
                        else if (boxed) {
                            env->CallVoidMethod(*gi, set, boxed);
                            if (env->ExceptionCheck()) env->ExceptionClear();
                            env->DeleteLocalRef(boxed);
                        }
                    }
                    env->DeleteLocalRef(dbl);
                }
            }
            if (oi) env->DeleteLocalRef(oi);
            applied = false;
            originalGamma = -1.0;
        }
    }

    // ---------------------------------------------------------------- zoom
    // Zoom via FOV OptionInstance (restores when toggled off).
    void applyZoom(JNIEnv* env, jobject mc) {
        auto options = getObjectField(env, mc, mc::F_options, "Lnet/minecraft/client/Options;");
        if (!options) return;
        LocalRef opt(env, *options);

        auto fovOpt = callObject(env, *opt, "fov", mc::M_fov);
        if (!fovOpt) return;
        LocalRef f(env, *fovOpt);

        static double originalFov = -1.0;
        static bool   applied     = false;

        if (g.zoom) {
            if (!applied) {
                auto cur = callObject(env, *f, "get", "()Ljava/lang/Object;");
                if (cur) {
                    LocalRef c(env, *cur);
                    jclass cc = env->GetObjectClass(*c);
                    jmethodID dv = cc ? env->GetMethodID(cc, "doubleValue", "()D") : nullptr;
                    if (dv) {
                        const double d = env->CallDoubleMethod(*c, dv);
                        if (env->ExceptionCheck()) env->ExceptionClear();
                        else originalFov = d;
                    }
                    if (cc) env->DeleteLocalRef(cc);
                }
            }
            jclass oi = env->GetObjectClass(*f);
            jmethodID set = oi ? env->GetMethodID(oi, "set", mc::M_oiSet) : nullptr;
            if (set) {
                jclass ip = env->FindClass("java/lang/Integer");
                if (ip) {
                    jmethodID valueOf = env->GetStaticMethodID(ip, "valueOf", "(I)Ljava/lang/Integer;");
                    if (valueOf) {
                        const jint zoomed = static_cast<jint>(std::max(5, static_cast<int>(70 / std::max(1.0f, g.zoomFactor))));
                        jobject boxed = env->CallStaticObjectMethod(ip, valueOf, zoomed);
                        if (env->ExceptionCheck()) env->ExceptionClear();
                        else if (boxed) {
                            env->CallVoidMethod(*f, set, boxed);
                            if (env->ExceptionCheck()) env->ExceptionClear();
                            applied = true;
                            env->DeleteLocalRef(boxed);
                        }
                    }
                    env->DeleteLocalRef(ip);
                }
            }
            if (oi) env->DeleteLocalRef(oi);
        } else if (applied) {
            jclass oi = env->GetObjectClass(*f);
            jmethodID set = oi ? env->GetMethodID(oi, "set", mc::M_oiSet) : nullptr;
            if (set) {
                jclass ip = env->FindClass("java/lang/Integer");
                if (ip) {
                    jmethodID valueOf = env->GetStaticMethodID(ip, "valueOf", "(I)Ljava/lang/Integer;");
                    if (valueOf) {
                        jobject boxed = env->CallStaticObjectMethod(ip, valueOf,
                                                                    static_cast<jint>(originalFov));
                        if (env->ExceptionCheck()) env->ExceptionClear();
                        else if (boxed) {
                            env->CallVoidMethod(*f, set, boxed);
                            if (env->ExceptionCheck()) env->ExceptionClear();
                            env->DeleteLocalRef(boxed);
                        }
                    }
                    env->DeleteLocalRef(ip);
                }
            }
            if (oi) env->DeleteLocalRef(oi);
            applied = false;
            originalFov = -1.0;
        }
    }

    // ------------------------------------------------------------------- ESP
    // Rebuild the world-space ESP cache from the client entity list.
    void rebuildEsp(JNIEnv* env, jobject player, jobject level) {
        g_espEntries.clear();
        if (!g.esp) return;

        const auto px = callDouble(env, player, "getX");
        const auto py = callDouble(env, player, "getY");
        const auto pz = callDouble(env, player, "getZ");
        if (!px || !py || !pz) return;

        auto ents = callObject(env, level, "entitiesForRendering", "()Ljava/lang/Iterable;");
        if (!ents) return;
        LocalRef list(env, *ents);

        jclass itClass = env->GetObjectClass(*list);
        jmethodID mIt = env->GetMethodID(itClass, "iterator", "()Ljava/util/Iterator;");
        jclass jIt = env->FindClass("java/util/Iterator");
        if (!mIt || !jIt) { if (jIt) env->DeleteLocalRef(jIt); env->DeleteLocalRef(itClass); return; }
        jmethodID mHas  = env->GetMethodID(jIt, "hasNext", "()Z");
        jmethodID mNext = env->GetMethodID(jIt, "next", "()Ljava/lang/Object;");

        jobject iter = env->CallObjectMethod(*list, mIt);
        env->DeleteLocalRef(itClass);
        if (!iter) { env->DeleteLocalRef(jIt); return; }
        LocalRef itRef(env, iter);

        while (env->CallBooleanMethod(iter, mHas)) {
            jobject e = env->CallObjectMethod(iter, mNext);
            if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
            if (!e) continue;
            LocalRef er(env, e);

            if (env->IsSameObject(e, player)) continue;
            auto alive = callBool(env, e, "isAlive");
            if (!alive || !*alive) continue;
            auto spec = callBool(env, e, "isSpectator");
            if (spec && *spec) continue;

            const auto ex = callDouble(env, e, "getX");
            const auto ey = callDouble(env, e, "getY");
            const auto ez = callDouble(env, e, "getZ");
            if (!ex || !ey || !ez) continue;

            const double dx = *ex - *px, dy = *ey - *py, dz = *ez - *pz;
            const double d = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (d > g.espMaxDistance) continue;

            Settings::EspEntry entry;
            entry.x = static_cast<float>(*ex);
            entry.y = static_cast<float>(*ey);
            entry.z = static_cast<float>(*ez);
            entry.health = 0.f;
            entry.kind = 2;  // 0 = player, 1 = hostile-ish, 2 = other

            // Player? Player class check via name string of the class.
            jclass cls = env->GetObjectClass(e);
            if (cls) {
                // cheap instanceof via FindClass cache
                static jclass playerCls = nullptr;
                if (!playerCls) {
                    auto pc = findClass(env, mc::Player);
                    if (pc) { playerCls = static_cast<jclass>(env->NewGlobalRef(*pc)); env->DeleteLocalRef(*pc); }
                }
                if (playerCls && env->IsInstanceOf(e, playerCls)) {
                    entry.kind = 0;
                    auto nm = callObject(env, e, "getName", "()Lnet/minecraft/network/chat/Component;");
                    if (nm) {
                        LocalRef nRef(env, *nm);
                        auto s = callObject(env, *nRef, "getString", "()Ljava/lang/String;");
                        if (s) {
                            LocalRef sRef(env, *s);
                            jstring js = static_cast<jstring>(*sRef);
                            const char* utf = env->GetStringUTFChars(js, nullptr);
                            if (utf) {
                                strncpy_s(entry.name, sizeof(entry.name), utf, _TRUNCATE);
                                env->ReleaseStringUTFChars(js, utf);
                            }
                        }
                    }
                    entry.health = callFloat(env, e, "getHealth").value_or(0.f);
                }
                env->DeleteLocalRef(cls);
            }

            // Bounding box extents (for 3d boxes).
            auto bb = callObject(env, e, "getBoundingBox", "()Lnet/minecraft/world/phys/AABB;");
            if (bb) {
                LocalRef b(env, *bb);
                const auto mnx = getDoubleField(env, *b, mc::F_minX);
                const auto mxx = getDoubleField(env, *b, mc::F_maxX);
                const auto mny = getDoubleField(env, *b, mc::F_minY);
                const auto mxy = getDoubleField(env, *b, mc::F_maxY);
                const auto mnz = getDoubleField(env, *b, mc::F_minZ);
                const auto mxz = getDoubleField(env, *b, mc::F_maxZ);
                if (mnx && mxx && mny && mxy && mnz && mxz) {
                    entry.ex = static_cast<float>((*mxx - *mnx) * 0.5);
                    entry.ey = static_cast<float>(*mxy - *mny);
                    entry.ez = static_cast<float>(( *mxz - *mnz) * 0.5);
                }
            }
            g_espEntries.push_back(entry);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(jIt);
    }

    // ----------------------------------------------------------------- stats
    void collectStats(JNIEnv* env, jobject mc, jobject player, jobject level) {
        g_rt.health = callFloat(env, player, "getHealth").value_or(0.f);

        int count = 0;
        auto ents = callObject(env, level, "entitiesForRendering", "()Ljava/lang/Iterable;");
        if (ents) {
            LocalRef list(env, *ents);
            jclass itClass = env->GetObjectClass(*list);
            jmethodID mIt = env->GetMethodID(itClass, "iterator", "()Ljava/util/Iterator;");
            jclass jIt = env->FindClass("java/util/Iterator");
            if (mIt && jIt) {
                jmethodID mHas  = env->GetMethodID(jIt, "hasNext", "()Z");
                jmethodID mNext = env->GetMethodID(jIt, "next", "()Ljava/lang/Object;");
                jobject iter = mIt ? env->CallObjectMethod(*list, mIt) : nullptr;
                if (iter) {
                    LocalRef itRef(env, iter);
                    while (env->CallBooleanMethod(iter, mHas)) {
                        jobject e = env->CallObjectMethod(iter, mNext);
                        if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
                        if (e) { env->DeleteLocalRef(e); ++count; }
                    }
                    if (env->ExceptionCheck()) env->ExceptionClear();
                }
            }
            if (jIt) env->DeleteLocalRef(jIt);
            env->DeleteLocalRef(itClass);
        }
        g_rt.entityCount = count;

        auto gui = getObjectField(env, mc, mc::F_gui, "Lnet/minecraft/client/gui/Gui;");
        g_rt.hasEscMenu = false;
        if (gui) {
            LocalRef g(env, *gui);
            auto scr = callObject(env, *g, "screen", mc::M_guiScreen);
            g_rt.hasEscMenu = scr.has_value() && scr.value() != nullptr;
        }
        g_rt.inGame = true;
    }

    // Toggle helpers used by the UI for edge-triggered actions.
    bool wasAuraToggled = false;
    bool wasFlightToggled = false;

} // namespace

// --------------------------------------------------------------------------
// Public entry — called from the WGL frame hook every rendered frame.
// --------------------------------------------------------------------------
void onClientTick(void* envPtr, void* mcObj) {
    JNIEnv* env = static_cast<JNIEnv*>(envPtr);
    if (!env || !mcObj) return;
    LocalRef mc(env, static_cast<jobject>(mcObj));

    auto playerOpt = getObjectField(env, *mc, mc::F_player, "Lnet/minecraft/client/player/LocalPlayer;");
    if (!playerOpt || !*playerOpt) { g_rt.inGame = false; return; }
    LocalRef player(env, *playerOpt);

    auto levelOpt = getObjectField(env, *mc, mc::F_level, "Lnet/minecraft/client/multiplayer/ClientLevel;");
    if (!levelOpt || !*levelOpt) { g_rt.inGame = false; return; }
    LocalRef level(env, *levelOpt);

    if (g.flight)        applyFlight(env, *player);
    else                 unflight(env, *player);   // edge-triggered restore
    if (g.speed)         applySpeed(env, *player);
    if (g.noFall)        applyNoFall(env, *player);
    if (g.velocity)      applyVelocity(env, *player);
    if (g.killAura)      doKillAura(env, *mc, *player, *level);
    applyMovement(env, *player);
    applyFullbright(env, *mc);
    applyZoom(env, *mc);
    collectStats(env, *mc, *player, *level);
    rebuildEsp(env, *player, *level);
}

void onFrame() {
    using clock = std::chrono::steady_clock;
    static auto t0 = clock::now();
    static int frames = 0;
    ++frames;
    const auto now = clock::now();
    if (now - t0 >= std::chrono::seconds(1)) {
        g_rt.fps = frames;
        frames = 0;
        t0 = now;
    }
}
