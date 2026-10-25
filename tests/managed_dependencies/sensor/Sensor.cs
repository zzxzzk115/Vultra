using Vultra.Scripting;

namespace DependencyTests;

public sealed partial class Sensor : Node3D
{
    [Export] public int Signal = 5;
}
