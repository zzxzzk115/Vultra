using System;
using System.Numerics;
using Vultra.Scripting;

namespace VultraTests;

public partial class InputBase : Node3D
{
    [Export] public long Inputs;
    [Export] public long Presses;
    [Export] public long Unhandled;
    [Export] public long Expired;
    [Export] public long Updates;
    [Export] public string Typed = "";
    [NonSerialized] private InputEvent? _previous;

    private void CheckPrevious()
    {
        if (_previous is { } previous)
        {
            var rejected = false;
            try
            {
                previous.Accept();
            }
            catch (InvalidOperationException)
            {
                rejected = true;
            }
            if (!rejected)
            {
                throw new InvalidOperationException("Stored managed input remained active");
            }
            ++Expired;
        }
    }

    public override void _Input(InputEvent input)
    {
        ++Inputs;
        CheckPrevious();
        _previous = input;
        if (input.Kind == InputEventKind.Key && input.Key == KeyCode.F2)
        {
            if (!InputEnabled)
            {
                throw new InvalidOperationException("Input was already disabled");
            }
            InputEnabled = false;
            return;
        }
        if (input.Kind == InputEventKind.Key && input.Key == KeyCode.F3)
        {
            if (!UnhandledInputEnabled)
            {
                throw new InvalidOperationException("Unhandled input was already disabled");
            }
            UnhandledInputEnabled = false;
            return;
        }
        if (input.Kind == InputEventKind.Key && input.Key == KeyCode.Delete)
        {
            QueueFree();
            return;
        }
        if (input.ActionState("jump").Pressed)
        {
            ++Presses;
            Position += Vector3.UnitX;
            input.Accept();
        }
        if (input.Kind == InputEventKind.Text)
        {
            Typed += input.Text;
        }
    }

    public override void _Process(double delta)
    {
        ++Updates;
        CheckPrevious();
    }
}

public sealed partial class InputController : InputBase
{
    public override void _UnhandledInput(InputEvent input)
    {
        ++Unhandled;
        var position = Position;
        if (input.Kind == InputEventKind.MouseButton && input.Button == MouseCode.Left && input.Action == InputAction.Press)
        {
            position.Y += 2;
        }
        if (input.Kind == InputEventKind.MouseMotion)
        {
            position.Z += input.Delta.X * 0.01f;
        }
        if (input.Kind == InputEventKind.MouseScroll)
        {
            position.Z -= input.Delta.Y * 0.25f;
        }
        Position = position;
    }
}
