using System;
using System.Numerics;
using Vultra.Scripting;

namespace DependencyTests;

public sealed partial class Controller : ControllerBase
{
    [Export(Nullable = false)] public Beacon? Target;
    [Export] public Beacon?[] Peers = [];
    [Export] public int ReadyPower;
#if DEPENDENCY_SENSOR
    [Export] public Sensor? Sensor;
#endif
    private Beacon? _cached;
    private Beacon?[] _cachedPeers = [];

    public override void _Ready()
    {
        _cached = Target;
        _cachedPeers = (Beacon?[])Peers.Clone();
        ReadyPower = Target!.ReadPower();
    }

    public override void _Process(double delta)
    {
        if (!ReferenceEquals(Target, _cached) || Peers.Length != _cachedPeers.Length)
        {
            throw new InvalidOperationException("Cross-assembly references were not restored");
        }
        for (var index = 0; index < Peers.Length; ++index)
        {
            if (!ReferenceEquals(Peers[index], _cachedPeers[index]))
            {
                throw new InvalidOperationException("Cross-assembly reference arrays were not restored");
            }
        }
        var power = Target!.ReadPower();
#if DEPENDENCY_SENSOR
        power += Sensor?.Signal ?? 0;
#endif
        Position = new Vector3(power * (float)delta, BaseValue, Peers.Length);
    }
}
