#include "Common.h"
#include "Debug.h"
#include "Log.h"

#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

#if defined _WIN32 || defined _WIN64
#   include <windows.h>

uint64_t getTime()
{
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);

    return ((uint64_t) ft.dwHighDateTime << 32 | (uint64_t) ft.dwLowDateTime) / 10;
}

#elif defined __unix__ || defined __APPLE__
#   include <sys/time.h>

uint64_t getTime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    return (uint64_t) tv.tv_sec * 1000000 + (uint64_t) tv.tv_usec;
}

#endif

// Length of time stamp
#define TIME_LEN 28

void logMsg(logInfo *log, const char *format, ...)
{
    time_t t;
    struct tm ts;
    char buff[TIME_LEN + 1];
    int msgpr;
    
    time(&t);
    localtime_s(&ts, &t);    
    strftime(buff, sizeof buff, "%Y-%m-%d %H:%M:%S UTC%z", &ts);        

    spinlockAcquire(&log->lock);
    
    msgpr = fprintf(log->dev, "[%." TOSTRING(TIME_LEN) "s] ", buff);
    if (msgpr > 0) log->size += msgpr;

    va_list args;    
    va_start(args, format);    
    msgpr = vfprintf(log->dev, format, args);
    if (msgpr > 0) log->size += msgpr;
    va_end(args);

    fflush(log->dev);
    
    spinlockRelease(&log->lock);
}

void logTime(logInfo *log, uint64_t inivalue, const char *func, const char *descr)
{
    uint64_t diff = getTime() - inivalue;
    uint64_t hdq = diff / UINT64_C(3600000000), hdr = diff % UINT64_C(3600000000), mdq = hdr / UINT64_C(60000000), mdr = hdr % UINT64_C(60000000);
    double sec = (double) mdr / 1.e6;

    if (hdq) logMsg(log, "INFO (%s): %s took %llu hr %llu min %.4f sec.\n", func, descr, hdq, mdq, sec);
    else if (mdq) logMsg(log, "INFO (%s): %s took %llu min %.4f sec.\n", func, descr, mdq, sec);
    else logMsg(log, "INFO (%s): %s took %.4f sec.\n", func, descr, sec);
}

void logError(logInfo *log, const char *func, errno_t err)
{
    char tempbuff[TEMP_BUFF] = { '\0' };
    strerror_s(tempbuff, sizeof tempbuff, err);
    logMsg(log, "ERROR (%s): %s!\n", func, tempbuff);
}