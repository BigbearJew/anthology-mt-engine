local now, marks = 0, {}
local vec={};vec.__index=vec
function vector() return setmetatable({x=0,y=0,z=0},vec) end
function vec:set(x,y,z) if type(x)=='table' then self.x,self.y,self.z=x.x,x.y,x.z else self.x,self.y,self.z=x,y,z end return self end
function vec:distance_to_sqr(o) return (self.x-o.x)^2+(self.y-o.y)^2+(self.z-o.z)^2 end
function vec:square_magnitude() return self.x^2+self.y^2+self.z^2 end
function vec:normalize() local n=math.sqrt(self:square_magnitude());self.x,self.y,self.z=self.x/n,self.y/n,self.z/n;return self end
local actor={p=vector(),d=vector():set(0,0,1)}
function actor:position() return self.p end
function actor:direction() return self.d end
function actor:id() return 1 end
db={actor=actor};anthology_runtime_seasons={get_style=function()return 4 end}
function time_global() return now end
function printf(...) error('Unexpected diagnostic') end
function system_ini() return {section_exist=function()return false end} end
local manager={place_oriented=function(_,down,p,range,size,section,obj,ttl,forward)
 marks[#marks+1]={p=vector():set(p),d=vector():set(forward),id=obj:id()}
end}
function wallmarks_manager() return manager end
function ray_pick() return {
 set_position=function()end,set_direction=function(self,d)self.d=d end,
 set_flags=function()end,set_range=function()end,
 query=function(self)return self.d.y<0 end,
 get_result=function()return {material_name='materials\\earth'} end} end
z_npc_footsteps={npc_on_foot_step=function()end}
dofile(arg[1]);first_update()
assert(actor_on_footstep());assert(#marks==1)
now=500;assert(not actor_on_footstep(),'Idle callback left a mark')
actor.p.z=0.7;assert(actor_on_footstep());assert(marks[1].p.x*marks[2].p.x<0,'Feet must alternate')
local npc={p=vector():set(1,0,1),d=vector():set(1,0,0)}
function npc:id()return 2 end
npc.position=actor.position;npc.direction=actor.direction
function npc:get_bone_id()return 3 end
function npc:bone_position()return vector():set(1.2,0.05,1.1)end
now=800;z_npc_footsteps.npc_on_foot_step(nil,npc,1,true,true,false)
assert(marks[3].p.x==1.2 and marks[3].p.z==1.1,'NPC contact must use foot bone')
now=1100;actor.p.z=1.4;assert(actor_on_footstep());assert(marks[4].p.x==marks[1].p.x,'NPC changed actor foot phase')
now=1400;actor.p.x=.7;actor.d:set(1,0,0);assert(actor_on_footstep());assert(marks[5].d.x==1 and marks[5].d.z==0,'Turn lost world direction')
now=1800;actor.p.z=100;assert(not actor_on_footstep(),'Teleport left a mark')
first_update();now=2000;assert(actor_on_footstep(),'Reload did not reset transient state')
print('PASS: idle, alternating feet, NPC bone contact, independent actor/NPC phase, turning, teleport, reload')
