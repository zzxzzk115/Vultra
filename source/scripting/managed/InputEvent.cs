using System;
using System.Numerics;
using Vultra.Interop;

namespace Vultra.Scripting;

// Data stays readable after dispatch. Actions/consumption require this event's original callback.
public readonly struct InputEvent
{
    private readonly ScriptScene? _scene;
    private readonly VultraSceneFrame _frame;
    private readonly VultraSceneInputData _value;
    private readonly string? _text;

    internal InputEvent(ScriptScene scene, VultraSceneFrame frame, VultraSceneInputData value, string text)
    {
        _scene = scene;
        _frame = frame;
        _value = value;
        _text = text;
    }

    public InputEventKind Kind => (InputEventKind)_value.Kind;
    public KeyCode Key => (KeyCode)_value.Code;
    public MouseCode Button => (MouseCode)_value.Code;
    public InputAction Action => (InputAction)_value.Action;
    public InputModifier Modifiers => (InputModifier)_value.Modifiers;
    public Vector2 Position => new(_value.X, _value.Y);
    public Vector2 Delta => new(_value.DeltaX, _value.DeltaY);
    public string Text => _text ?? string.Empty;

    private ScriptScene Owner => _scene ?? throw new InvalidOperationException("Input event has no owner");
    public ActionState ActionState(string name) => Owner.InputAction(_frame, _value.Serial, name);
    public void Accept() => Owner.AcceptInput(_frame, _value.Serial);
}
