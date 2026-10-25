using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;

namespace Vultra.Scripting;

// Constructed inside each generated partial script, so private members need no reflection or global registry.
public sealed record ScriptClassDescriptor(string Name, string NativeBase, IReadOnlyList<ScriptProperty> Properties,
                                           IReadOnlyList<Type> ScriptTypes, bool Input, bool UnhandledInput);

public readonly record struct ScriptEnumChoice(string Label, long Value);

public sealed class ScriptProperty
{
    private readonly Type _type;
    private readonly string _kind;
    private readonly int _integerBits;
    private readonly string? _elementKind;
    private readonly string? _referenceType;
    private readonly string? _referenceClass;
    private readonly string? _referenceModule;
    private readonly bool _resource;
    private readonly bool _nullable;
    private readonly ScriptEnumChoice[] _choices;
    private readonly Func<object?> _read;
    private readonly Action<object?> _write;

    internal string Name { get; }

    private ScriptProperty(string name, Type type, string kind, Func<object?> read, Action<object?> write,
                           int integerBits, string? elementKind, string? referenceType, bool resource,
                           bool nullable, ScriptEnumChoice[] choices, string? referenceClass, string? referenceModule)
    {
        Name = name;
        _type = type;
        _kind = kind;
        _integerBits = integerBits;
        _elementKind = elementKind;
        _referenceType = referenceType;
        _referenceClass = referenceClass;
        _referenceModule = referenceModule;
        _resource = resource;
        _nullable = nullable;
        _choices = choices;
        _read = read;
        _write = write;
    }

    public static ScriptProperty Bind<T>(string name, string kind, Func<T> read, Action<T> write,
                                          int integerBits = 0, string? elementKind = null,
                                          string? referenceType = null, bool resource = false, bool nullable = true,
                                          ScriptEnumChoice[]? choices = null, string? referenceClass = null,
                                          string? referenceModule = null) =>
        new(name, typeof(T), kind, () => read(), value => write((T)value!), integerBits, elementKind,
            referenceType, resource, nullable, choices ?? [], referenceClass, referenceModule);

    internal static object ClassIdentity(Type type, Type owner, Func<Assembly, string> origin)
    {
        var module = type.Assembly == owner.Assembly ? "" : origin(type.Assembly);
        if (type.Assembly != owner.Assembly && module.Length == 0)
        {
            throw new InvalidOperationException("Referenced script assembly has no module origin");
        }
        return new { module, name = type.FullName! };
    }

    internal object Describe(ScriptScene scene, Type owner, Func<Assembly, string> origin)
    {
        var result = new Dictionary<string, object?>
        {
            ["name"] = Name,
            ["label"] = Name,
            ["drawer"] = null,
            ["kind"] = _kind,
            ["json_path"] = "/" + Name.Replace("~", "~0").Replace("/", "~1"),
            ["flags"] = new[] { "serialize", "inspect", "bind" },
            ["default"] = PropertyValueCodec.ToWire(scene, _type, _read()),
            ["widget"] = "drag",
            ["speed"] = 0.1,
            ["integer_bits"] = _integerBits,
            ["choices"] = _choices.Select(choice => new { label = choice.Label, value = choice.Value }).ToArray(),
            ["enum_values"] = _choices.Select(choice => choice.Value).Distinct().ToArray()
        };
        if (_elementKind != null)
        {
            result["element_kind"] = _elementKind;
        }
        if (_referenceType != null)
        {
            var reference = new Dictionary<string, object?>
            {
                ["category"] = _resource ? "resource" : "node",
                ["type"] = _referenceType,
                ["nullable"] = _nullable
            };
            if (_referenceClass != null)
            {
                var scalar = _type.IsArray ? _type.GetElementType()! : _type;
                reference["class"] = scalar.Assembly == typeof(Node).Assembly ?
                    new { module = _referenceModule ?? "", name = _referenceClass } : ClassIdentity(scalar, owner, origin);
            }
            result["reference"] = reference;
        }
        return result;
    }

    internal string Read(ScriptScene scene) => PropertyValueCodec.Serialize(scene, _type, _read());
    internal void Write(ScriptScene scene, string json) => _write(PropertyValueCodec.Deserialize(scene, _type, json));

    internal void RefreshReference(ScriptScene scene)
    {
        if (_referenceType == null || _resource)
        {
            return;
        }
        var value = PropertyValueCodec.RefreshNode(scene, _type, _read(), out var changed);
        if (!_nullable && (value == null || value is Array array && array.Cast<object?>().Any(item => item == null)))
        {
            throw new InvalidOperationException($"Required script reference target was removed: {Name}");
        }
        if (changed)
        {
            _write(value);
        }
    }
}
