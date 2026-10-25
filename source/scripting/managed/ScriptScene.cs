using System;
using System.Collections.Generic;
using System.Numerics;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json;
using Vultra.Interop;

[assembly: InternalsVisibleTo("ControlTest")]
[assembly: InternalsVisibleTo("Vultra.ManagedHost")]

namespace Vultra.Scripting;

public sealed class ScriptScene
{
    private readonly Dictionary<string, byte[]> _names = new();
    private readonly nint _api;
    private VultraSceneFrame _frame;
    private bool _active;
    // Candidate property preparation must see new peers without publishing them to live callbacks.
    private bool _preferPending;
    private readonly Dictionary<ulong, Node> _nodes = new();
    private readonly Dictionary<ulong, Node> _pendingScripts = new();

    internal unsafe string PersistentId(ulong id, string type)
    {
        var key = Encoding.UTF8.GetBytes(type);
        fixed (byte* name = key)
        {
            ulong size = 0;
            Check(((VultraSceneApi*)_api)->ObjectPersistentId(Frame, id, name, (ulong)key.Length, null, 0, &size));
            var bytes = new byte[checked((int)size)];
            fixed (byte* data = bytes)
            {
                Check(((VultraSceneApi*)_api)->ObjectPersistentId(Frame, id, name, (ulong)key.Length, data, size, &size));
            }
            return Encoding.UTF8.GetString(bytes);
        }
    }

    internal unsafe ulong ResolveReferenceId(string persistentId, bool resource)
    {
        var bytes = Encoding.UTF8.GetBytes(persistentId);
        ulong id = 0;
        var api = (VultraSceneApi*)_api;
        var find = resource ? api->FindResource : api->FindNode;
        fixed (byte* data = bytes)
        {
            Check(find(Frame, data, (ulong)bytes.Length, &id));
        }
        if (id == 0)
        {
            throw new ArgumentException("Reference target is not loaded in this world");
        }
        return id;
    }

    internal unsafe ScriptScene(VultraSceneApi* api) => _api = (nint)api;

    private static Type? ResolveType(ReadOnlySpan<byte> type)
    {
        if (type.SequenceEqual("Node"u8))
        {
            return typeof(Node);
        }
        if (type.SequenceEqual("Node3D"u8))
        {
            return typeof(Node3D);
        }
        if (type.SequenceEqual("MeshInstance3D"u8))
        {
            return typeof(MeshInstance3D);
        }
        if (type.SequenceEqual("Camera3D"u8))
        {
            return typeof(Camera3D);
        }
        if (type.SequenceEqual("WorldEnvironment"u8))
        {
            return typeof(WorldEnvironment);
        }
        if (type.SequenceEqual("DirectionalLight3D"u8) || type.SequenceEqual("OmniLight3D"u8) ||
            type.SequenceEqual("SpotLight3D"u8))
        {
            return typeof(Light3D);
        }
        return null;
    }

    private Type ResolveBoundNodeType(ReadOnlySpan<byte> type)
    {
        var name = Encoding.UTF8.GetString(type);
        while (true)
        {
            var bound = ResolveType(Encoding.UTF8.GetBytes(name));
            if (bound != null)
            {
                return bound;
            }
            var description = DescribeType(name);
            name = description.GetProperty("base").GetString() ??
                throw new InvalidOperationException("Node type has no bound native ancestor");
        }
    }

    internal static void ValidateScriptBase(Node script, ReadOnlySpan<byte> type)
    {
        var nativeType = ResolveType(type) ?? throw new InvalidOperationException("Script has no bound native base");
        var engineBase = script.GetType();
        while (engineBase.Assembly != typeof(Node).Assembly)
        {
            engineBase = engineBase.BaseType!;
        }
        if (!engineBase.IsAssignableFrom(nativeType))
        {
            throw new InvalidOperationException($"{script.GetType().Name} cannot attach to {nativeType.Name}");
        }
    }

