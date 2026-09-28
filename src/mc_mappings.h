#pragma once
// Verified against the decompiled Minecraft 26.2 jar (official Mojang names, no obfuscation).
// Every signature below was confirmed by reading the actual classes in the client jar.

namespace mc {
    // ---- classes -----------------------------------------------------------
    constexpr auto Minecraft       = "net/minecraft/client/Minecraft";
    constexpr auto LocalPlayer     = "net/minecraft/client/player/LocalPlayer";
    constexpr auto Entity          = "net/minecraft/world/entity/Entity";
    constexpr auto LivingEntity    = "net/minecraft/world/entity/LivingEntity";
    constexpr auto Player          = "net/minecraft/world/entity/player/Player";
    constexpr auto Abilities       = "net/minecraft/world/entity/player/Abilities";
    constexpr auto ClientInput     = "net/minecraft/client/player/ClientInput";
    constexpr auto Input           = "net/minecraft/world/entity/player/Input";
    constexpr auto Vec3            = "net/minecraft/world/phys/Vec3";
    constexpr auto Vec2            = "net/minecraft/world/phys/Vec2";
    constexpr auto KeyMapping      = "net/minecraft/client/KeyMapping";
    constexpr auto Options         = "net/minecraft/client/Options";
    constexpr auto OptionInstance  = "net/minecraft/client/OptionInstance";
    constexpr auto MouseHandler    = "net/minecraft/client/MouseHandler";
    constexpr auto Gui             = "net/minecraft/client/gui/Gui";
    constexpr auto Window          = "com/mojang/blaze3d/platform/Window";
    constexpr auto GameRenderer    = "net/minecraft/client/renderer/GameRenderer";
    constexpr auto Camera          = "net/minecraft/client/Camera";
    constexpr auto ClientPacketListener = "net/minecraft/client/multiplayer/ClientPacketListener";
    constexpr auto Component       = "net/minecraft/network/chat/Component";
    constexpr auto InteractionHand = "net/minecraft/world/InteractionHand";

    // ---- Minecraft fields (public) ------------------------------------------
    // public ClientLevel level; public LocalPlayer player; public Options options;
    // public MultiPlayerGameMode gameMode; public HitResult hitResult;
    // public final Gui gui; public final GameRenderer gameRenderer;
    // public final MouseHandler mouseHandler; public final KeyboardHandler keyboardHandler;
    // public Entity crosshairPickEntity;
    constexpr auto F_level       = "level";
    constexpr auto F_player      = "player";
    constexpr auto F_options     = "options";
    constexpr auto F_gui         = "gui";
    constexpr auto F_mouseHandler = "mouseHandler";
    // static Minecraft getInstance()

    // ---- LocalPlayer / Player ------------------------------------------------
    // public final ClientPacketListener connection;  public ClientInput input;
    constexpr auto F_connection  = "connection";
    constexpr auto F_input       = "input";
    // public int hurtTime;              (LivingEntity)
    constexpr auto F_hurtTime    = "hurtTime";

    // ---- Entity --------------------------------------------------------------
    // public boolean noPhysics;  public boolean hurtMarked;  public double fallDistance;
    constexpr auto F_noPhysics    = "noPhysics";
    constexpr auto F_hurtMarked   = "hurtMarked";
    constexpr auto F_fallDistance = "fallDistance";
    // float getXRot()  float getYRot()  double getX()  double getY()  double getZ()
    // void setPos(double,double,double)  Vec3 position()  AABB getBoundingBox()
    // Vec3 getDeltaMovement()  void setDeltaMovement(Vec3)  boolean isAlive()
    // void setSprinting(boolean)  boolean isSprinting()
    constexpr auto M_getXRot = "(F)F";            // getXRot
    constexpr auto M_getYRot = "(F)F";            // getYRot  (arg is partialTick)
    constexpr auto M_getX    = "()D";
    constexpr auto M_getY    = "()D";
    constexpr auto M_getZ    = "()D";
    constexpr auto M_setPos  = "(DDD)V";
    constexpr auto M_setDeltaMovement = "(Lnet/minecraft/world/phys/Vec3;)V";
    constexpr auto M_getDeltaMovement = "()Lnet/minecraft/world/phys/Vec3;";
    constexpr auto M_setSprinting = "(Z)V";
    constexpr auto M_isSprinting  = "()Z";

