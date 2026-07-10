// pulse.cs — C# script: scales this entity with a sine pulse and tints it.
// Attach via Inspector -> Add Component -> Script, path: assets/scripts/pulse.cs
// (compiled live by `dotnet build`; edits hot-reload during Play).
using System;
using Schizo;

public static class Script
{
    public static void OnStart(Api api, uint entity)
    {
        api.Log($"pulse (C#) started on entity {entity}");
    }

    public static void OnUpdate(Api api, uint entity, float dt)
    {
        float s = 1.0f + 0.35f * MathF.Sin((float)api.Time * 4.0f);
        api.SetScale(entity, s, s, s);
        api.SetColor(entity, 0.3f + 0.7f * (s - 0.65f), 0.4f, 1.0f - 0.5f * (s - 0.65f));
    }
}
