#pragma once

#include <Windows.h>
#include <malloc.h>
#include <cstddef>

namespace xr_heap
{
struct UsageSnapshot
{
    std::size_t bytes = 0;
    int status = _HEAPBADBEGIN;
};

inline UsageSnapshot Snapshot() noexcept
{
    UsageSnapshot result;
    const HANDLE heap = reinterpret_cast<HANDLE>(_get_heap_handle());
    if (!heap || !HeapLock(heap))
        return result;

    // _heapwalk validates the previous entry. A worker freeing it between
    // iterations otherwise looks like heap corruption. Do not allocate, log,
    // or acquire another engine lock while holding this heap lock.
    _HEAPINFO info = {};
    while ((result.status = _heapwalk(&info)) == _HEAPOK)
    {
        if (info._useflag == _USEDENTRY)
            result.bytes += info._size;
    }
    HeapUnlock(heap);
    return result;
}
}
