using System;
using System.Numerics;
using System.Text.Json;

namespace Vultra.Scripting;

// The wire representation is the core PropertySchema value format, including persistent UUID references.
internal static class PropertyValueCodec
{
    internal static bool IsReference(Type type) => typeof(Node).IsAssignableFrom(type) || typeof(Resource).IsAssignableFrom(type);

    internal static string ReferenceType(Type type)
    {
        while (type.Assembly != typeof(Node).Assembly)
        {
            type = type.BaseType!;
        }
        return type == typeof(Material) ? "MaterialResource" : type.Name;
    }

    internal static string Serialize(ScriptScene scene, Type type, object? value) =>
        JsonSerializer.Serialize(ToWire(scene, type, value));

    internal static object? ToWire(ScriptScene scene, Type type, object? value)
    {
        if (type.IsArray)
        {
            if (value is not Array array || array.Rank != 1)
            {
                throw new ArgumentException("Property requires a non-null one-dimensional array");
            }
            var element = type.GetElementType()!;
            var result = new object?[array.Length];
            for (var index = 0; index < result.Length; ++index)
            {
                result[index] = ToWire(scene, element, array.GetValue(index));
            }
            return result;
        }
        if (typeof(Node).IsAssignableFrom(type))
        {
            if (value == null)
            {
                return null;
            }
            var node = (Node)value;
            if (!ReferenceEquals(node.Scene, scene))
            {
                throw new ArgumentException("Reference belongs to another world");
            }
            return scene.PersistentId(node.Id, ReferenceType(type));
        }
        if (typeof(Resource).IsAssignableFrom(type))
        {
            if (value == null)
            {
                return null;
            }
            var resource = (Resource)value;
            if (!ReferenceEquals(resource.Scene, scene))
            {
                throw new ArgumentException("Reference belongs to another world");
            }
            return scene.PersistentId(resource.Id, ReferenceType(type));
        }
        if (type.IsEnum)
        {
            return Convert.ToInt64(value);
        }
        if (value is Vector2 v2)
        {
            return new[] { v2.X, v2.Y };
        }
        if (value is Vector3 v3)
        {
            return new[] { v3.X, v3.Y, v3.Z };
        }
        if (value is Vector4 v4)
        {
            return new[] { v4.X, v4.Y, v4.Z, v4.W };
        }
        if (value is Matrix4x4 m)
        {
            // System.Numerics uses row vectors; row-major fields match the core's column-vector/column-major wire data.
            return new[] { m.M11, m.M12, m.M13, m.M14, m.M21, m.M22, m.M23, m.M24,
                           m.M31, m.M32, m.M33, m.M34, m.M41, m.M42, m.M43, m.M44 };
        }
        return value;
    }

    internal static object? Deserialize(ScriptScene scene, Type type, string json)
    {
        using var document = JsonDocument.Parse(json);
        return FromWire(scene, type, document.RootElement);
    }

    private static object? FromWire(ScriptScene scene, Type type, JsonElement value)
    {
        if (type.IsArray && type.GetArrayRank() == 1)
        {
            var element = type.GetElementType()!;
            var result = Array.CreateInstance(element, value.GetArrayLength());
            for (var index = 0; index < result.Length; ++index)
            {
                result.SetValue(FromWire(scene, element, value[index]), index);
            }
            return result;
        }
        if (IsReference(type))
        {
            if (value.ValueKind == JsonValueKind.Null)
            {
                return null;
            }
            var resource = typeof(Resource).IsAssignableFrom(type);
            var id = scene.ResolveReferenceId(value.GetString()!, resource);
            object result = resource ? scene.WrapResource(id) : scene.ResolveNode(id);
            if (!type.IsInstanceOfType(result))
            {
                throw new InvalidCastException($"Reference is not a {type.Name}");
            }
            return result;
        }
        if (type.IsEnum)
        {
            var result = Enum.ToObject(type, value.GetInt64());
            if (!Enum.IsDefined(type, result))
            {
                throw new ArgumentException($"Unknown {type.Name} enum value");
            }
            return result;
        }
        if (type == typeof(Vector2))
        {
            return new Vector2(Component(value, 0, 2), Component(value, 1, 2));
        }
        if (type == typeof(Vector3))
        {
            return new Vector3(Component(value, 0, 3), Component(value, 1, 3), Component(value, 2, 3));
        }
        if (type == typeof(Vector4))
        {
            return new Vector4(Component(value, 0, 4), Component(value, 1, 4), Component(value, 2, 4), Component(value, 3, 4));
        }
        if (type == typeof(Matrix4x4))
        {
            var elements = new float[16];
            for (var index = 0; index < elements.Length; ++index)
            {
                elements[index] = Component(value, index, 16);
            }
            return new Matrix4x4(elements[0], elements[1], elements[2], elements[3], elements[4], elements[5],
                                 elements[6], elements[7], elements[8], elements[9], elements[10], elements[11],
                                 elements[12], elements[13], elements[14], elements[15]);
        }
        return JsonSerializer.Deserialize(value.GetRawText(), type);
    }

    private static float Component(JsonElement value, int index, int length)
    {
        if (value.GetArrayLength() != length)
        {
            throw new ArgumentException("Wrong property vector/matrix length");
        }
        return value[index].GetSingle();
    }

    internal static object? RefreshNode(ScriptScene scene, Type type, object? value, out bool changed)
    {
        changed = false;
        if (value == null)
        {
            return null;
        }
        if (type.IsArray)
        {
            var source = (Array)value;
            Array? result = null;
            for (var index = 0; index < source.Length; ++index)
            {
                var item = RefreshNode(scene, type.GetElementType()!, source.GetValue(index), out var replaced);
                if (replaced)
                {
                    result ??= (Array)source.Clone();
                    result.SetValue(item, index);
                }
            }
            changed = result != null;
            return result ?? value;
        }
        if (value is Node node)
        {
            var replacement = scene.ResolveNodeIfAlive(node.Id);
            if (replacement == null)
            {
                changed = true;
                return null;
            }
            if (!ReferenceEquals(node, replacement))
            {
                if (!type.IsInstanceOfType(replacement))
                {
                    throw new InvalidCastException("Replacement changed the reference's managed base type");
                }
                changed = true;
                return replacement;
            }
        }
        return value;
    }
}