    internal Node ResolveNode(ulong id) => ResolveNodeIfAlive(id) ?? throw new ArgumentException("Node is not live in this world");

    internal unsafe Node? ResolveNodeIfAlive(ulong id)
    {
        byte* data;
        ulong length;
        var status = ((VultraSceneApi*)_api)->NodeType(Frame, id, &data, &length);
        if (status == VultraStatus.InvalidArgument)
        {
            return null;
        }
        Check(status);
        if (_preferPending && _pendingScripts.TryGetValue(id, out var pending))
        {
            return pending;
        }
        if (_nodes.TryGetValue(id, out var existing))
        {
            return existing;
        }
        var type = ResolveBoundNodeType(new ReadOnlySpan<byte>(data, checked((int)length)));
        var node = (Node)Activator.CreateInstance(type)!;
        node.Attach(this, id);
        _nodes.Add(id, node);
        return node;
    }

    internal T ResolveNode<T>(ulong id)
        where T : Node
    {
        return ResolveNode(id) as T ?? throw new InvalidCastException($"Node is not a {typeof(T).Name}");
    }

    internal void RegisterScript(Node script)
    {
        // Initial instances are registered before callbacks. Reload candidates leave the active identity intact.
        if (!_nodes.TryAdd(script.Id, script))
        {
            _pendingScripts.Add(script.Id, script);
        }
    }

    internal void ActivateScript(Node script)
    {
        _nodes[script.Id] = script;
        if (_pendingScripts.TryGetValue(script.Id, out var pending) && ReferenceEquals(pending, script))
        {
            _pendingScripts.Remove(script.Id);
        }
    }

    internal void RetireScript(Node script)
    {
        if (_nodes.TryGetValue(script.Id, out var current) && ReferenceEquals(current, script))
        {
            _nodes.Remove(script.Id);
            if (_pendingScripts.Remove(script.Id, out var replacement))
            {
                _nodes.Add(script.Id, replacement);
            }
        }
        else if (_pendingScripts.TryGetValue(script.Id, out var pending) && ReferenceEquals(pending, script))
        {
            _pendingScripts.Remove(script.Id);
        }
        script.Detach();
    }

    internal void Begin(VultraSceneFrame frame)
    {
        _frame = frame;
        _active = true;
    }

    internal void End()
    {
        _active = false;
        _frame = default;
    }

    internal readonly ref struct FrameScope
    {
        private readonly ScriptScene _scene;
        private readonly VultraSceneFrame _previous;
        private readonly bool _active;
        private readonly bool _previousPending;

        internal FrameScope(ScriptScene scene, VultraSceneFrame frame, bool preferPending)
        {
            _scene = scene;
            _previous = scene._frame;
            _active = scene._active;
            _previousPending = scene._preferPending;
            scene.Begin(frame);
            scene._preferPending = preferPending;
        }

        public void Dispose()
        {
            _scene._frame = _previous;
            _scene._active = _active;
            _scene._preferPending = _previousPending;
        }
    }

    internal FrameScope BorrowFrame(VultraSceneFrame frame, bool preferPending = false) => new(this, frame, preferPending);

    private VultraSceneFrame Frame =>
        _active ? _frame :
                  throw new InvalidOperationException("Scene access requires a lifecycle/process callback");

    public unsafe bool Paused
    {
        get
        {
            ulong value;
            Check(((VultraSceneApi*)_api)->Paused(Frame, &value));
            return value != 0;
        }
        set => Check(((VultraSceneApi*)_api)->SetPaused(Frame, value ? 1ul : 0ul));
    }

    public unsafe ulong PhysicsFrame
    {
        get
        {
            ulong value;
            Check(((VultraSceneApi*)_api)->PhysicsFrame(Frame, &value));
            return value;
        }
    }

