using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using Vultra.Scripting;

namespace Vultra.ManagedHost;

internal sealed class ScriptExports
{
    private readonly Dictionary<string, ScriptProperty> _members = new(StringComparer.Ordinal);
    private readonly ScriptScene _scene;
    internal string Description { get; }

    internal ScriptExports(Node script, Func<Assembly, string> origin, IEnumerable<string> dependencies)
    {
        _scene = script.Scene;
        var descriptor = script.BindScriptProperties();
        foreach (var property in descriptor.Properties)
        {
            if (!_members.TryAdd(property.Name, property))
            {
                throw new InvalidOperationException($"Duplicate exported member: {property.Name}");
            }
        }
        var properties = _members.Values.OrderBy(value => value.Name, StringComparer.Ordinal)
            .Select(value => value.Describe(_scene, script.GetType(), origin)).ToArray();
        Description = JsonSerializer.Serialize(new
        {
            version = 1,
            category = "script",
            name = descriptor.Name,
            @base = descriptor.NativeBase,
            classes = descriptor.ScriptTypes.Select(type => ScriptProperty.ClassIdentity(type, script.GetType(), origin)).ToArray(),
            dependencies = dependencies.OrderBy(path => path, StringComparer.Ordinal).ToArray(),
            callbacks = new[] { descriptor.Input ? "input" : null,
                                descriptor.UnhandledInput ? "unhandled_input" : null }
                .Where(value => value != null).ToArray(),
            properties
        });
    }

    private ScriptProperty Find(string name) => _members.TryGetValue(name, out var member) ? member :
        throw new ArgumentException($"Unknown script export: {name}");

    internal string Read(string name) => Find(name).Read(_scene);
    internal void Write(string name, string json) => Find(name).Write(_scene, json);

    internal void RefreshReferences()
    {
        foreach (var member in _members.Values)
        {
            member.RefreshReference(_scene);
        }
    }
}
