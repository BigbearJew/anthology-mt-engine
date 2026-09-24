#include "../../src/xrCore/xrCore.h"
#include "../../src/xrCore/HeapUsageSnapshot.h"
#include <array>
#include <cstdio>

static_assert(std::is_base_of_v<ref_count_storage<CounterPolicy::Atomic>, str_container>);
static_assert(std::is_base_of_v<ref_count_storage<CounterPolicy::Atomic>, str_value>);

int main()
{
    // Exercise the actual entry counter and pointer copy/release operations.
    // Independent pointer instances must leave the surviving owner untouched.
    str_value entry;
    intrusive_ptr<str_value> owner(&entry);
    std::atomic<bool> start{false};
    std::array<std::thread, 8> workers;
    for (auto& worker : workers)
        worker = std::thread([&] {
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            for (unsigned i = 0; i != 200000; ++i)
            {
                auto first = owner;
                auto second = first;
                first = nullptr;
            }
        });
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();
    if (entry.intrusive_ref_count() != 1) return 1;
    owner = nullptr;
    if (entry.intrusive_ref_count() != 0) return 2;

    // Heap enumeration must coexist with actual CRT allocation/free churn.
    std::atomic<bool> stop{false};
    std::atomic<unsigned> allocations{0};
    for (auto& worker : workers)
        worker = std::thread([&] {
            while (!stop.load(std::memory_order_relaxed))
            {
                void* blocks[64];
                for (unsigned i = 0; i != 64; ++i)
                {
                    blocks[i] = malloc(32 + i * 37);
                    if (!blocks[i]) std::abort();
                    memset(blocks[i], 0x5a, 32 + i * 37);
                }
                for (void* block : blocks) free(block);
                allocations.fetch_add(64, std::memory_order_relaxed);
            }
        });
    while (allocations.load() < 4096) std::this_thread::yield();
    unsigned failures = 0;
    for (unsigned i = 0; i != 300; ++i)
    {
        const auto sample = xr_heap::Snapshot();
        if (sample.status != _HEAPEND && sample.status != _HEAPEMPTY) ++failures;
        Sleep(1);
    }
    stop.store(true);
    for (auto& worker : workers) worker.join();
    std::printf("reference copies=3200000 heap snapshots=300 allocations=%u failures=%u\n",
        allocations.load(), failures);
    return failures || !allocations.load() ? 3 : 0;
}