    public unsafe double TimeScale
    {
        get
        {
            double value;
            Check(((VultraSceneApi*)_api)->TimeScale(Frame, &value));
            return value;
        }
        set => Check(((VultraSceneApi*)_api)->SetTimeScale(Frame, value));
    }

    internal unsafe bool InsideTree(ulong id)
    {
        ulong value;
        Check(((VultraSceneApi*)_api)->NodeInsideTree(Frame, id, &value));
        return value != 0;
    }

    internal unsafe bool QueuedForDeletion(ulong id)
    {
        ulong value;
        Check(((VultraSceneApi*)_api)->NodeQueuedForDeletion(Frame, id, &value));
        return value != 0;
    }

    internal unsafe ProcessMode ProcessMode(ulong id)
    {
        ulong value;
        Check(((VultraSceneApi*)_api)->NodeProcessMode(Frame, id, &value));
        return (ProcessMode)value;
    }

    internal unsafe void SetProcessMode(ulong id, ProcessMode value) =>
        Check(((VultraSceneApi*)_api)->SetNodeProcessMode(Frame, id, (ulong)value));

    internal unsafe int ProcessPriority(ulong id)
    {
        int value;
        Check(((VultraSceneApi*)_api)->NodeProcessPriority(Frame, id, &value));
        return value;
    }

    internal unsafe void SetProcessPriority(ulong id, int value) =>
        Check(((VultraSceneApi*)_api)->SetNodeProcessPriority(Frame, id, value));

    internal unsafe void QueueFree(ulong id) => Check(((VultraSceneApi*)_api)->QueueFree(Frame, id));

    public Node Root
    {
        get
        {
            ulong id;
            unsafe
            {
                Check(((VultraSceneApi*)_api)->RootId(Frame, &id));
            }
            return ResolveNode(id);
        }
    }

