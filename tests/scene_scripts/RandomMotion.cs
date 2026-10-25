using System.Numerics;
using Vultra.Scripting;

namespace VultraTests;

public sealed partial class RandomMotion : Node3D
{
    [Export] public long Seed;
    [Export] public long Word;
    [Export] public long Index;
    [Export] public double Unit;

    public override void _Ready() => Seed = Scene.RandomSeed;

    public override void _Process(double delta)
    {
        Seed = Scene.RandomSeed;
        Word = Scene.RandomUInt("motion.word");
        Index = Scene.RandomUInt("motion.index", 7);
        Unit = Scene.RandomUnit("motion.unit");
        Position = new Vector3((float)(Unit * 0.5), (float)(Index * 0.02), 0);
    }
}
