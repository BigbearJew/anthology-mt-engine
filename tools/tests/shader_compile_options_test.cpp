#include "../../src/Layers/xrRender/ShaderCompileOptions.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

static void check(bool value)
{
    if (!value) { std::fputs("FAIL: shader options crossed compilation threads\n", stderr); std::exit(1); }
}

int main()
{
    ShaderCompileOptions::add("TESS_PN", "1");
    ShaderCompileOptions::add("USE_TDETAIL", "1");
    std::atomic<int> phase{0};
    std::thread plain([&] {
        check(ShaderCompileOptions::get().empty());
        ShaderCompileOptions::add("PLAIN_THREAD", "1");
        phase.store(1, std::memory_order_release);
        while (phase.load(std::memory_order_acquire) != 2) std::this_thread::yield();
        check(ShaderCompileOptions::get().size() == 1);
        check(std::strcmp(ShaderCompileOptions::get()[0].Name, "PLAIN_THREAD") == 0);
        ShaderCompileOptions::clear();
    });
    while (phase.load(std::memory_order_acquire) != 1) std::this_thread::yield();
    check(ShaderCompileOptions::get().size() == 2);
    check(std::strcmp(ShaderCompileOptions::get()[1].Name, "USE_TDETAIL") == 0);
    ShaderCompileOptions::clear();
    phase.store(2, std::memory_order_release);
    plain.join();

    std::vector<std::thread> workers;
    for (int worker = 0; worker < 8; ++worker)
        workers.emplace_back([worker] {
            char value[16]; std::snprintf(value, sizeof(value), "%d", worker);
            check(ShaderCompileOptions::get().empty());
            for (int pass = 0; pass < 4096; ++pass)
            {
                ShaderCompileOptions::add("OWNER", value);
                std::this_thread::yield();
                check(ShaderCompileOptions::get().size() == 1);
                check(std::strcmp(ShaderCompileOptions::get()[0].Definition, value) == 0);
                ShaderCompileOptions::clear();
            }
        });
    for (auto& worker : workers) worker.join();
    check(ShaderCompileOptions::get().empty());
    std::puts("PASS: isolated tessellation/plain options and 32768 concurrent add/read/clear cycles");
}
