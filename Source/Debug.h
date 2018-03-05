#pragma once

#if defined _MSC_VER && defined _DEBUG

#   define _CRTDBG_MAP_ALLOC
#   include <crtdbg.h>

// Memory leaks will be reported at the program exit
#   define crtDbgInit() \
        do { \
            _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF); \
            _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE); \
            _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR); \
            _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE); \
            _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR); \
            _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE); \
            _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR); \
        } while (0)

#else

#   define crtDbgInit()

#endif
