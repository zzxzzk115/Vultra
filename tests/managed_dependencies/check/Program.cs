using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using Vultra.ManagedHost;

internal static class Program
{
    private static void Require(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference[] CheckGroups(string directory, bool ownerFirst)
    {
        var groups = new List<ScriptAssemblyGroup>();
        var batch = new ScriptAssemblyBatch(() => groups, false);
        var beacon = Path.Combine(directory, "DependencyBeacon.dll");
        var controller = Path.Combine(directory, "DependencyController.dll");
        var first = batch.Acquire(ownerFirst ? controller : beacon);
        groups.Add(first);
        var second = batch.Acquire(ownerFirst ? beacon : controller);
        groups.Add(second);
        var beaconGroup = ownerFirst ? second : first;
        var controllerGroup = ownerFirst ? first : second;
        var type = beaconGroup.GetAssembly(beacon).GetType("DependencyTests.Beacon", true)!;
        var consumer = controllerGroup.GetAssembly(controller).GetType("DependencyTests.Controller", true)!;
        Require(consumer.GetField("Target")!.FieldType == type && consumer.BaseType!.Assembly == type.Assembly,
            "Separate roots did not share the same CLR dependency identity");
        Require(controllerGroup.Origin(type.Assembly) == beacon,
            "A stream-loaded external type lost its original source path");
        var contexts = new[] { first.GetAssembly(ownerFirst ? controller : beacon),
                               second.GetAssembly(ownerFirst ? beacon : controller) }
            .Select(AssemblyLoadContext.GetLoadContext).Distinct().Select(context => new WeakReference(context!)).ToArray();
        // The dependent group must keep a shared context alive after its original instance retires.
        first.Release();
        groups.RemoveAt(0);
        controllerGroup.VerifyFiles();
        var helper = Path.Combine(directory, "DependencyHelper.dll");
        var later = new ScriptAssemblyBatch(() => groups, false);
        var helperAttachment = later.Acquire(helper);
        Require(ReferenceEquals(helperAttachment.GetAssembly(helper), controllerGroup.GetAssembly(helper)),
            "A later attachment did not reuse the transitive dependency's CLR identity");
        helperAttachment.Release();
        second.Release();
        groups.Clear();
        return contexts;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference[] CheckIsolation(string directory)
    {
        var source = Path.Combine(directory, "DependencySensor.dll");
        var alias = Path.Combine(directory, "IndependentSensor.dll");
        File.Copy(source, alias);
        var batch = new ScriptAssemblyBatch(() => [], false);
        var first = batch.Acquire(source);
        var second = batch.Acquire(alias);
        var firstType = first.GetAssembly(source).GetType("DependencyTests.Sensor", true)!;
        var secondType = second.GetAssembly(alias).GetType("DependencyTests.Sensor", true)!;
        Require(firstType != secondType && firstType.Assembly.GetName().Name == secondType.Assembly.GetName().Name,
            "Independent sources with the same assembly identity were conflated");
        var contexts = new[] { firstType.Assembly, secondType.Assembly }
            .Select(assembly => new WeakReference(AssemblyLoadContext.GetLoadContext(assembly)!)).ToArray();
        first.Release();
        second.Release();
        return contexts;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference CheckSnapshot(string directory)
    {
        var batch = new ScriptAssemblyBatch(() => [], false);
        var path = Path.Combine(directory, "DependencyController.dll");
        var group = batch.Acquire(path);
        var assembly = group.GetAssembly(path);
        var context = new WeakReference(AssemblyLoadContext.GetLoadContext(assembly)!);
        var helper = Path.Combine(directory, "DependencyHelper.dll");
        var original = File.ReadAllBytes(helper);
        var changed = (byte[])original.Clone();
        changed[^1] ^= 1;
        File.WriteAllBytes(helper, changed);
        var rejected = false;
        try
        {
            group.VerifyFiles();
        }
        catch (IOException)
        {
            rejected = true;
        }
        finally
        {
            File.WriteAllBytes(helper, original);
        }
        Require(rejected, "Changing a dependency during staging was not rejected");
        group.VerifyFiles();
        var manifest = Path.Combine(directory, "DependencyController.deps.json");
        var manifestBytes = File.ReadAllBytes(manifest);
        File.AppendAllText(manifest, "\n");
        rejected = false;
        try
        {
            group.VerifyFiles();
        }
        catch (IOException)
        {
            rejected = true;
        }
        finally
        {
            File.WriteAllBytes(manifest, manifestBytes);
        }
        Require(rejected, "Changing a dependency manifest during staging was not rejected");
        group.VerifyFiles();
        group.Release();
        return context;
    }

    private static int Main()
    {
        var fixtures = Path.GetFullPath("build/.tmp/managed-dependency-fixtures/initial");
        var directory = Path.GetFullPath("build/.tmp/managed-assembly-check-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        foreach (var file in Directory.EnumerateFiles(fixtures))
        {
            File.Copy(file, Path.Combine(directory, Path.GetFileName(file)));
        }
        var contexts = CheckGroups(directory, true).Concat(CheckGroups(directory, false)).Concat(CheckIsolation(directory))
            .Append(CheckSnapshot(directory)).ToArray();
        for (var attempt = 0; attempt < 20 && contexts.Any(context => context.IsAlive); ++attempt)
        {
            GC.Collect();
            GC.WaitForPendingFinalizers();
        }
        Require(contexts.All(context => !context.IsAlive), "Retired managed dependency groups retained collectible contexts");
        Console.WriteLine("Managed assembly groups passed: CLR identity, original paths, dependency leases, snapshot rejection and collectible unload");
        return 0;
    }
}
