#pragma once

bool anthology_prepare_flora_textures(LPCSTR base, string512& green, string512& autumn, string512& dead);

void uber_deffer(CBlender_Compile& C, bool hq, LPCSTR _vspec, LPCSTR _pspec, BOOL _aref, LPCSTR _detail_replace = 0,
                 bool DO_NOT_FINISH = false, bool DO_NOT_WRITE = false);
void uber_shadow(CBlender_Compile& C, LPCSTR _vspec);
