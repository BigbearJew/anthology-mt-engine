#pragma once

class CActor;
struct lua_State;

bool AnomalyCameraHudEnabled(const CActor* actor);
void RegisterAnomalyScriptCamera(lua_State* L);
