local Base = {
    class_name = "InputBase",
    extends = "Node3D",
    exports = {
        inputs = {kind = "int", default = 0},
        presses = {kind = "int", default = 0},
        unhandled = {kind = "int", default = 0},
        expired = {kind = "int", default = 0},
        updates = {kind = "int", default = 0},
        typed = {kind = "string", default = ""}
    }
}

function Base:check_previous()
    if self.previous then
        assert(not pcall(function() self.previous:accept() end), "Stored Lua input remained active")
        self.expired = self.expired + 1
    end
end

function Base:_input(event)
    self.inputs = self.inputs + 1
    self:check_previous()
    self.previous = event
    if event.kind == InputEventKind.Key and event.code == KeyCode.F2 then
        assert(self.node:is_input_enabled())
        self.node:set_input_enabled(false)
        return
    end
    if event.kind == InputEventKind.Key and event.code == KeyCode.F3 then
        assert(self.node:is_unhandled_input_enabled())
        self.node:set_unhandled_input_enabled(false)
        return
    end
    if event.kind == InputEventKind.Key and event.code == KeyCode.Delete then
        self.node:queue_free()
        return
    end
    if event:action_state("jump").pressed then
        self.presses = self.presses + 1
        local x, y, z = self.node:position()
        self.node:set_position(x + 1, y, z)
        event:accept()
    end
    if event.kind == InputEventKind.Text then
        self.typed = self.typed .. event.text
    end
end

function Base:_process(delta)
    self.updates = self.updates + 1
    self:check_previous()
end

local Controller = {class_name = "InputController", extends = Base}

function Controller:_unhandled_input(event)
    self.unhandled = self.unhandled + 1
    local x, y, z = self.node:position()
    if event.kind == InputEventKind.MouseButton and event.code == MouseCode.Left and event.action == InputAction.Press then
        y = y + 2
    end
    if event.kind == InputEventKind.MouseMotion then
        z = z + event.delta_x * 0.01
    end
    if event.kind == InputEventKind.MouseScroll then
        z = z - event.delta_y * 0.25
    end
    self.node:set_position(x, y, z)
end

return Controller
