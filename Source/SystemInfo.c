#include "SystemInfo.h"

#if defined _WIN32 || defined _WIN64
#   include <windows.h>

uint32_t getProcessorCount()
{
    SYSTEM_INFO si;

    GetSystemInfo(&si);
    return si.dwNumberOfProcessors;
}

uint32_t getProcessId()
{
    return (uint32_t) GetCurrentProcessId();
}

#elif defined __unix__ || defined __APPLE__
#   include <sys/types.h>
#   include <unistd.h>

uint32_t getProcessorCount()
{
    return (uint32_t) sysconf(_SC_NPROCESSORS_ONLN);
}

uint32_t getProcessId()
{
    return (uint32_t) getpid();
}

#endif
