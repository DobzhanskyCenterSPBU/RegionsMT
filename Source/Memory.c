#include "Memory.h"

extern inline bool arrayInit(void **, size_t, size_t);
extern inline bool arrayShrink(void **, size_t, size_t);
extern inline bool flexArrayInit(void **, size_t, size_t, size_t);
extern inline bool flexArrayInitClear(void **, size_t, size_t, size_t);
extern inline bool arrayInitClear(void **, size_t, size_t);
extern inline bool dynamicArrayTest(void **, size_t *, size_t, size_t);
extern inline bool dynamicArrayFinalize(void **, size_t *, size_t, size_t);

#if defined _WIN32 || defined _WIN64
#   include <io.h>
#   include <windows.h>

size_t fileGetSize(FILE *f)
{
    LARGE_INTEGER sz;
    if (GetFileSizeEx((HANDLE) _get_osfhandle(_fileno(f)), &sz)) return (size_t) sz.QuadPart;
    else return 0;
}

#elif defined __unix__ || defined __APPLE__
#   include <sys/mman.h>
#   include <sys/stat.h>

size_t fileGetSize(FILE *f)
{
    struct stat st;
    if (!fstat(fileno(f), &st)) return (size_t) st.st_size;
    else return 0;
}

/*

void *fileMap(FILE *f, size_t length, size_t offset)
{
    void *map = mmap(NULL, length, PROT_READ, MAP_SHARED, fileno(f), offset);
    if (map == MAP_FAILED) return NULL;
    return map;
}

void *fileUnmap()
{
    void *map = munmap();
    if (map == MAP_FAILED) return NULL;
    return map;
}
*/

#endif
