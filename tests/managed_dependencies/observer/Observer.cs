using System;
using System.Numerics;
using Vultra.Scripting;

namespace DependencyTests;

public sealed partial class Observer : Node3D
{
    [Export(Nullable = false)] public Sensor? Target;
    private Sensor? _cached;

    public override void _Ready()
    {
        _cached = Target;
    }

    public override void _Process(double delta)
    {
        if (!ReferenceEquals(Target, _cached))
        {
            throw new InvalidOperationException("The reverse dependency retained an old script identity");
        }
        Position = new Vector3(Target!.Signal * (float)delta, 0, 0);
    }
}
