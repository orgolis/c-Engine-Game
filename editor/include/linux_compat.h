#pragma once

#ifndef _WIN32

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sys/sysinfo.h>

#ifndef _TRUNCATE
#define _TRUNCATE static_cast<std::size_t>(-1)
#endif

inline int strncpy_s(char* dest,
                     std::size_t dest_size,
                     const char* src,
                     std::size_t count) {
    if (!dest || dest_size == 0) {
        return 22;
    }

    if (!src) {
        dest[0] = '\0';
        return 22;
    }

    std::size_t copy_count = std::strlen(src);
    if (count != _TRUNCATE) {
        copy_count = std::min(copy_count, count);
    }
    copy_count = std::min(copy_count, dest_size - 1);

    std::memcpy(dest, src, copy_count);
    dest[copy_count] = '\0';
    return 0;
}

struct MEMORYSTATUSEX {
    std::uint32_t dwLength = 0;
    std::uint32_t dwMemoryLoad = 0;
    std::uint64_t ullTotalPhys = 0;
    std::uint64_t ullAvailPhys = 0;
    std::uint64_t ullTotalPageFile = 0;
    std::uint64_t ullAvailPageFile = 0;
    std::uint64_t ullTotalVirtual = 0;
    std::uint64_t ullAvailVirtual = 0;
    std::uint64_t ullAvailExtendedVirtual = 0;
};

inline int GlobalMemoryStatusEx(MEMORYSTATUSEX* status) {
    if (!status) {
        return 0;
    }

    struct sysinfo info {};
    if (sysinfo(&info) != 0) {
        return 0;
    }

    const std::uint64_t unit = static_cast<std::uint64_t>(info.mem_unit);
    status->ullTotalPhys = static_cast<std::uint64_t>(info.totalram) * unit;
    status->ullAvailPhys = static_cast<std::uint64_t>(info.freeram + info.bufferram) * unit;
    status->ullTotalPageFile = static_cast<std::uint64_t>(info.totalswap) * unit;
    status->ullAvailPageFile = static_cast<std::uint64_t>(info.freeswap) * unit;

    if (status->ullTotalPhys > 0) {
        const std::uint64_t used = status->ullTotalPhys -
                                   std::min(status->ullTotalPhys, status->ullAvailPhys);
        status->dwMemoryLoad = static_cast<std::uint32_t>((used * 100u) / status->ullTotalPhys);
    }

    return 1;
}

#endif // !_WIN32
