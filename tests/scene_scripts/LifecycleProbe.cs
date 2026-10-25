using System;
using System.IO;
using System.Numerics;
using Vultra.Scripting;

namespace VultraTests;

public sealed partial class LifecycleProbe : Node3D
{
    private void Record(string phase) =>
        File.AppendAllText(Path.Combine("build/.tmp", Scene.Root.Name + ".txt"), Name + ":" + phase + "\n");

    public override void _EnterTree() => Record("enter");

    public override void _Ready()
    {
        var description = Scene.DescribeType("Node3D");
        if (Array.IndexOf(Scene.GetTypeNames(), "Node3D") < 0 ||
            description.GetProperty("version").GetInt32() != 1 ||
            description.GetProperty("base").GetString() != "Node" ||
            description.GetProperty("properties").GetArrayLength() == 0 ||
            Scene.DescribeType("CameraSettings").GetProperty("category").GetString() != "value")
        {
            throw new InvalidOperationException("Managed metadata does not describe the registered Node3D schema");
        }
        Position += Vector3.UnitX;
        SetComponentProperty("SceneProcess", "priority", -7);
        if (Array.IndexOf(GetComponents(), "SceneTransform") < 0 ||
            GetComponentProperty<float[]>("SceneTransform", "local")![12] != Position.X)
        {
            throw new InvalidOperationException("Managed components differ from the Node view");
        }
        var resource = Scene.CreateResource("Resource");
        if (resource.TypeName != "Resource")
        {
            throw new InvalidOperationException("Managed resource factory returned a different type");
        }
        resource.Remove();
        var material = Scene.CreateMaterial("Before");
        material.Name = "After";
        if (material.TypeName != "MaterialResource" || material.GetProperty<string>("name") != "After")
        {
            throw new InvalidOperationException("Managed resource properties bypass the shared ABI");
        }
        material.Remove();
        Scene.TimeScale = Scene.TimeScale;
        if (!IsInsideTree || ProcessPriority != -7 || Scene.TimeScale != 1)
        {
            throw new InvalidOperationException("Managed scene phase/priority ABI is inconsistent");
        }
        Record("ready");
    }

    public override void _PhysicsProcess(double delta)
    {
        if (Math.Abs(delta - 1.0 / 60.0) > 1e-7 || Scene.PhysicsFrame == 0)
        {
            throw new InvalidOperationException("Managed fixed update has no scene clock");
        }
        var action = Scene.Action("jump");
        if (!action.Held || action.Pressed != (Scene.PhysicsFrame == 1))
        {
            throw new InvalidOperationException("Managed fixed input repeated or lost its edge");
        }
        Position += Vector3.UnitY;
        Record("fixed");
    }

    public override void _Process(double delta)
    {
        Position += Vector3.UnitZ;
        Record("process");
    }

    public override void _ExitTree()
    {
        if (!IsInsideTree)
        {
            throw new InvalidOperationException("Managed exit lost tree access");
        }
        Record("exit");
    }
}
