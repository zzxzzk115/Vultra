using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Text.Json;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Vultra.Scripting;
using Vultra.Scripting.Generators;

internal static class Program
{
    private static readonly CSharpParseOptions ParseOptions = new(LanguageVersion.Latest);
    private static readonly MetadataReference[] References = ((string)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")!)
        .Split(Path.PathSeparator).Append(typeof(Node).Assembly.Location).Distinct(StringComparer.Ordinal)
        .Select(path => MetadataReference.CreateFromFile(path)).ToArray();

    private static void Require(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }

    private static CSharpCompilation Compilation(params string[] sources) =>
        CSharpCompilation.Create("GeneratedScriptProbe", sources.Select((source, index) =>
            CSharpSyntaxTree.ParseText("#nullable enable\nusing Vultra.Scripting;\n" + source, ParseOptions, $"script{index}.cs")),
            References, new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary));

    private static (Compilation Output, GeneratorDriverRunResult Run) Generate(CSharpCompilation compilation)
    {
        GeneratorDriver driver = CSharpGeneratorDriver.Create([new ScriptPropertiesGenerator().AsSourceGenerator()],
            parseOptions: ParseOptions);
        driver = driver.RunGeneratorsAndUpdateCompilation(compilation, out var output, out _);
        return (output, driver.GetRunResult());
    }

    private static byte[] Emit(Compilation compilation)
    {
        using var stream = new MemoryStream();
        var result = compilation.Emit(stream);
        Require(result.Success, string.Join("\n", result.Diagnostics));
        return stream.ToArray();
    }

    private static void Reject(string body, string diagnostic)
    {
        var (_, run) = Generate(Compilation(body));
        Require(run.Diagnostics.Any(item => item.Id == diagnostic && item.Severity == DiagnosticSeverity.Error),
            "Missing compile-time diagnostic " + diagnostic + ": " + string.Join("\n", run.Diagnostics));
    }

