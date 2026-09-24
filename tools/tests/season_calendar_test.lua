local style, month, year, now = 2, 1, 2018, 0
local calendar, actor = true, {}
local applied, reads = 0, 0
local callbacks = {}
db = {actor=actor}
printf = function() end
RegisterScriptCallback = function(name, fn) callbacks[name]=fn end
time_global = function() return now end
get_console = function() return {
    get_integer=function() return style end,
    execute=function(_,cmd) style=tonumber(cmd:match("(%d+)$")); applied=applied+1 end
} end
ui_mcm = {get=function() return calendar end}
game = {get_game_time=function() reads=reads+1; return {get=function() return year,month,1,0,0,0,0 end} end}
level = {weather_exists=function(name) return name=='winter_clear' or name=='summer_clear' end}
local sections={calendar={},weather_winter={style='4',category='clear',cycle='winter_clear'},
    weather_bad={style='4',category='rain',cycle='missing_weather'}}
ini_file = function() return {
    line_exist=function(_,s,k) return sections[s] and sections[s][k]~=nil end,
    r_string=function(_,s,k) return sections[s][k] end,
    section_for_each=function(_,fn) for s in pairs(sections) do fn(s) end end
} end
local manager={presets={clear={pack_clear=true},rain={pack_rain=true}},last_hour=12,weatherType='atmosfear'}
local calls=0
level_weathers={WeatherManager={select_weather=function(self, forced)
    calls=calls+1
    if self.wfx then return 'emission' end
    self.selected=next(self.presets.clear)
    return 'original',forced
end}}
setmetatable(manager,{__index=level_weathers.WeatherManager})
level_weathers.get_weather_manager=function() return manager end
dofile(arg[1])
on_game_start()
assert(callbacks.actor_on_first_update and callbacks.actor_on_update)
callbacks.actor_on_first_update()
assert(style==4 and manager.selected=='winter_clear')
assert(manager.presets.rain.pack_rain)
local expected={4,4,5,6,1,1,1,1,2,3,5,4}
for m=1,12 do month=m; now=now+1000; on_update(); assert(style==expected[m],m) end
month=5; now=now+1000; on_update(); assert(manager.selected=='pack_clear')
calendar=false; set_style(4); on_option_change(); assert(style==4 and manager.selected=='winter_clear')
month=7; now=now+1000; on_update(); assert(style==4,'manual preserved')
calendar=true; on_option_change(); assert(style==1,'resume calendar')
local n=reads; for i=1,100 do on_update() end; assert(reads==n,'bounded polling')
now=1; on_update(); assert(reads==n+1,'clock reset')
db.actor=nil; refresh(true); assert(reads==n+1,'world readiness')
db.actor=actor; month=12; on_first_update(); assert(style==4,'save/load')
manager.wfx=true; assert(manager:select_weather(false)=='emission','emission untouched')
manager.wfx=false
manager.presets.rain={new_pack_rain=true}; manager:select_weather(false); assert(manager.presets.rain.new_pack_rain)
assert(not register_weather(8,'clear','winter_clear'))
assert(not register_weather(4,'bad','winter_clear'))
assert(not register_weather(4,'clear','missing'))
assert(register_weather(1,'clear','summer_clear'))
month=6; refresh(true); assert(manager.selected=='summer_clear')
print('PASS calendar: 12 months, manual, reload, fallback, emission, clock reset, bounded polling, addon coexistence')
