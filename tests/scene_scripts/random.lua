local Motion = {
    class_name = "RandomMotion",
    extends = "Node3D",
    exports = {
        seed = {kind = "int", integer_bits = 64, default = 0},
        word = {kind = "int", integer_bits = 64, default = 0},
        index = {kind = "int", integer_bits = 64, default = 0},
        unit = {kind = "double", default = 0}
    }
}

function Motion:_ready()
    self.seed = self.scene:random_seed()
end

function Motion:_process(delta)
    self.seed = self.scene:random_seed()
    self.word = self.scene:random_uint("motion.word")
    self.index = self.scene:random_uint("motion.index", 7)
    self.unit = self.scene:random_unit("motion.unit")
    self.node:set_position(self.unit * 0.5, self.index * 0.02, 0)
end

return Motion
