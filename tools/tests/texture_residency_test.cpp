#include "../../src/Layers/xrRender/TextureResidencyPolicy.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace
{
unsigned checks = 0;
void check(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
}

int main()
{
    using namespace texture_residency;
    constexpr Bytes GiB = 1024 * MiB;
    // Reproduce the native-render capture: 19.7 GiB allocated, 9.9 GiB DXGI budget.
    check(Pressure(false, 10142 * MiB, 9876 * MiB, 19700 * MiB, 250 * MiB, 32 * GiB),
        "native capture enters pressure despite allocations above 4 GiB");
    check(Pressure(false, 6 * GiB, 10 * GiB, 22 * GiB, 12 * GiB, 32 * GiB),
        "paged-out textures remain in the 64-bit allocation budget");
    check(!Pressure(false, 7 * GiB, 10 * GiB, 7 * GiB, 12 * GiB, 32 * GiB), "healthy working set");
    check(Pressure(true, 85 * MiB, 100 * MiB, 60 * MiB, 12 * GiB, 32 * GiB), "pressure hysteresis");
    check(!Pressure(true, 79 * MiB, 100 * MiB, 74 * MiB, 12 * GiB, 32 * GiB), "recovery exits pressure");
    check(!Pressure(false, 0, 0, 22 * GiB, 12 * GiB, 32 * GiB), "unknown DXGI budget does not evict world");
    check(Pressure(false, 0, 0, 0, 250 * MiB, 32 * GiB), "RAM exhaustion works without DXGI query");
    check(Pressure(true, 0, 0, 0, 3 * GiB, 32 * GiB), "RAM recovery also has hysteresis");
    check(!Pressure(true, 0, 0, 0, 5 * GiB, 32 * GiB), "RAM pressure clears after recovery");

    check(!Expired(100000, 0, 99999, true, true, true), "visible sky survives pressure");
    check(!Expired(100000, 0, 99999, true, false, true), "visible main/PiP world survives pressure");
    check(!Expired(100000, 99999, 0, false, true, true), "new demand load has grace");
    check(Expired(15000, 0, 0, false, true, false), "unused sky expires without pressure");
    check(!Expired(14999, 0, 0, false, true, false), "idle timeout boundary");
    check(!Expired(100000, 0, 0, false, false, false), "world retained without pressure");
    check(Expired(20000, 0, 0, false, false, true), "never drawn world can leave under pressure");
    check(!Expired(59999, 0, 0, true, false, true), "recently drawn world gets longer grace");
    check(Expired(60000, 0, 0, true, false, true), "unused world eventually leaves under pressure");
    check(!Expired(70000, 69999, 0, true, false, true), "reload does not immediately re-evict");
    check(Expired(4000, 0xfffff000u, 0xfffff000u, true, true, true), "tick wrap preserves elapsed time");
    check(!Expired(400, 0xffffff00u, 0xffffff00u, true, true, true), "tick wrap cannot evict fresh image");

    char first[] = "GSCFokYa/NPC/Body";
    char second[] = "gscfokya\\npc\\body";
    CanonicalizeFileName(first);
    check(std::strcmp(first, second) == 0, "case/slash aliases share one file texture");
    char target[] = "$user$Scope/Depth";
    CanonicalizeFileName(target);
    check(std::strcmp(target, "$user$Scope/Depth") == 0, "named RT identity is preserved");
    char sky[] = "Sky/Weather/Clouds";
    CanonicalizeFileName(sky);
    check(DemandOnly(sky) && DemandOnly("ui\\pda"), "optional image families remain demand-loaded");
    for (const char* name : {"map\\jupiter", "map\\subfolder\\escape", "ui\\ui_global_map", "ui\\ui_nomap2"})
        check(PdaMap(name) && !DemandOnly(name), "PDA map preloads and is retained");
    check(!PdaMap(nullptr) && !PdaMap("maple\\tree") && !PdaMap("ui\\ui_global_map_icons"), "only actual map families retained");
    check(!DemandOnly("skybox") && !DemandOnly("weapons\\scope") && !DemandOnly("$user$sky"), "no prefix collisions");
    std::printf("texture residency: %u checks passed\n", checks);
}
