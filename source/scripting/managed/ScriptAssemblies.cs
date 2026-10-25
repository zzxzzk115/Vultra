using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using Vultra.Scripting;

namespace Vultra.ManagedHost;

// A staging batch shares only its candidates during replacement. Live groups stay available for ordinary attachment.
internal sealed class ScriptAssemblyBatch(Func<IEnumerable<ScriptAssemblyGroup>> live, bool replacement)
{
    private readonly List<WeakReference<ScriptAssemblyGroup>> _groups = new();

    private ScriptAssemblyGroup? Find(string path, byte[] hash)
    {
        foreach (var weak in _groups)
        {
            if (weak.TryGetTarget(out var group) && group.Matches(path, hash))
            {
                return group;
            }
        }
        if (!replacement)
        {
            return live().FirstOrDefault(group => group.Matches(path, hash));
        }
        return null;
    }

    internal ScriptAssemblyGroup Acquire(string path)
    {
        var bytes = File.ReadAllBytes(path);
        var hash = SHA256.HashData(bytes);
        var group = Find(path, hash);
        if (group != null)
        {
            group.Retain();
        }
        else
        {
            group = new ScriptAssemblyGroup(path, bytes, hash, Find);
        }
        if (!_groups.Any(weak => weak.TryGetTarget(out var current) && ReferenceEquals(current, group)))
        {
            _groups.Add(new(group));
        }
        return group;
    }
}

// References from script instances and dependent groups own the context. No process-wide assembly/type cache exists.
internal sealed class ScriptAssemblyGroup
{
    private sealed record Image(Assembly Assembly, byte[] Hash, string Path);

    private sealed class LoadContext(ScriptAssemblyGroup owner) : AssemblyLoadContext(isCollectible: true)
    {
        protected override Assembly? Load(AssemblyName name) => owner.Resolve(name);
    }

    private readonly LoadContext _context;
    private readonly AssemblyDependencyResolver _resolver;
    private Func<string, byte[], ScriptAssemblyGroup?>? _shared;
    private readonly Dictionary<string, Image> _images = new(
        OperatingSystem.IsWindows() ? StringComparer.OrdinalIgnoreCase : StringComparer.Ordinal);
    private readonly HashSet<ScriptAssemblyGroup> _dependencies = new();
    private readonly Dictionary<string, byte[]> _manifests = new();
    private int _users = 1;

    internal ScriptAssemblyGroup(string path, byte[] bytes, byte[] hash,
                                  Func<string, byte[], ScriptAssemblyGroup?> shared)
    {
        _shared = shared;
        _context = new(this);
        try
        {
            var manifest = Path.ChangeExtension(path, ".deps.json");
            if (File.Exists(manifest))
            {
                _manifests.Add(manifest, SHA256.HashData(File.ReadAllBytes(manifest)));
            }
            _resolver = new(path);
            var root = Load(path, bytes, hash);
            // Resolve IL references before publishing metadata, including helper-only dependencies with no exports.
            var pending = new Queue<Assembly>();
            var visited = new HashSet<Assembly>();
            pending.Enqueue(root);
            while (pending.TryDequeue(out var assembly))
            {
                if (!visited.Add(assembly))
                {
                    continue;
                }
                foreach (var reference in assembly.GetReferencedAssemblies())
                {
                    var dependency = _context.LoadFromAssemblyName(reference);
                    if (AssemblyLoadContext.GetLoadContext(dependency) == _context)
                    {
                        pending.Enqueue(dependency);
                    }
                }
            }
        }
        catch
        {
            Release();
            throw;
        }
    }

    internal IEnumerable<string> Paths => _images.Keys.Concat(_manifests.Keys)
        .Concat(_dependencies.SelectMany(group => group.Paths))
        .Distinct(OperatingSystem.IsWindows() ? StringComparer.OrdinalIgnoreCase : StringComparer.Ordinal);

    internal Assembly GetAssembly(string path)
    {
        return FindImage(path)?.Assembly ??
            throw new InvalidOperationException($"Assembly is outside this staged group: {path}");
    }

    private Image? FindImage(string path)
    {
        if (_images.TryGetValue(path, out var image))
        {
            return image;
        }
        foreach (var dependency in _dependencies)
        {
            if (dependency.FindImage(path) is { } inherited)
            {
                return inherited;
            }
        }
        return null;
    }

    internal string Origin(Assembly assembly)
    {
        var image = _images.Values.FirstOrDefault(image => image.Assembly == assembly);
        if (image != null)
        {
            return image.Path;
        }
        foreach (var dependency in _dependencies)
        {
            if (dependency.Contains(assembly))
            {
                return dependency.Origin(assembly);
            }
        }
        throw new InvalidOperationException($"Script type has no original assembly path: {assembly.FullName}");
    }

    private bool Contains(Assembly assembly) => _images.Values.Any(image => image.Assembly == assembly) ||
        _dependencies.Any(group => group.Contains(assembly));

    internal bool Matches(string path, byte[] hash) => _users != 0 && FindImage(path) is { } image &&
        image.Hash.AsSpan().SequenceEqual(hash);

    private bool DependsOn(ScriptAssemblyGroup group) => ReferenceEquals(this, group) ||
        _dependencies.Any(dependency => dependency.DependsOn(group));

    internal void VerifyFiles()
    {
        foreach (var image in _images.Values)
        {
            if (!SHA256.HashData(File.ReadAllBytes(image.Path)).AsSpan().SequenceEqual(image.Hash))
            {
                throw new IOException($"Assembly changed during staging: {image.Path}");
            }
        }
        foreach (var (path, hash) in _manifests)
        {
            if (!SHA256.HashData(File.ReadAllBytes(path)).AsSpan().SequenceEqual(hash))
            {
                throw new IOException($"Assembly dependency manifest changed during staging: {path}");
            }
        }
        foreach (var dependency in _dependencies)
        {
            dependency.VerifyFiles();
        }
    }

    internal void Retain()
    {
        if (_users == 0)
        {
            throw new InvalidOperationException("Retain a retired assembly group");
        }
        ++_users;
    }

    internal void Release()
    {
        if (--_users != 0)
        {
            return;
        }
        _images.Clear();
        _manifests.Clear();
        _shared = null;
        _context.Unload();
        foreach (var dependency in _dependencies)
        {
            dependency.Release();
        }
        _dependencies.Clear();
    }

    private Assembly Load(string path, byte[] bytes, byte[] hash)
    {
        using var stream = new MemoryStream(bytes);
        var assembly = _context.LoadFromStream(stream);
        _images.Add(path, new(assembly, hash, path));
        return assembly;
    }

    private Assembly? Resolve(AssemblyName name)
    {
        if (name.Name == typeof(Node).Assembly.GetName().Name)
        {
            return typeof(Node).Assembly;
        }
        var resolved = _resolver.ResolveAssemblyToPath(name);
        if (resolved == null)
        {
            return null;
        }
        resolved = Path.GetFullPath(resolved);
        if (FindImage(resolved) is { } existing)
        {
            return existing.Assembly;
        }
        var bytes = File.ReadAllBytes(resolved);
        var hash = SHA256.HashData(bytes);
        var shared = _shared!(resolved, hash);
        if (shared != null)
        {
            if (shared.DependsOn(this))
            {
                throw new InvalidOperationException("Cyclic managed loading across active groups; stage the sources together");
            }
            if (_dependencies.Add(shared))
            {
                shared.Retain();
            }
            var assembly = shared.GetAssembly(resolved);
            _images.Add(resolved, new(assembly, hash, resolved));
            return assembly;
        }
        return Load(resolved, bytes, hash);
    }
}
