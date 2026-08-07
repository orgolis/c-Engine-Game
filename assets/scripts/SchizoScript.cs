// ============================================================
// Project Schizo — C# script SDK (Stage 12)
// ============================================================
// This file is compiled TOGETHER with your script into one assembly by the
// editor. Write a .cs file like:
//
//     using Schizo;
//     public static class Script {
//         public static void OnStart(Api api, uint entity) { ... }
//         public static void OnUpdate(Api api, uint entity, float dt) { ... }
//     }
//
// and attach it via Inspector -> Add Component -> Script. Saving the file
// during Play recompiles + reloads it live.
//
// NativeApi mirrors the engine's C ScriptApi table (editor/include/
// script_api.h) — layout must match field-for-field.

using System;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

namespace Schizo
{
    [StructLayout(LayoutKind.Sequential)]
    public unsafe struct NativeApi
    {
        public void* ctx;
        public float dt;
        public double time;

        public delegate* unmanaged[Cdecl]<void*, byte*, void> log;

        public delegate* unmanaged[Cdecl]<void*, byte*, uint> find_entity;
        public delegate* unmanaged[Cdecl]<void*, uint, float*, byte> get_position;
        public delegate* unmanaged[Cdecl]<void*, uint, float*, void> set_position;
        public delegate* unmanaged[Cdecl]<void*, uint, float*, byte> get_rotation_euler;
        public delegate* unmanaged[Cdecl]<void*, uint, float*, void> set_rotation_euler;
        public delegate* unmanaged[Cdecl]<void*, uint, float*, byte> get_scale;
        public delegate* unmanaged[Cdecl]<void*, uint, float*, void> set_scale;

        public delegate* unmanaged[Cdecl]<void*, int, byte> key_down;
        public delegate* unmanaged[Cdecl]<void*, int, byte> mouse_down;
        public delegate* unmanaged[Cdecl]<void*, float*, void> mouse_delta;

        public delegate* unmanaged[Cdecl]<void*, int, float*, float, float*, int, uint> spawn_primitive;
        public delegate* unmanaged[Cdecl]<void*, uint, void> destroy_entity;

        public delegate* unmanaged[Cdecl]<void*, uint, float*, void> set_velocity;
        public delegate* unmanaged[Cdecl]<void*, uint, float*, void> add_impulse;
        public delegate* unmanaged[Cdecl]<void*, float*, float*, float, float*, uint*, byte> raycast;

        public delegate* unmanaged[Cdecl]<void*, uint, float*, void> set_color;
        public delegate* unmanaged[Cdecl]<void*, uint, float*, float, void> set_emissive;

        public delegate* unmanaged[Cdecl]<void*, uint, void> audio_play;
        public delegate* unmanaged[Cdecl]<void*, uint, void> audio_stop;

        // ---- gameplay (G0-G4); appended at the end so the ABI prefix is stable ----
        public delegate* unmanaged[Cdecl]<void*, uint, byte*, float> get_attribute;
        public delegate* unmanaged[Cdecl]<void*, uint, byte*, float, void> set_attribute;
        public delegate* unmanaged[Cdecl]<void*, uint, byte*, float, void> adjust_attribute;
        public delegate* unmanaged[Cdecl]<void*, uint, byte*, byte> has_tag;
        public delegate* unmanaged[Cdecl]<void*, uint, byte*, void> add_tag;
        public delegate* unmanaged[Cdecl]<void*, uint, byte*, void> remove_tag;
        public delegate* unmanaged[Cdecl]<void*, uint, float, byte*, float> apply_damage;
        public delegate* unmanaged[Cdecl]<void*, uint, byte*, float, void> apply_heal;
        public delegate* unmanaged[Cdecl]<void*, uint, int, uint, byte> activate_ability;
        public delegate* unmanaged[Cdecl]<void*, uint, float, void> grant_xp;
        public delegate* unmanaged[Cdecl]<void*, uint, int> get_level;
        public delegate* unmanaged[Cdecl]<void*, uint, byte*, byte> unlock_skill;
        public delegate* unmanaged[Cdecl]<void*, uint, byte*, byte> send_state_event;
        public delegate* unmanaged[Cdecl]<void*, uint, byte*, byte> in_state;
        public delegate* unmanaged[Cdecl]<void*, uint, byte*, int, int> add_item;
        public delegate* unmanaged[Cdecl]<void*, uint, byte*, int> item_count;
        public delegate* unmanaged[Cdecl]<void*, uint, byte*, byte> equip_item;
        public delegate* unmanaged[Cdecl]<void*, uint, byte*, byte> use_item;
        public delegate* unmanaged[Cdecl]<void*, uint, byte> interact;
    }