    private static readonly string Script = """
        namespace Probe;
        public enum Mode : long { Low = long.MinValue, Idle = 0, Alias = 0, Run = 2, High = long.MaxValue }
        public abstract partial class Base : Node3D
        {
            [Export] private int _counter = 7;
            public int Counter => _counter;
            public override void _Input(InputEvent input) { }
        }
        public partial class Ship : Base
        {
            public override void _UnhandledInput(InputEvent input) { }
            [Export] public float Speed = 2.5f;
            [Export] public short[] Samples = [1, 2];
            [Export] public Mode Mode = Mode.Run;
            [Export] public Mode[] Modes = [Mode.Idle, Mode.Run];
            [Export] public Node3D? Target { get; private set; }
            [Export] public Node3D?[] Peers = [];
            [Export] public Ship? Partner;
            [Export] public Ship?[] Partners = [];
            [Export(ReferenceClass="Foreign", ReferenceModule="foreign.lua")] public Node3D? Foreign;
            [Export] public Material? Material;
            [Export] public System.Numerics.Vector3 Tint = new(1, 2, 3);
            [Export] public System.Numerics.Matrix4x4 Basis = System.Numerics.Matrix4x4.Identity;
            [Export] public string @event = "ready";
            public int Writes;
            private int _gain = 3;
            [Export] public int Gain { get => _gain; set { _gain = value; Writes++; } }
            public ScriptClassDescriptor Describe() => GetScriptClass();
        }
        public partial class Ship { }
        public partial record struct Container
        {
            public sealed partial class Nested : Ship { }
        }
        public sealed partial class Empty : Node { }
        """;

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference CheckBindings(byte[] image)
    {
        var context = new AssemblyLoadContext("generated-bindings", isCollectible: true);
        var assembly = context.LoadFromStream(new MemoryStream(image));
        var type = assembly.GetType("Probe.Ship", throwOnError: true)!;
        var first = Activator.CreateInstance(type)!;
        var second = Activator.CreateInstance(type)!;
        var describe = type.GetMethod("Describe")!;
        var descriptor = (ScriptClassDescriptor)describe.Invoke(first, null)!;
        Require(descriptor.Name == "Probe.Ship" && descriptor.NativeBase == "Node3D" && descriptor.Properties.Count == 15 &&
                descriptor.ScriptTypes.Count == 2 && descriptor.ScriptTypes[0] == type &&
                descriptor.Input && descriptor.UnhandledInput,
            "Generated class identity or inherited export list is wrong");
        var descriptors = descriptor.Properties.Select(property => (Dictionary<string, object?>)
            typeof(ScriptProperty).GetMethod("Describe", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(property,
                [null, type, (Func<Assembly, string>)(assembly => assembly.Location)])!)
            .ToDictionary(value => (string)value["name"]!);
        Require((string)descriptors["Mode"]["kind"]! == "enum" &&
                (string)descriptors["Modes"]["element_kind"]! == "enum" &&
                (int)descriptors["Samples"]["integer_bits"]! == 16,
            "Compile-time enum/array metadata differs from the common schema");
        var reference = JsonSerializer.SerializeToElement(descriptors["Material"]["reference"]);
        Require(reference.GetProperty("type").GetString() == "MaterialResource" &&
                reference.GetProperty("category").GetString() == "resource", "Nullable material reference lost its native type");
        var partner = JsonSerializer.SerializeToElement(descriptors["Partner"]["reference"]);
        var foreign = JsonSerializer.SerializeToElement(descriptors["Foreign"]["reference"]);
        Require(partner.GetProperty("type").GetString() == "Node3D" &&
                partner.GetProperty("class").GetProperty("name").GetString() == "Probe.Ship" &&
                partner.GetProperty("class").GetProperty("module").GetString() == "" &&
                foreign.GetProperty("class").GetProperty("module").GetString() == "foreign.lua",
            "Generated script references lost their separate native base and module/class constraint");
        Require((int)descriptors["_counter"]["default"]! == 7 && type.GetField("Writes")!.GetValue(first) is 0,
            "Default capture invoked a setter or lost a private base field");
        var propertyName = typeof(ScriptProperty).GetProperty("Name", BindingFlags.Instance | BindingFlags.NonPublic)!;
        ScriptProperty Property(string name) => descriptor.Properties.Single(property => (string)propertyName.GetValue(property)! == name);
        var write = typeof(ScriptProperty).GetMethod("Write", BindingFlags.Instance | BindingFlags.NonPublic)!;
        write.Invoke(Property("Gain"), [null, "9"]);
        write.Invoke(Property("_counter"), [null, "12"]);
        Require(type.GetProperty("Gain")!.GetValue(first) is 9 && type.GetField("Writes")!.GetValue(first) is 1 &&
                type.GetProperty("Counter")!.GetValue(first) is 12 && type.GetProperty("Counter")!.GetValue(second) is 7,
            "Generated typed setters bypassed property logic or coupled two script instances");
        var nestedType = assembly.GetType("Probe.Container+Nested", throwOnError: true)!;
        var nested = (ScriptClassDescriptor)describe.Invoke(Activator.CreateInstance(nestedType), null)!;
        Require(nested.Name == "Probe.Container+Nested" && nested.Properties.Count == 15 && nested.ScriptTypes.Count == 3,
            "Nested/inherited script metadata lost CLR identity");
        context.Unload();
        return new WeakReference(context);
    }

    private static void CheckMissingBindings(byte[] generated)
    {
        var context = new AssemblyLoadContext("missing-bindings", isCollectible: true);
        context.LoadFromStream(new MemoryStream(generated));
        var source = Compilation("public class Missing : Node { } public class Child : Probe.Base { }")
            .WithAssemblyName("UngeneratedScriptProbe").AddReferences(MetadataReference.CreateFromImage(generated));
        var assembly = context.LoadFromStream(new MemoryStream(Emit(source)));
        var bind = typeof(Node).GetMethod("BindScriptProperties", BindingFlags.Instance | BindingFlags.NonPublic)!;
        foreach (var name in new[] { "Missing", "Child" })
        {
            try
            {
                bind.Invoke(Activator.CreateInstance(assembly.GetType(name, throwOnError: true)!), null);
            }
            catch (TargetInvocationException error) when (error.InnerException is InvalidOperationException invalid &&
                                                         invalid.Message.Contains("Vultra.Scripting.props", StringComparison.Ordinal))
            {
                continue;
            }
            throw new InvalidOperationException("A missing generator silently reused base script metadata");
        }
        context.Unload();
    }

    private static void Main()
    {
        var compilation = Compilation(Script);
        var (output, run) = Generate(compilation);
        Require(run.Diagnostics.Length == 0 && run.Results.Single().GeneratedSources.Length == 4,
            "Generated duplicate partial descriptors or unexpected diagnostics: " + string.Join("\n", run.Diagnostics));
        var (_, repeat) = Generate(compilation);
        Require(run.Results.Single().GeneratedSources.Select(source => (source.HintName, source.SourceText.ToString()))
            .SequenceEqual(repeat.Results.Single().GeneratedSources.Select(source => (source.HintName, source.SourceText.ToString()))),
            "Generator output is not deterministic");
        var previousCulture = CultureInfo.CurrentCulture;
        CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("fr-FR");
        var (_, localized) = Generate(compilation);
        CultureInfo.CurrentCulture = previousCulture;
        Require(run.Results.Single().GeneratedSources.Select(source => source.SourceText.ToString())
            .SequenceEqual(localized.Results.Single().GeneratedSources.Select(source => source.SourceText.ToString())),
            "Generator numeric declarations depend on the user's culture");
        var image = Emit(output);
        var weak = CheckBindings(image);
        for (var attempt = 0; attempt < 10 && weak.IsAlive; ++attempt)
        {
            GC.Collect();
            GC.WaitForPendingFinalizers();
        }
        Require(!weak.IsAlive, "Generated property delegates or metadata retained a collectible script assembly");
        CheckMissingBindings(image);

        Reject("class Ship : Node { }", "VSG001");
        Reject("class Outer { public partial class Ship : Node { } }", "VSG001");
        Reject("partial class Ship<T> : Node { }", "VSG002");
        Reject("file partial class Ship : Node { }", "VSG002");
        Reject("partial class Ship : Node { [Export] public static float Speed; }", "VSG003");
        Reject("partial class Ship : Node { [Export] public readonly float Speed; }", "VSG003");
        Reject("partial class Ship : Node { [Export] public int Speed { get; init; } }", "VSG003");
        Reject("partial class Ship : Node { [Export] public int Speed => 1; }", "VSG003");
        Reject("partial class Ship : Node { [Export] public object? Value; }", "VSG004");
        Reject("partial class Ship : Node { [Export] public float[,] Values = new float[1,1]; }", "VSG004");
        Reject("partial class Ship : Node { [Export] public float[][] Values = []; }", "VSG004");
        Reject("enum Mode : ulong { Huge = ulong.MaxValue } partial class Ship : Node { [Export] public Mode Mode; }", "VSG005");
        Reject("enum Mode { } partial class Ship : Node { [Export] public Mode Mode; }", "VSG005");
        Reject("partial class Base : Node { [Export] public int Value; } partial class Ship : Base { [Export] public new int Value; }", "VSG006");
        Reject("partial class Ship : Node { [Export(Nullable=false)] public float Value; }", "VSG007");
        Reject("partial class Ship : Node { [Export(ReferenceClass=\"Other\")] public float Value; }", "VSG009");
        Reject("partial class Ship : Node { [Export(ReferenceClass=\"\")] public Node? Value; }", "VSG009");
        Reject("partial class Ship : Node { [Export(ReferenceModule=\"other.lua\")] public Node? Value; }", "VSG009");
        Reject("partial class Ship : Node { [Export(ReferenceClass=\"Other\")] public Ship? Value; }", "VSG009");
        var oldBase = Emit(Compilation("public class OldBase : Node { [Export] public int Value; }"));
        var inherited = Compilation("partial class Ship : OldBase { }").AddReferences(MetadataReference.CreateFromImage(oldBase));
        var (_, badBase) = Generate(inherited);
        Require(badBase.Diagnostics.Any(diagnostic => diagnostic.Id == "VSG008"), "Ungenerated external base silently lost exports");
        var (external, _) = Generate(Compilation("public partial class ExternalScript : Node { }")
            .WithAssemblyName("ExternalScripts"));
        var crossAssembly = Compilation("partial class Ship : Node { [Export] public ExternalScript? Target; }")
            .AddReferences(MetadataReference.CreateFromImage(Emit(external)));
        var (crossAssemblyOutput, crossAssemblyResult) = Generate(crossAssembly);
        Require(!crossAssemblyResult.Diagnostics.Any(diagnostic => diagnostic.Severity == DiagnosticSeverity.Error),
            "A generated external script reference was rejected");
        Emit(crossAssemblyOutput);
        Console.WriteLine("Managed generator passed: typed/private/inherited bindings, script class constraints, values, deterministic metadata, unload and invalid declarations");
    }
}
