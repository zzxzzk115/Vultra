namespace DependencyTests;

public static class Energy
{
#if DEPENDENCY_RELOAD
    public static int Multiplier => 2;
#else
    public static int Multiplier => 1;
#endif
}