    /// Friendly wrapper handed to your OnStart/OnUpdate.
    public unsafe class Api
    {
        private readonly NativeApi* p;
        public Api(IntPtr native) { p = (NativeApi*)native; }

        public float Dt => p->dt;
        public double Time => p->time;

        public void Log(string msg)
        {
            var b = Encoding.UTF8.GetBytes(msg + "\0");
            fixed (byte* bp = b) p->log(p->ctx, bp);
        }
        public uint Find(string name)
        {
            var b = Encoding.UTF8.GetBytes(name + "\0");
            fixed (byte* bp = b) return p->find_entity(p->ctx, bp);
        }

        public (float x, float y, float z) GetPosition(uint e)
        { float* v = stackalloc float[3]; p->get_position(p->ctx, e, v); return (v[0], v[1], v[2]); }
        public void SetPosition(uint e, float x, float y, float z)
        { float* v = stackalloc float[3] { x, y, z }; p->set_position(p->ctx, e, v); }
        public (float x, float y, float z) GetRotation(uint e)
        { float* v = stackalloc float[3]; p->get_rotation_euler(p->ctx, e, v); return (v[0], v[1], v[2]); }
        public void SetRotation(uint e, float xDeg, float yDeg, float zDeg)
        { float* v = stackalloc float[3] { xDeg, yDeg, zDeg }; p->set_rotation_euler(p->ctx, e, v); }
        public (float x, float y, float z) GetScale(uint e)
        { float* v = stackalloc float[3]; p->get_scale(p->ctx, e, v); return (v[0], v[1], v[2]); }
        public void SetScale(uint e, float x, float y, float z)
        { float* v = stackalloc float[3] { x, y, z }; p->set_scale(p->ctx, e, v); }

        public bool KeyDown(int glfwKey) => p->key_down(p->ctx, glfwKey) != 0;
        public bool MouseDown(int button) => p->mouse_down(p->ctx, button) != 0;
        public (float dx, float dy) MouseDelta()
        { float* v = stackalloc float[2]; p->mouse_delta(p->ctx, v); return (v[0], v[1]); }

        public uint SpawnCube(float x, float y, float z, float size,
                              float r, float g, float b, bool dynamic)
        {
            float* pos = stackalloc float[3] { x, y, z };
            float* col = stackalloc float[4] { r, g, b, 1.0f };
            return p->spawn_primitive(p->ctx, 0, pos, size, col, dynamic ? 1 : 0);
        }
        public uint SpawnSphere(float x, float y, float z, float size,
                                float r, float g, float b, bool dynamic)
        {
            float* pos = stackalloc float[3] { x, y, z };
            float* col = stackalloc float[4] { r, g, b, 1.0f };
            return p->spawn_primitive(p->ctx, 1, pos, size, col, dynamic ? 1 : 0);
        }
        public void Destroy(uint e) => p->destroy_entity(p->ctx, e);

        public void SetVelocity(uint e, float x, float y, float z)
        { float* v = stackalloc float[3] { x, y, z }; p->set_velocity(p->ctx, e, v); }
        public void AddImpulse(uint e, float x, float y, float z)
        { float* v = stackalloc float[3] { x, y, z }; p->add_impulse(p->ctx, e, v); }
        public bool Raycast(float ox, float oy, float oz, float dx, float dy, float dz,
                            float maxDist, out float hx, out float hy, out float hz, out uint entity)
        {
            float* o = stackalloc float[3] { ox, oy, oz };
            float* d = stackalloc float[3] { dx, dy, dz };
            float* hp = stackalloc float[3];
            uint he = 0;
            byte hit = p->raycast(p->ctx, o, d, maxDist, hp, &he);
            hx = hp[0]; hy = hp[1]; hz = hp[2]; entity = he;
            return hit != 0;
        }

        public void SetColor(uint e, float r, float g, float b, float a = 1.0f)
        { float* v = stackalloc float[4] { r, g, b, a }; p->set_color(p->ctx, e, v); }
        public void SetEmissive(uint e, float r, float g, float b, float intensity)
        { float* v = stackalloc float[3] { r, g, b }; p->set_emissive(p->ctx, e, v, intensity); }

        public void AudioPlay(uint e) => p->audio_play(p->ctx, e);
        public void AudioStop(uint e) => p->audio_stop(p->ctx, e);

        // ---- gameplay (G0-G4) ----
        private static byte[] Utf8(string s) => Encoding.UTF8.GetBytes(s + "\0");

