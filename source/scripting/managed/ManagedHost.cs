using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Vultra.Interop;
using Vultra.Scripting;

namespace Vultra.ManagedHost;

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ManagedLoadRequest
{
    public byte* Path;
    public ulong PathSize;
    public byte* TypeName;
    public ulong TypeNameSize;
    public ulong NodeId;
    public byte* NodeType;
    public ulong NodeTypeSize;
    public void* Peer;
    public void* Previous;
    public void* BatchPeer;
    public void* Instance;
}

public static unsafe class Entry
{
    // Immutable callbacks belong to the non-collectible host, never to a user's script assembly.
    private static readonly nint PropertyApi = CreatePropertyApi();

    private static nint CreatePropertyApi()
    {
        var api = (VultraScriptPropertyApi*)RuntimeHelpers.AllocateTypeAssociatedMemory(typeof(Entry), sizeof(VultraScriptPropertyApi));
        *api = new VultraScriptPropertyApi
        {
            Version = VultraAbi.Version,
            StructSize = (uint)sizeof(VultraScriptPropertyApi),
            Describe = &DescribeExports,
            Read = &ReadExport,
            Write = &WriteExport,
            Prepare = &PrepareExports
        };
        return (nint)api;
    }

    private static ScriptExports Exports(void* state) =>
        ((State)GCHandle.FromIntPtr((nint)state).Target!).Exports!;

    private static string Utf8(byte* data, ulong size)
    {
        if ((data == null && size != 0) || size > int.MaxValue)
        {
            throw new ArgumentException("Invalid UTF-8 script property argument");
        }
        return Encoding.UTF8.GetString(new ReadOnlySpan<byte>(data, (int)size));
    }

