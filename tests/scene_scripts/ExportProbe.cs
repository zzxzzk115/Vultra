using System.Numerics;
using Vultra.Scripting;

namespace VultraTests;

public sealed partial class ExportProbe : Node3D
{
#if VULTRA_IDENTITY_RELOAD
    [Export] public float Speed = 4;
#else
    [Export] public float Speed = 1;
#endif
    [Export] public float ReadySpeed;
#if VULTRA_GROUP_NARROW
    [Export] public sbyte Steps;
#else
    [Export] public int Steps;
#endif
    [Export] public bool Enabled = true;
    [Export] public string Caption = "ready";
    private float _offset;

    [Export]
    public float Offset
    {
        get => _offset;
        set
        {
            _offset = value;
            Position = new Vector3(Position.X, value, Position.Z);
        }
    }

    public override void _Ready()
    {
        if (GetPropertyList().GetProperty("version").GetInt32() != 1)
        {
            throw new System.InvalidOperationException("Node omitted script property metadata");
        }
        SetProperty("ReadySpeed", GetProperty<float>("Speed"));
    }

    public override void _Process(double delta)
    {
        if (Enabled)
        {
            Position = new Vector3(Position.X + Speed * (float)delta, _offset, Position.Z);
            ++Steps;
        }
    }
}