        public float GetAttribute(uint e, string name)
        { var b = Utf8(name); fixed (byte* bp = b) return p->get_attribute(p->ctx, e, bp); }
        public void SetAttribute(uint e, string name, float value)
        { var b = Utf8(name); fixed (byte* bp = b) p->set_attribute(p->ctx, e, bp, value); }
        public void AdjustAttribute(uint e, string name, float delta)
        { var b = Utf8(name); fixed (byte* bp = b) p->adjust_attribute(p->ctx, e, bp, delta); }
        public bool HasTag(uint e, string tag)
        { var b = Utf8(tag); fixed (byte* bp = b) return p->has_tag(p->ctx, e, bp) != 0; }
        public void AddTag(uint e, string tag)
        { var b = Utf8(tag); fixed (byte* bp = b) p->add_tag(p->ctx, e, bp); }
        public void RemoveTag(uint e, string tag)
        { var b = Utf8(tag); fixed (byte* bp = b) p->remove_tag(p->ctx, e, bp); }
        public float ApplyDamage(uint e, float amount, string type)
        { var b = Utf8(type); fixed (byte* bp = b) return p->apply_damage(p->ctx, e, amount, bp); }
        public void ApplyHeal(uint e, string attribute, float amount)
        { var b = Utf8(attribute); fixed (byte* bp = b) p->apply_heal(p->ctx, e, bp, amount); }
        public bool ActivateAbility(uint e, int index, uint target = 0)
        => p->activate_ability(p->ctx, e, index, target) != 0;
        public void GrantXp(uint e, float amount) => p->grant_xp(p->ctx, e, amount);
        public int GetLevel(uint e) => p->get_level(p->ctx, e);
        public bool UnlockSkill(uint e, string nodeId)
        { var b = Utf8(nodeId); fixed (byte* bp = b) return p->unlock_skill(p->ctx, e, bp) != 0; }
        public bool SendStateEvent(uint e, string ev)
        { var b = Utf8(ev); fixed (byte* bp = b) return p->send_state_event(p->ctx, e, bp) != 0; }
        public bool InState(uint e, string state)
        { var b = Utf8(state); fixed (byte* bp = b) return p->in_state(p->ctx, e, bp) != 0; }
        public int AddItem(uint e, string defId, int qty)
        { var b = Utf8(defId); fixed (byte* bp = b) return p->add_item(p->ctx, e, bp, qty); }
        public int ItemCount(uint e, string defId)
        { var b = Utf8(defId); fixed (byte* bp = b) return p->item_count(p->ctx, e, bp); }
        public bool EquipItem(uint e, string defId)
        { var b = Utf8(defId); fixed (byte* bp = b) return p->equip_item(p->ctx, e, bp) != 0; }
        public bool UseItem(uint e, string defId)
        { var b = Utf8(defId); fixed (byte* bp = b) return p->use_item(p->ctx, e, bp) != 0; }
        public bool Interact(uint e) => p->interact(p->ctx, e) != 0;

        // GLFW key codes.
        public const int KeySpace = 32;
        public const int KeyA = 65, KeyD = 68, KeyE = 69, KeyF = 70, KeyG = 71;
        public const int KeyQ = 81, KeyS = 83, KeyW = 87;
        public const int KeyArrowRight = 262, KeyArrowLeft = 263,
                         KeyArrowDown = 264, KeyArrowUp = 265;
        public const int KeyLShift = 340, KeyLCtrl = 341;
        public const int MouseLeft = 0, MouseRight = 1, MouseMiddle = 2;
    }

    /// Native entry points the engine binds via hostfxr. Dispatches to the
    /// user's `Script.OnStart/OnUpdate` (resolved once via reflection so
    /// either hook may be omitted). Exceptions are logged, not fatal.
    public static class Bridge
    {
        private static MethodInfo? _start;
        private static MethodInfo? _update;
        private static bool _resolved;

        private static void Resolve()
        {
            if (_resolved) return;
            _resolved = true;
            var t = Assembly.GetExecutingAssembly().GetType("Script");
            _start  = t?.GetMethod("OnStart",  BindingFlags.Public | BindingFlags.Static);
            _update = t?.GetMethod("OnUpdate", BindingFlags.Public | BindingFlags.Static);
        }

        [UnmanagedCallersOnly]
        public static void Start(IntPtr api, uint entity)
        {
            Resolve();
            try { _start?.Invoke(null, new object[] { new Api(api), entity }); }
            catch (Exception ex) { new Api(api).Log("C# OnStart exception: " + (ex.InnerException ?? ex).Message); }
        }

        [UnmanagedCallersOnly]
        public static void Update(IntPtr api, uint entity, float dt)
        {
            Resolve();
            try { _update?.Invoke(null, new object[] { new Api(api), entity, dt }); }
            catch (Exception ex) { new Api(api).Log("C# OnUpdate exception: " + (ex.InnerException ?? ex).Message); }
        }
    }
}