    private static VultraStatus CopyText(string text, byte* data, ulong capacity, ulong* size)
    {
        if (size == null || (data == null && capacity != 0))
        {
            return VultraStatus.InvalidArgument;
        }
        var bytes = Encoding.UTF8.GetBytes(text);
        *size = (ulong)bytes.Length;
        if (data == null)
        {
            return VultraStatus.Ok;
        }
        if (capacity < *size)
        {
            return VultraStatus.BufferTooSmall;
        }
        bytes.AsSpan().CopyTo(new Span<byte>(data, bytes.Length));
        return VultraStatus.Ok;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static VultraStatus DescribeExports(void* state, byte* data, ulong capacity, ulong* size)
    {
        if (state == null)
        {
            return VultraStatus.InvalidArgument;
        }
        try
        {
            return CopyText(Exports(state).Description, data, capacity, size);
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"Describe C# exports: {error}");
            return VultraStatus.Error;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static VultraStatus ReadExport(void* state, VultraSceneFrame frame, byte* name, ulong nameSize, byte* data, ulong capacity, ulong* size)
    {
        if (state == null)
        {
            return VultraStatus.InvalidArgument;
        }
        try
        {
            var instance = (State)GCHandle.FromIntPtr((nint)state).Target!;
            using var access = instance.Scene!.BorrowFrame(frame, !instance.Committed);
            return CopyText(Exports(state).Read(Utf8(name, nameSize)), data, capacity, size);
        }
        catch (ArgumentException error)
        {
            Console.Error.WriteLine($"Read C# export: {error}");
            return VultraStatus.InvalidArgument;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"Read C# export: {error}");
            return VultraStatus.Error;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static VultraStatus WriteExport(void* state, VultraSceneFrame frame, byte* name, ulong nameSize, byte* json, ulong jsonSize)
    {
        if (state == null)
        {
            return VultraStatus.InvalidArgument;
        }
        try
        {
            var instance = (State)GCHandle.FromIntPtr((nint)state).Target!;
            using var access = instance.Scene!.BorrowFrame(frame, !instance.Committed);
            Exports(state).Write(Utf8(name, nameSize), Utf8(json, jsonSize));
            return VultraStatus.Ok;
        }
        catch (ArgumentException error)
        {
            Console.Error.WriteLine($"Write C# export: {error}");
            return VultraStatus.InvalidArgument;
        }
        catch (JsonException error)
        {
            Console.Error.WriteLine($"Write C# export: {error}");
            return VultraStatus.InvalidArgument;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"Write C# export: {error}");
            return VultraStatus.Error;
        }
    }

    private sealed class World(ScriptScene scene)
    {
        public readonly ScriptScene Scene = scene;
        public readonly List<State> States = new();
        public bool ReferencesDirty;
    }

    private sealed class State
    {
        public World? World;
        public ScriptAssemblyGroup? Module;
        public ScriptAssemblyBatch? Batch;
        public Node? Script;
        public ScriptExports? Exports;
        public ScriptScene? Scene;
        public VultraUiApi* Ui;
        public bool Committed;
        public Dictionary<string, NodeReloadValue>? PendingReferences;
    }

    private static void Activate(State state)
    {
        var world = state.World!;
        if (!world.ReferencesDirty)
        {
            return;
        }
        // Publish all committed identities before resolving fields. This also handles forward references at startup.
        foreach (var consumer in world.States)
        {
            if (world.Scene.ResolveNodeIfAlive(consumer.Script!.Id) != null)
            {
                consumer.Committed = true;
                world.Scene.ActivateScript(consumer.Script);
            }
        }
        foreach (var consumer in world.States)
        {
            if (world.Scene.ResolveNodeIfAlive(consumer.Script!.Id) == null)
            {
                continue;
            }
            foreach (var field in InstanceFields(consumer.Script!.GetType()))
            {
                if (field.FieldType.IsArray && field.FieldType.GetArrayRank() != 1)
                {
                    continue;
                }
                var scalar = field.FieldType.IsArray ? field.FieldType.GetElementType()! : field.FieldType;
                if (!typeof(Node).IsAssignableFrom(scalar))
                {
                    continue;
                }
                var value = PropertyValueCodec.RefreshNode(world.Scene, field.FieldType,
                                                            field.GetValue(consumer.Script), out var changed);
                if (changed)
                {
                    field.SetValue(consumer.Script, value);
                }
            }
            consumer.Exports?.RefreshReferences();
        }
        world.ReferencesDirty = false;
    }

    private static IEnumerable<FieldInfo> InstanceFields(Type type)
    {
        for (Type? current = type; current != null && current.Assembly != typeof(Node).Assembly; current = current.BaseType)
        {
            foreach (var field in current.GetFields(BindingFlags.Instance | BindingFlags.Public |
                                                    BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
            {
                if (!field.IsInitOnly)
                {
                    yield return field;
                }
            }
        }
    }

    private static IEnumerable<FieldInfo> ReloadFields(Type type) => InstanceFields(type).Where(field =>
        !field.IsDefined(typeof(NonSerializedAttribute), false) && !field.IsDefined(typeof(ExportAttribute), false));

    private static string FieldKey(FieldInfo field) => $"{field.DeclaringType!.FullName}.{field.Name}";

    private readonly record struct ReloadValue(string? Json, object? Reference);
    private sealed record NodeReloadValue(ulong[]? Ids, bool Array);

    private static Dictionary<string, ReloadValue> SaveFields(Node script)
    {
        var values = new Dictionary<string, ReloadValue>();
        // Local options cannot retain types from unloaded script assemblies in a global cache.
        var options = new JsonSerializerOptions { IncludeFields = true };
        foreach (var field in ReloadFields(script.GetType()))
        {
            try
            {
                var value = field.GetValue(script);
                var scalar = field.FieldType.IsArray ? field.FieldType.GetElementType()! : field.FieldType;
                if (typeof(Node).IsAssignableFrom(scalar))
                {
                    ulong Identity(object? item)
                    {
                        if (item is not Node node || node.IsDetached)
                        {
                            return 0;
                        }
                        if (!ReferenceEquals(node.Scene, script.Scene))
                        {
                            throw new ArgumentException("Reload reference belongs to another script context");
                        }
                        return node.Id;
                    }
                    var ids = value == null ? null : field.FieldType.IsArray ?
                        ((Array)value).Cast<object?>().Select(Identity).ToArray() : [Identity(value)];
                    values.Add(FieldKey(field), new ReloadValue(null, new NodeReloadValue(ids, field.FieldType.IsArray)));
                }
                else if (value != null && PropertyValueCodec.IsReference(scalar))
                {
                    values.Add(FieldKey(field), new ReloadValue(null, value is Array array ? array.Clone() : value));
                }
                else
                {
                    values.Add(FieldKey(field), new ReloadValue(
                        JsonSerializer.Serialize(value, field.FieldType, options), null));
                }
            }
            catch (Exception error)
            {
                throw new InvalidOperationException($"Save reload field {FieldKey(field)}", error);
            }
        }
        return values;
    }

    private static Dictionary<string, NodeReloadValue> RestoreFields(Node script, Dictionary<string, ReloadValue> values)
    {
        var options = new JsonSerializerOptions { IncludeFields = true };
        var pending = new Dictionary<string, NodeReloadValue>();
        foreach (var field in ReloadFields(script.GetType()))
        {
            if (!values.TryGetValue(FieldKey(field), out var saved))
            {
                continue;
            }
            try
            {
                if (saved.Reference is NodeReloadValue reference)
                {
                    var scalar = field.FieldType.IsArray ? field.FieldType.GetElementType()! : field.FieldType;
                    if (!typeof(Node).IsAssignableFrom(scalar) || field.FieldType.IsArray != reference.Array)
                    {
                        throw new InvalidCastException("Reload changed the node reference shape");
                    }
                    pending.Add(FieldKey(field), reference);
                    continue;
                }
                if (saved.Reference != null && !field.FieldType.IsInstanceOfType(saved.Reference))
                {
                    throw new InvalidCastException("Reload changed the native reference type");
                }
                var value = saved.Reference ?? JsonSerializer.Deserialize(saved.Json!, field.FieldType, options);
                field.SetValue(script, value);
            }
            catch (Exception error)
            {
                throw new InvalidOperationException($"Restore reload field {FieldKey(field)}", error);
            }
        }
        return pending;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static VultraStatus PrepareExports(void* state, VultraSceneFrame frame)
    {
        if (state == null)
        {
            return VultraStatus.InvalidArgument;
        }
        try
        {
            var instance = (State)GCHandle.FromIntPtr((nint)state).Target!;
            using var access = instance.Scene!.BorrowFrame(frame, !instance.Committed);
            instance.Module!.VerifyFiles();
            if (instance.PendingReferences == null)
            {
                return VultraStatus.Ok;
            }
            foreach (var field in ReloadFields(instance.Script!.GetType()))
            {
                if (!instance.PendingReferences.TryGetValue(FieldKey(field), out var reference))
                {
                    continue;
                }
                var scalar = field.FieldType.IsArray ? field.FieldType.GetElementType()! : field.FieldType;
                object? Resolve(ulong id)
                {
                    var target = id == 0 ? null : instance.Scene.ResolveNodeIfAlive(id);
                    if (target != null && !scalar.IsInstanceOfType(target))
                    {
                        throw new InvalidCastException($"Reload reference is not a {scalar.FullName}");
                    }
                    return target;
                }
                object? value = null;
                if (reference.Ids != null)
                {
                    if (reference.Array)
                    {
                        var array = Array.CreateInstance(scalar, reference.Ids.Length);
                        for (var index = 0; index < reference.Ids.Length; ++index)
                        {
                            array.SetValue(Resolve(reference.Ids[index]), index);
                        }
                        value = array;
                    }
                    else
                    {
                        value = Resolve(reference.Ids[0]);
                    }
                }
                field.SetValue(instance.Script, value);
            }
            instance.PendingReferences = null;
            return VultraStatus.Ok;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"Prepare C# references: {error}");
            return VultraStatus.Error;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int Initialize(void* hostData, void* pluginData)
    {
        var host = (VultraHostApi*)hostData;
        var plugin = (VultraPluginApi*)pluginData;
        if (host == null || plugin == null || plugin->UserData == null || host->Version != VultraAbi.Version ||
            host->StructSize != sizeof(VultraHostApi) ||
            (host->Ui != null && (host->Ui->Version != VultraAbi.Version || host->Ui->StructSize != sizeof(VultraUiApi) ||
                                 host->Ui->Fingerprint != VultraAbi.UiFingerprint)) ||
            host->Scene == null ||
            host->Scene->Version != VultraAbi.Version || host->Scene->StructSize != sizeof(VultraSceneApi) ||
            host->Scene->Fingerprint != VultraAbi.SceneFingerprint ||
            plugin->Version != VultraAbi.Version || plugin->StructSize != sizeof(VultraPluginApi))
        {
            return (int)VultraStatus.InvalidArgument;
        }
        var request = (ManagedLoadRequest*)plugin->UserData;
        if (request->Path == null || request->PathSize == 0 || request->PathSize > int.MaxValue ||
            request->TypeName == null || request->TypeNameSize == 0 || request->TypeNameSize > int.MaxValue ||
            request->NodeId == 0 || request->NodeType == null || request->NodeTypeSize == 0 ||
            request->NodeTypeSize > int.MaxValue)
        {
            return (int)VultraStatus.InvalidArgument;
        }
        ScriptAssemblyGroup? stagedGroup = null;
        try
        {
            var path = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(request->Path, (int)request->PathSize));
            var typeName =
                Encoding.UTF8.GetString(new ReadOnlySpan<byte>(request->TypeName, (int)request->TypeNameSize));
            var shared = request->Previous != null ? request->Previous : request->Peer;
            var world = shared == null ? new World(new ScriptScene(host->Scene)) :
                                          ((State)GCHandle.FromIntPtr((nint)shared).Target!).World!;
            path = Path.GetFullPath(path);
            var batch = request->BatchPeer == null ?
                new ScriptAssemblyBatch(() => world.States.Select(state => state.Module!).Distinct(),
                                        request->Previous != null) :
                ((State)GCHandle.FromIntPtr((nint)request->BatchPeer).Target!).Batch!;
            stagedGroup = batch.Acquire(path);
            var module = stagedGroup;
            var type = module.GetAssembly(path).GetType(typeName, throwOnError: true)!;
            if (!typeof(Node).IsAssignableFrom(type) || type.IsAbstract)
            {
                throw new InvalidOperationException($"{typeName} must derive from Node");
            }
            var scene = world.Scene;
            var script = (Node)Activator.CreateInstance(type)!;
            ScriptScene.ValidateScriptBase(script,
                                           new ReadOnlySpan<byte>(request->NodeType, (int)request->NodeTypeSize));
            script.Attach(scene, request->NodeId);
            var exports = new ScriptExports(script, module.Origin, module.Paths);
            var previous = request->Previous == null ? null :
                (State)GCHandle.FromIntPtr((nint)request->Previous).Target!;
            Dictionary<string, NodeReloadValue>? references = null;
            if (previous != null)
            {
                references = RestoreFields(script, SaveFields(previous.Script!));
            }
            var state = new State
            {
                World = world,
                Module = module,
                Batch = batch,
                Script = script,
                Exports = exports,
                Scene = scene,
                Ui = host->Ui,
                PendingReferences = references
            };
            plugin->UserData = (void*)GCHandle.ToIntPtr(GCHandle.Alloc(state));
            plugin->Update = &Update;
            plugin->OnGui = &OnGui;
            plugin->Stop = &Stop;
            plugin->OnScene = &OnScene;
            plugin->Properties = (VultraScriptPropertyApi*)PropertyApi;
            request->Instance = plugin->UserData;
            scene.RegisterScript(script);
            world.States.Add(state);
            world.ReferencesDirty = true;
            stagedGroup = null;
            return (int)VultraStatus.Ok;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"Load C# script: {error}");
            stagedGroup?.Release();
            return (int)VultraStatus.Error;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

    private static VultraStatus Update(void* userData, VultraSceneFrame frame, float deltaSeconds)
    {
        try
        {
            var state = (State)GCHandle.FromIntPtr((nint)userData).Target!;
            state.Scene!.Begin(frame);
            try
            {
                Activate(state);
                state.Script!._Process(deltaSeconds);
            }
            finally
            {
                state.Scene.End();
            }
            return VultraStatus.Ok;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"C# script update: {error}");
            return VultraStatus.Error;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

    private static VultraStatus OnGui(void* userData, VultraUiFrame frame)
    {
        try
        {
            var state = (State)GCHandle.FromIntPtr((nint)userData).Target!;
            if (state.Ui == null)
            {
                return VultraStatus.InvalidArgument;
            }
            state.Script!._EditorGui(new EditorGui(state.Ui, frame));
            return VultraStatus.Ok;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"C# script UI: {error}");
            return VultraStatus.Error;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static VultraStatus OnScene(void* userData, VultraSceneFrame frame, uint sceneEvent, float deltaSeconds)
    {
        try
        {
            var state = (State)GCHandle.FromIntPtr((nint)userData).Target!;
            state.Scene!.Begin(frame);
            try
            {
                Activate(state);
                switch (sceneEvent)
                {
                    case 0:
                        state.Script!._EnterTree();
                        break;
                    case 1:
                        state.Script!._Ready();
                        break;
                    case 2:
                        state.Script!._PhysicsProcess(deltaSeconds);
                        break;
                    case 3:
                        state.Script!._ExitTree();
                        break;
                    case 4:
                        break;
                    case 6:
                        state.Script!._Input(state.Scene.CurrentInput());
                        break;
                    case 7:
                        state.Script!._UnhandledInput(state.Scene.CurrentInput());
                        break;
                    default:
                        return VultraStatus.InvalidArgument;
                }
            }
            finally
            {
                state.Scene.End();
            }
            return VultraStatus.Ok;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"C# script scene callback: {error}");
            return VultraStatus.Error;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]

    private static VultraStatus Stop(void* userData)
    {
        var handle = GCHandle.FromIntPtr((nint)userData);
        var state = (State)handle.Target!;
        var status = VultraStatus.Ok;
        try
        {
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"C# script stop: {error}");
            status = VultraStatus.Error;
        }
        finally
        {
            if (state.World != null)
            {
                state.World.States.Remove(state);
                state.World.ReferencesDirty = true;
            }
            state.Scene?.End();
            if (state.Script != null)
            {
                state.Scene?.RetireScript(state.Script);
            }
            state.Exports = null;
            state.Script = null;
            state.Scene = null;
            try
            {
                state.Module!.Release();
            }
            catch (Exception error)
            {
                Console.Error.WriteLine($"Unload C# module: {error}");
                status = VultraStatus.Error;
            }
            finally
            {
                state.Module = null;
                state.Batch = null;
                state.World = null;
                handle.Free();
            }
        }
        return status;
    }
}
