using System;
using System.Numerics;
using Vultra.Scripting;

namespace VultraTests;

public partial class ReferenceBeacon : Node3D
{
    [Export] public int Energy = 7;
#if VULTRA_IDENTITY_RELOAD
    [Export] public int Revision = 2;
#else
    [Export] public int Revision = 1;
#endif
    public int ReadEnergy() => Energy * Revision;
}

public sealed partial class ReferenceSpecialBeacon : ReferenceBeacon
{
}

public sealed partial class ReferenceAlien : Node3D
{
}

public sealed partial class ReferencePilot : Node3D
{
    [Export(Nullable = false)] public ReferenceBeacon? Partner;
    [Export] public ReferenceBeacon?[] Peers = [];
    [Export] public int ReadyEnergy;
    [Export] public int LastEnergy;
#if VULTRA_GROUP_NARROW
    private ReferenceAlien? _cached = null;
#else
    private ReferenceBeacon? _cached;
#endif
    private ReferenceBeacon?[] _cachedPeers = [];

    public override void _Ready()
    {
#if !VULTRA_GROUP_NARROW
        _cached = Partner;
#endif
        _cachedPeers = (ReferenceBeacon?[])Peers.Clone();
        ReadyEnergy = Partner!.ReadEnergy();
    }

    public override void _Process(double delta)
    {
        if (!ReferenceEquals(Partner, _cached) || Peers.Length != _cachedPeers.Length)
        {
            throw new InvalidOperationException("Reload did not restore typed script reference identities");
        }
        for (var index = 0; index < Peers.Length; ++index)
        {
            if (!ReferenceEquals(Peers[index], _cachedPeers[index]))
            {
                throw new InvalidOperationException("Reload did not restore typed reference array identities");
            }
        }
        LastEnergy = Partner!.ReadEnergy();
        Position = new Vector3(LastEnergy * (float)delta, Peers.Length, 0);
    }
}
