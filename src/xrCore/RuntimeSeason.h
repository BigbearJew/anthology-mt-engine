#pragma once
#include <atomic>

namespace anthology
{
// Sound updates may run on the MT worker while the console runs on the main thread.
inline std::atomic<int>& runtime_season_state()
{
	static std::atomic<int> style{1};
	return style;
}
inline int runtime_season()
{
	return runtime_season_state().load(std::memory_order_relaxed);
}
inline void set_runtime_season(int style)
{
	if (style >= 1 && style <= 6)
		runtime_season_state().store(style, std::memory_order_relaxed);
}
}