    internal unsafe Node Child(ulong parent, int index)
    {
        if (index < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(index));
        }
        ulong id;
        Check(((VultraSceneApi*)_api)->ChildId(Frame, parent, (ulong)index, &id));
        return ResolveNode(id);
    }

    internal unsafe Node? Parent(ulong node)
    {
        ulong id;
        Check(((VultraSceneApi*)_api)->NodeParentId(Frame, node, &id));
        return id == 0 ? null : ResolveNode(id);
    }

    internal unsafe Node? Owner(ulong node)
    {
        ulong id;
        Check(((VultraSceneApi*)_api)->NodeOwnerId(Frame, node, &id));
        return id == 0 ? null : ResolveNode(id);
    }

    internal unsafe void SetOwner(ulong node, Node? owner)
    {
        if (owner != null && !ReferenceEquals(this, owner.Scene))
        {
            throw new ArgumentException("Owner belongs to another world");
        }
        Check(((VultraSceneApi*)_api)->SetNodeOwner(Frame, node, owner?.Id ?? 0));
    }

    internal unsafe int ChildCount(ulong node)
    {
        ulong count;
        Check(((VultraSceneApi*)_api)->ChildCount(Frame, node, &count));
        return checked((int)count);
    }

    internal unsafe string Name(ulong node)
    {
        byte* data;
        ulong length;
        Check(((VultraSceneApi*)_api)->NodeName(Frame, node, &data, &length));
        return Encoding.UTF8.GetString(new ReadOnlySpan<byte>(data, checked((int)length)));
    }

    public string[] GetTypeNames()
    {
        unsafe
        {
            ulong count;
            Check(((VultraSceneApi*)_api)->TypeCount(Frame, &count));
            var result = new string[checked((int)count)];
            for (var index = 0; index < result.Length; ++index)
            {
                byte* name;
                ulong length;
                Check(((VultraSceneApi*)_api)->TypeName(Frame, (ulong)index, &name, &length));
                result[index] = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(name, checked((int)length)));
            }
            return result;
        }
    }

    // The returned metadata owns its JSON storage and may outlive this callback/world.
    public JsonElement DescribeType(string type)
    {
        using var document = JsonDocument.Parse(ReadTypeDescriptionJson(type));
        return document.RootElement.Clone();
    }

    private unsafe string ReadTypeDescriptionJson(string type)
    {
        ArgumentException.ThrowIfNullOrEmpty(type);
        var key = Encoding.UTF8.GetBytes(type);
        fixed (byte* name = key)
        {
            ulong size = 0;
            Check(((VultraSceneApi*)_api)->DescribeType(Frame, name, (ulong)key.Length, null, 0, &size));
            var result = new byte[checked((int)size)];
            fixed (byte* data = result)
            {
                Check(((VultraSceneApi*)_api)->DescribeType(Frame, name, (ulong)key.Length, data, size, &size));
                return Encoding.UTF8.GetString(result);
            }
        }
    }

    internal unsafe JsonElement GetPropertyList(ulong node)
    {
        ulong size = 0;
        Check(((VultraSceneApi*)_api)->PropertyList(Frame, node, null, 0, &size));
        var result = new byte[checked((int)size)];
        fixed (byte* data = result)
        {
            Check(((VultraSceneApi*)_api)->PropertyList(Frame, node, data, size, &size));
        }
        using var document = JsonDocument.Parse(result);
        return document.RootElement.Clone();
    }

    internal unsafe string[] GetComponents(ulong node)
    {
        ulong count;
        Check(((VultraSceneApi*)_api)->ComponentCount(Frame, node, &count));
        var result = new string[checked((int)count)];
        for (var index = 0; index < result.Length; ++index)
        {
            byte* name;
            ulong size;
            Check(((VultraSceneApi*)_api)->ComponentName(Frame, node, (ulong)index, &name, &size));
            result[index] = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(name, checked((int)size)));
        }
        return result;
    }

    internal unsafe string ReadComponentPropertyJson(ulong node, string component, string property)
    {
        var type = Encoding.UTF8.GetBytes(component);
        var key = Encoding.UTF8.GetBytes(property);
        fixed (byte* typeName = type)
        fixed (byte* name = key)
        {
            ulong size = 0;
            Check(((VultraSceneApi*)_api)->ReadComponentProperty(Frame, node, typeName, (ulong)type.Length,
                                                               name, (ulong)key.Length, null, 0, &size));
            var result = new byte[checked((int)size)];
            fixed (byte* data = result)
            {
                Check(((VultraSceneApi*)_api)->ReadComponentProperty(Frame, node, typeName, (ulong)type.Length,
                                                                   name, (ulong)key.Length, data, size, &size));
                return Encoding.UTF8.GetString(result);
            }
        }
    }

    internal unsafe void WriteComponentPropertyJson(ulong node, string component, string property, string json)
    {
        var type = Encoding.UTF8.GetBytes(component);
        var key = Encoding.UTF8.GetBytes(property);
        var value = Encoding.UTF8.GetBytes(json);
        fixed (byte* typeName = type)
        fixed (byte* name = key)
        fixed (byte* data = value)
        {
            Check(((VultraSceneApi*)_api)->WriteComponentProperty(Frame, node, typeName, (ulong)type.Length,
                                                                name, (ulong)key.Length, data, (ulong)value.Length));
        }
    }

    internal unsafe string ReadPropertyJson(ulong node, string property)
    {
        var key = Encoding.UTF8.GetBytes(property);
        fixed (byte* name = key)
        {
            ulong size = 0;
            Check(((VultraSceneApi*)_api)->ReadProperty(Frame, node, name, (ulong)key.Length, null, 0, &size));
            var result = new byte[checked((int)size)];
            fixed (byte* data = result)
            {
                Check(((VultraSceneApi*)_api)->ReadProperty(Frame, node, name, (ulong)key.Length, data, size, &size));
                return Encoding.UTF8.GetString(result);
            }
        }
    }

    internal unsafe void WritePropertyJson(ulong node, string property, string json)
    {
        var key = Encoding.UTF8.GetBytes(property);
        var value = Encoding.UTF8.GetBytes(json);
        fixed (byte* name = key)
        fixed (byte* data = value)
        {
            Check(((VultraSceneApi*)_api)->WriteProperty(Frame, node, name, (ulong)key.Length, data, (ulong)value.Length));
        }
    }

    private byte[] EncodedName(string name)
    {
        if (!_names.TryGetValue(name, out var key))
        {
            key = Encoding.UTF8.GetBytes(name);
            _names.Add(name, key);
        }
        return key;
    }

    public uint RandomSeed
    {
        get
        {
            unsafe
            {
                ulong value = 0;
                Check(((VultraSceneApi*)_api)->RandomSeed(Frame, &value));
                return checked((uint)value);
            }
        }
    }

    public uint RandomUInt(string stream, uint? bound = null)
    {
        var key = EncodedName(stream);
        unsafe
        {
            fixed (byte* data = key)
            {
                ulong value = 0;
                var api = (VultraSceneApi*)_api;
                Check(bound.HasValue ? api->RandomBoundedUInt(Frame, data, (ulong)key.Length, bound.Value, &value)
                                     : api->RandomUInt(Frame, data, (ulong)key.Length, &value));
                return checked((uint)value);
            }
        }
    }

    public double RandomUnit(string stream)
    {
        var key = EncodedName(stream);
        unsafe
        {
            fixed (byte* data = key)
            {
                double value = 0;
                Check(((VultraSceneApi*)_api)->RandomUnit(Frame, data, (ulong)key.Length, &value));
                return value;
            }
        }
    }

    public ActionState Action(string name)
    {
        var key = EncodedName(name);
        unsafe
        {
            fixed (byte* data = key)
            {
                ulong value = 0;
                Check(((VultraSceneApi*)_api)->ActionState(Frame, data, (ulong)key.Length, &value));
                return new ActionState((value & 1) != 0, (value & 2) != 0, (value & 4) != 0);
            }
        }
    }

    internal unsafe bool InputEnabled(ulong id, bool unhandled)
    {
        var api = (VultraSceneApi*)_api;
        var function = unhandled ? api->NodeUnhandledInputEnabled : api->NodeInputEnabled;
        ulong value;
        Check(function(Frame, id, &value));
        return value != 0;
    }

    internal unsafe void SetInputEnabled(ulong id, bool enabled, bool unhandled)
    {
        var api = (VultraSceneApi*)_api;
        var function = unhandled ? api->SetNodeUnhandledInputEnabled : api->SetNodeInputEnabled;
        Check(function(Frame, id, enabled ? 1ul : 0ul));
    }

    private unsafe void RequireInputFrame(VultraSceneFrame frame)
    {
        var current = Frame;
        if (current.Context != frame.Context || current.Serial != frame.Serial)
        {
            throw new InvalidOperationException("Input event requires its original callback");
        }
    }

    internal unsafe InputEvent CurrentInput()
    {
        var frame = Frame;
        VultraSceneInputData value;
        Check(((VultraSceneApi*)_api)->InputEvent(frame, &value));
        byte* text;
        ulong size;
        Check(((VultraSceneApi*)_api)->InputText(frame, value.Serial, &text, &size));
        var snapshot = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(text, checked((int)size)));
        return new InputEvent(this, frame, value, snapshot);
    }

    internal unsafe ActionState InputAction(VultraSceneFrame frame, ulong serial, string name)
    {
        RequireInputFrame(frame);
        var key = EncodedName(name);
        fixed (byte* data = key)
        {
            ulong value;
            Check(((VultraSceneApi*)_api)->InputActionState(frame, serial, data, (ulong)key.Length, &value));
            return new ActionState((value & 1) != 0, (value & 2) != 0, (value & 4) != 0);
        }
    }

    internal unsafe void AcceptInput(VultraSceneFrame frame, ulong serial)
    {
        RequireInputFrame(frame);
        Check(((VultraSceneApi*)_api)->SetInputHandled(frame, serial));
    }

    internal unsafe Node CreateChild(ulong parent, string name)
    {
        ArgumentNullException.ThrowIfNull(name);
        var utf8 = Encoding.UTF8.GetBytes(name);
        ulong id;
        fixed (byte* data = utf8)
        {
            Check(((VultraSceneApi*)_api)->CreateNode(Frame, parent, data, (ulong)utf8.Length, &id));
        }
        return ResolveNode(id);
    }

    internal unsafe Node3D CreateChild3D(ulong parent, string name)
    {
        ArgumentNullException.ThrowIfNull(name);
        var utf8 = Encoding.UTF8.GetBytes(name);
        ulong id;
        fixed (byte* data = utf8)
        {
            Check(((VultraSceneApi*)_api)->CreateNode3D(Frame, parent, data, (ulong)utf8.Length, &id));
        }
        return ResolveNode<Node3D>(id);
    }

    internal unsafe MeshInstance3D CreateMeshChild(ulong parent, string name, string assetId)
    {
        ArgumentNullException.ThrowIfNull(name);
        ArgumentNullException.ThrowIfNull(assetId);
        var nameUtf8 = Encoding.UTF8.GetBytes(name);
        var assetUtf8 = Encoding.UTF8.GetBytes(assetId);
        ulong id;
        fixed (byte* nameData = nameUtf8) fixed (byte* assetData = assetUtf8)
        {
            Check(((VultraSceneApi*)_api)
                      ->CreateMesh(Frame,
                                   parent,
                                   nameData,
                                   (ulong)nameUtf8.Length,
                                   assetData,
                                   (ulong)assetUtf8.Length,
                                   &id));
        }
        return ResolveNode<MeshInstance3D>(id);
    }

    internal unsafe void ReparentNode(ulong node, ulong newParent, bool keepGlobal)
    {
        Check(((VultraSceneApi*)_api)->ReparentNode(Frame, node, newParent, keepGlobal ? 1UL : 0UL));
    }

    internal unsafe MeshInstance3D DuplicateMesh(ulong source, ulong parent)
    {
        ulong id;
        Check(((VultraSceneApi*)_api)->DuplicateMesh(Frame, source, parent, &id));
        return ResolveNode<MeshInstance3D>(id);
    }

    internal unsafe void CopyMeshModel(ulong target, ulong source)
    {
        Check(((VultraSceneApi*)_api)->CopyMeshModel(Frame, target, source));
    }

    internal unsafe void SetMeshModel(ulong node, string assetId)
    {
        ArgumentNullException.ThrowIfNull(assetId);
        var utf8 = Encoding.UTF8.GetBytes(assetId);
        fixed (byte* data = utf8)
        {
            Check(((VultraSceneApi*)_api)->SetMeshModel(Frame, node, data, (ulong)utf8.Length));
        }
    }

    internal unsafe void RemoveNode(ulong node)
    {
        Check(((VultraSceneApi*)_api)->RemoveNode(Frame, node));
        if (_nodes.Remove(node, out var removed))
        {
            removed.Detach();
        }
    }

    internal unsafe Camera3D CreateCameraChild(ulong parent, string name)
    {
        ArgumentNullException.ThrowIfNull(name);
        var utf8 = Encoding.UTF8.GetBytes(name);
        ulong id;
        fixed (byte* data = utf8)
        {
            Check(((VultraSceneApi*)_api)->CreateCamera(Frame, parent, data, (ulong)utf8.Length, &id));
        }
        return ResolveNode<Camera3D>(id);
    }

    internal unsafe Light3D CreateLightChild(ulong parent, string name, LightKind kind)
    {
        ArgumentNullException.ThrowIfNull(name);
        var utf8 = Encoding.UTF8.GetBytes(name);
        ulong id;
        fixed (byte* data = utf8)
        {
            Check(((VultraSceneApi*)_api)->CreateLight(Frame, parent, data, (ulong)utf8.Length, (ulong)kind, &id));
        }
        return ResolveNode<Light3D>(id);
    }

    internal unsafe CameraSettings CameraSettings(ulong node)
    {
        VultraCameraSettings value;
        Check(((VultraSceneApi*)_api)->CameraSettings(Frame, node, &value));
        return Scripting.CameraSettings.FromAbi(value);
    }

    internal unsafe void SetCameraSettings(ulong node, CameraSettings value)
    {
        Check(((VultraSceneApi*)_api)->SetCameraSettings(Frame, node, value.ToAbi()));
    }

    internal unsafe void SetCurrentCamera(ulong node) => Check(((VultraSceneApi*)_api)->SetCurrentCamera(Frame, node));

    public void ClearCurrentCamera() => SetCurrentCamera(0);

    public void ClearCurrentEnvironment() => SetCurrentEnvironment(0);

    internal unsafe WorldEnvironment CreateEnvironmentChild(ulong parent, string name)
    {
        ArgumentNullException.ThrowIfNull(name);
        var utf8 = Encoding.UTF8.GetBytes(name);
        ulong id;
        fixed (byte* data = utf8)
        {
            Check(((VultraSceneApi*)_api)->CreateEnvironment(Frame, parent, data, (ulong)utf8.Length, &id));
        }
        return ResolveNode<WorldEnvironment>(id);
    }

    internal unsafe EnvironmentSettings EnvironmentSettings(ulong node)
    {
        VultraEnvironmentSettings value;
        Check(((VultraSceneApi*)_api)->EnvironmentSettings(Frame, node, &value));
        return Scripting.EnvironmentSettings.FromAbi(value);
    }

    internal unsafe void SetEnvironmentSettings(ulong node, EnvironmentSettings value)
    {
        Check(((VultraSceneApi*)_api)->SetEnvironmentSettings(Frame, node, value.ToAbi()));
    }

    internal unsafe void SetEnvironmentAsset(ulong node, string assetId)
    {
        var utf8 = Encoding.UTF8.GetBytes(assetId);
        fixed (byte* data = utf8)
        {
            Check(((VultraSceneApi*)_api)->SetEnvironmentAsset(Frame, node, data, (ulong)utf8.Length));
        }
    }

    internal unsafe void
    SetCurrentEnvironment(ulong node) => Check(((VultraSceneApi*)_api)->SetCurrentEnvironment(Frame, node));

    public unsafe Material CreateMaterial(string name)
    {
        ArgumentNullException.ThrowIfNull(name);
        var utf8 = Encoding.UTF8.GetBytes(name);
        ulong id;
        fixed (byte* data = utf8)
        {
            Check(((VultraSceneApi*)_api)->CreateMaterial(Frame, data, (ulong)utf8.Length, &id));
        }
        return new Material(this, id);
    }

    public unsafe Resource CreateResource(string type, string state = "{}")
    {
        ArgumentException.ThrowIfNullOrEmpty(type);
        ArgumentNullException.ThrowIfNull(state);
        var typeBytes = Encoding.UTF8.GetBytes(type);
        var stateBytes = Encoding.UTF8.GetBytes(state);
        ulong id;
        fixed (byte* name = typeBytes)
        fixed (byte* data = stateBytes)
        {
            Check(((VultraSceneApi*)_api)->CreateResource(Frame, name, (ulong)typeBytes.Length,
                                                        data, (ulong)stateBytes.Length, &id));
        }
        return WrapResource(id);
    }

    public unsafe Resource? FindResource(string assetId)
    {
        ArgumentException.ThrowIfNullOrEmpty(assetId);
        var bytes = Encoding.UTF8.GetBytes(assetId);
        ulong id;
        fixed (byte* data = bytes)
        {
            Check(((VultraSceneApi*)_api)->FindResource(Frame, data, (ulong)bytes.Length, &id));
        }
        return id == 0 ? null : WrapResource(id);
    }

    internal Resource WrapResource(ulong id)
    {
        var name = ObjectType(id);
        while (name != "MaterialResource" && name != "Resource")
        {
            name = DescribeType(name).GetProperty("base").GetString() ??
                throw new InvalidOperationException("Resource type has no bound native ancestor");
        }
        return name == "MaterialResource" ? new Material(this, id) : new Resource(this, id);
    }

    internal unsafe string ObjectType(ulong id)
    {
        byte* data;
        ulong length;
        Check(((VultraSceneApi*)_api)->ObjectType(Frame, id, &data, &length));
        return Encoding.UTF8.GetString(new ReadOnlySpan<byte>(data, checked((int)length)));
    }

    internal unsafe void RemoveResource(ulong id) => Check(((VultraSceneApi*)_api)->RemoveResource(Frame, id));

    internal unsafe Material? MeshMaterial(ulong node, int slot)
    {
        if (slot < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(slot));
        }
        ulong id;
        Check(((VultraSceneApi*)_api)->MeshMaterial(Frame, node, (ulong)slot, &id));
        return id == 0 ? null : new Material(this, id);
    }

    internal unsafe void SetMeshMaterial(ulong node, int slot, Material? material)
    {
        if (slot < 0 || (material != null && material.Scene != this))
        {
            throw new ArgumentException("Material slot must be nonnegative and resource must belong to this scene");
        }
        Check(((VultraSceneApi*)_api)->SetMeshMaterial(Frame, node, (ulong)slot, material?.Id ?? 0));
    }

    internal unsafe string MaterialName(ulong id)
    {
        byte* data;
        ulong length;
        Check(((VultraSceneApi*)_api)->MaterialName(Frame, id, &data, &length));
        return Encoding.UTF8.GetString(new ReadOnlySpan<byte>(data, checked((int)length)));
    }

    internal unsafe MaterialParameters MaterialParameters(ulong id)
    {
        VultraMaterialParameters value;
        Check(((VultraSceneApi*)_api)->MaterialParameters(Frame, id, &value));
        return Scripting.MaterialParameters.FromAbi(value);
    }

    internal unsafe void SetMaterialParameters(ulong id, MaterialParameters value)
    {
        Check(((VultraSceneApi*)_api)->SetMaterialParameters(Frame, id, value.ToAbi()));
    }

    internal unsafe LightKind LightKind(ulong node)
    {
        ulong kind;
        Check(((VultraSceneApi*)_api)->LightKind(Frame, node, &kind));
        return (LightKind)kind;
    }

    internal unsafe LightSettings LightSettings(ulong node)
    {
        VultraLightSettings value;
        Check(((VultraSceneApi*)_api)->LightSettings(Frame, node, &value));
        return Scripting.LightSettings.FromAbi(value);
    }

    internal unsafe void SetLightSettings(ulong node, LightSettings value)
    {
        Check(((VultraSceneApi*)_api)->SetLightSettings(Frame, node, value.ToAbi()));
    }

    internal unsafe Vector3 Position(ulong node)
    {
        VultraSceneTranslation value;
        Check(((VultraSceneApi*)_api)->NodeTranslation(Frame, node, &value));
        return new Vector3(value.X, value.Y, value.Z);
    }

    internal unsafe void SetPosition(ulong node, Vector3 position)
    {
        Check(((VultraSceneApi*)_api)
                  ->SetNodeTranslation(Frame,
                                       node,
                                       new VultraSceneTranslation { X = position.X, Y = position.Y, Z = position.Z }));
    }

    internal static void Check(VultraStatus status)
    {
        if (status != VultraStatus.Ok)
        {
            throw new InvalidOperationException($"Vultra scene API status {status}");
        }
    }
}
