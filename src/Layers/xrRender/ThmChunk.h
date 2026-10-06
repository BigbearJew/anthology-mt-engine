#pragma once

// THM optional chunks need not be adjacent or present. Repair only a broken
// chunk chain, never a valid chain that happens to omit the requested chunk.
inline u32 FindTextureChunk(IReader& file, u32 id, bool& repaired)
{
    const u32 length = file.length();
    u32 position = 0;
    u32 recovery_start = 0;
    bool broken = false;
    file.m_last_pos = 0;
    while (position < length)
    {
        if (length - position < 8)
        {
            broken = true;
            break;
        }
        file.seek(position);
        const u32 type = file.r_u32() & ~CFS_CompressMark;
        const u32 size = file.r_u32();
        const u32 payload = file.tell();
        if (size > length - payload)
        {
            broken = true;
            break;
        }
        if (type == id)
        {
            file.m_last_pos = length - payload - size >= 8 ? payload + size : 0;
            return size;
        }
        recovery_start = payload;
        position = payload + size;
    }
    if (!broken)
        return 0;

    // Some legacy THMs wrote an incorrect length for a preceding chunk.
    for (u32 offset = recovery_start; offset <= length && length - offset >= 8; ++offset)
    {
        file.seek(offset);
        const u32 type = file.r_u32() & ~CFS_CompressMark;
        const u32 size = file.r_u32();
        if (type == id && size <= length - file.tell())
        {
            repaired = true;
            return size;
        }
    }
    return 0;
}
