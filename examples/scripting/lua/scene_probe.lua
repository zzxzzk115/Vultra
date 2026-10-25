local BeaconPickup = {
    class_name = "BeaconPickup",
    extends = "Node3D",
    exports = {
        pickup_radius = { kind = "float", default = 0.18, min = 0.01, max = 0.5, speed = 0.01,
                          label = "Pickup radius" },
    },
    score = 0,
    target = 1,
    angles = {0.6, 1.8, 3.0, 4.2, 5.4},
}

function BeaconPickup:_ready()
    self.ship = self.scene:root():get_child(0)
end

function BeaconPickup:_process(delta_seconds)
    if not self.ship then
        self.ship = self.scene:root():get_child(0)
    end
    local ship_x, ship_y = self.ship:position()
    local beacon_x, beacon_y = self.node:position()
    local dx, dy = ship_x - beacon_x, ship_y - beacon_y
    if dx * dx + dy * dy < self.pickup_radius * self.pickup_radius then
        self.score = self.score + 1
        self.target = self.target % #self.angles + 1
        local angle = self.angles[self.target]
        self.node:set_position(0.68 * math.cos(angle), 0.68 * math.sin(angle), 0)
    end
end

function BeaconPickup:_editor_gui(gui)
    gui:text(string.format("Lua  |  Beacon pickups: %d", self.score))
end

return BeaconPickup
