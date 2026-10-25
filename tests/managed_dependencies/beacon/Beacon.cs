using Vultra.Scripting;

namespace DependencyTests;

public partial class Beacon : Node3D
{
    [Export] public int Power = 7;
    public int ReadPower() => Power * Energy.Multiplier;
}

public sealed partial class SpecialBeacon : Beacon
{
}

public abstract partial class ControllerBase : Node3D
{
    [Export] private int _baseValue = 3;
    public int BaseValue => _baseValue;
}
