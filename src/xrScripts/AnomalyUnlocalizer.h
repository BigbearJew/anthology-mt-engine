#pragma once

namespace AnomalyUnlocalizer
{
void Reset();
bool Apply(LPCSTR nameSpace, LPCSTR source, size_t size, xr_string& output);
}