    // ---- Abilities (all public fields) ---------------------------------------
    // public boolean invulnerable, flying, mayfly, instabuild, mayBuild;
    constexpr auto F_invulnerable = "invulnerable";
    constexpr auto F_flying       = "flying";
    constexpr auto F_mayfly       = "mayfly";
    constexpr auto F_instabuild   = "instabuild";
    // Player.getAbilities() -> Abilities ; Player.onUpdateAbilities()
    constexpr auto M_getAbilities      = "()Lnet/minecraft/world/entity/player/Abilities;";
    constexpr auto M_onUpdateAbilities = "()V";

    // ---- Input (record) -------------------------------------------------------
    // record Input(boolean forward, backward, left, right, jump, shift, sprint)
    constexpr auto F_in_forward = "forward";
    constexpr auto F_in_backward= "backward";
    constexpr auto F_in_left    = "left";
    constexpr auto F_in_right   = "right";
    constexpr auto F_in_jump    = "jump";
    constexpr auto F_in_sprint  = "sprint";

    // ---- Vec3 -----------------------------------------------------------------
    // public final double x, y, z;
    constexpr auto F_vx = "x"; constexpr auto F_vy = "y"; constexpr auto F_vz = "z";

    // ---- AABB -----------------------------------------------------------------
    // public final double minX, minY, minZ, maxX, maxY, maxZ;
    constexpr auto F_minX = "minX"; constexpr auto F_minY = "minY"; constexpr auto F_minZ = "minZ";
    constexpr auto F_maxX = "maxX"; constexpr auto F_maxY = "maxY"; constexpr auto F_maxZ = "maxZ";

    // ---- KeyMapping / Options --------------------------------------------------
    // boolean isDown()  void setDown(boolean)  boolean consumeClick()
    // Options: public final KeyMapping keyJump / keyAttack / keyUse / keySprint ...
    constexpr auto F_keyJump   = "keyJump";
    constexpr auto F_keyAttack = "keyAttack";
    constexpr auto F_keyUse    = "keyUse";
    constexpr auto F_keySprint = "keySprint";
    // Options.fov() -> OptionInstance<Integer>;  Options.gamma() -> OptionInstance<Double>
    constexpr auto M_fov   = "()Lnet/minecraft/client/OptionInstance;";
    constexpr auto M_gamma = "()Lnet/minecraft/client/OptionInstance;";
    // OptionInstance.set(Object)
    constexpr auto M_oiSet = "(Ljava/lang/Object;)V";

    // ---- MouseHandler -----------------------------------------------------------
    // double xpos()  double ypos()  void grabMouse()  void releaseMouse()
    // boolean isMouseGrabbed()

    // ---- Gui ---------------------------------------------------------------------
    // public Screen screen()   (current screen; used to detect menus open)
    constexpr auto M_guiScreen = "()Lnet/minecraft/client/gui/screens/Screen;";

    // ---- Camera / GameRenderer -----------------------------------------------------
    // GameRenderer.mainCamera() -> Camera ; Camera.position() -> Vec3 ; Camera.rotation() -> Quaternionf
    constexpr auto M_mainCamera = "()Lnet/minecraft/client/Camera;";
    constexpr auto M_camPos     = "()Lnet/minecraft/world/phys/Vec3;";
    constexpr auto M_camRot     = "()Lorg/joml/Quaternionf;";

    // ---- Window -----------------------------------------------------------------------
    // long handle()  int getWidth()  int getHeight()  int getGuiScaledWidth()  int getGuiScaledHeight()

    // ---- ClientPacketListener (extends ClientCommonPacketListenerImpl) -----------------
    // void send(Packet)  void sendChat(String)  void sendCommand(String)
    constexpr auto M_send      = "(Lnet/minecraft/network/packet/Packet;)V";
    constexpr auto M_sendChat  = "(Ljava/lang/String;)V";

    // ---- MultiPlayerGameMode -------------------------------------------------------------
    // void attack(Player, Entity)
    constexpr auto M_attack = "(Lnet/minecraft/world/entity/player/Player;Lnet/minecraft/world/entity/Entity;)V";

    // ---- LivingEntity ----------------------------------------------------------------------
    // float getHealth()  void swing(InteractionHand)  boolean isUsingItem()
    constexpr auto M_getHealth  = "()F";
    constexpr auto M_swing      = "(Lnet/minecraft/world/InteractionHand;)V";
    constexpr auto M_isUsingItem= "()Z";

    // ---- Component ------------------------------------------------------------------------------
    // static Component.literal(String) -> MutableComponent (usable as Component)
    constexpr auto M_literal = "(Ljava/lang/String;)Lnet/minecraft/network/chat/MutableComponent;";
}
