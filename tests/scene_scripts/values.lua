local Movement = { Idle=0, Cruise=2 }
local Script = {
    target='class data must not override a nil exported value',
    class_name='ValueProbe', extends='Node3D', exports={
        mode={kind='enum', enum=Movement, default=Movement.Idle},
        samples={kind='array', element_kind='int', integer_bits=16, default={1,2}},
        modes={kind='array', element_kind='enum', enum=Movement, default={Movement.Idle,Movement.Cruise}},
        uv={kind='vector2', default={0.25,0.75}},
        offset={kind='vector3', default={1,2,3}},
        tint={kind='vector4', default={1,0,0,1}},
        basis={kind='matrix4', default={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}},
        vectors={kind='array', element_kind='vector3', default={{0,0,0},{1,1,1}}},
        target={kind='reference', reference={category='node',type='Node3D'}},
        peers={kind='array',element_kind='reference',reference={category='node',type='Node3D'},default={}},
        material={kind='reference',reference={category='resource',type='MaterialResource'}},
        readyTarget={kind='string',default=''},
        revision={kind='float',default=1}
    }
}
function Script:_ready()
    self.readyTarget = self.target and self.target:name() or ''
end
function Script:_process(delta)
    if self.target == nil then self.readyTarget = 'cleared'; return end
    if self.mode ~= Movement.Cruise then return end
    local sum = self.offset[1] + self.basis[13] + self.vectors[1][1]
    for _, value in ipairs(self.samples) do sum = sum + value end
    assert(self.node:get_property('target') == self.target)
    self.node:set_property('target', self.target)
    local peers = self.node:get_property('peers')
    assert(#peers == #self.peers)
    if #peers == 2 then assert(peers[2] == self.scene.null) end
    self.node:set_position(sum, #self.target:name() + #self.material:get_property('name') + #self.peers + self.modes[1],
                           self.offset[3] + self.revision)
end
return Script
