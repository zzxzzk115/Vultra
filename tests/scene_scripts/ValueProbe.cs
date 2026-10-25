using System;
using System.Numerics;
using Vultra.Scripting;

namespace VultraTests;

public enum Movement : byte
{
    Idle = 0,
    Cruise = 2
}

public sealed partial class ValueProbe : Node3D
{
    [Export] public Movement Mode;
    [Export] public short[] Samples = [1, 2];
    [Export] public Movement[] Modes = [Movement.Idle, Movement.Cruise];
    [Export] public Vector2 Uv = new(0.25f, 0.75f);
    [Export] public Vector3 Offset = new(1, 2, 3);
    [Export] public Vector4 Tint = new(1, 0, 0, 1);
    [Export] public Matrix4x4 Basis = Matrix4x4.Identity;
    [Export] public Vector3[] Vectors = [Vector3.Zero, Vector3.One];
    [Export] public Node3D? Target { get; set; }
    [Export] public Node3D?[] Peers = [];
    [Export] public Material? Material;
    [Export] public string ReadyTarget = "";
#if VULTRA_IDENTITY_RELOAD
    [Export] public float Revision = 4;
#else
    [Export] public float Revision = 1;
#endif

    public override void _Ready()
    {
        ReadyTarget = Target?.Name ?? "";
    }

    public override void _Process(double delta)
    {
        if (Target == null)
        {
            ReadyTarget = "cleared";
            return;
        }
        if (Mode != Movement.Cruise)
        {
            return;
        }
        if (!ReferenceEquals(Target, GetProperty<Node3D>("Target")))
        {
            throw new InvalidOperationException("Reference fields differ from the world's current managed identity");
        }
        float sum = Offset.X + Basis.M41 + Vectors[0].X;
        foreach (var value in Samples)
        {
            sum += value;
        }
        Position = new Vector3(sum, Target.Name.Length + Material!.Name.Length + Peers.Length + (int)Modes[0], Offset.Z + Revision);
    }
}

public sealed partial class RequiredProbe : Node3D
{
    [Export(Nullable = false)] public Node3D? Target;
}
