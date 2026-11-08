#include "SystemInfo.h"

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <psapi.h>
#elif defined (__APPLE__)
 #include <mach/mach.h>
#else
 #include <cstdio>
 #include <unistd.h>
#endif

namespace dm::ui
{
std::size_t getProcessMemoryBytes()
{
#if defined (_WIN32)
    PROCESS_MEMORY_COUNTERS pmc {};
    if (GetProcessMemoryInfo (GetCurrentProcess(), &pmc, sizeof (pmc)))
        return (std::size_t) pmc.WorkingSetSize;
    return 0;
#elif defined (__APPLE__)
    mach_task_basic_info info {};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info (mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t) &info, &count) == KERN_SUCCESS)
        return (std::size_t) info.resident_size;
    return 0;
#else
    long pages = 0, resident = 0;
    if (FILE* f = std::fopen ("/proc/self/statm", "r"))
    {
        if (std::fscanf (f, "%ld %ld", &pages, &resident) != 2) resident = 0;
        std::fclose (f);
    }
    return (std::size_t) resident * (std::size_t) sysconf (_SC_PAGESIZE);
#endif
}
} // namespace dm::ui
