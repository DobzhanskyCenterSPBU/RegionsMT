#pragma once

#include "Common.h"
#include "StringTools.h"
#include "Log.h"

#include <stdbool.h>

typedef bool (*prologueCallback)(void *, void **, void *);
typedef bool (*epilogueCallback)(void *, void *, void *);
typedef void (*disposeCallback)(void *);

typedef struct programObject programObject;

void programObjectDispose(programObject *);
bool programObjectExecute(programObject *, void *);

typedef struct xmlNode xmlNode;

struct xmlNode
{
    strl name;
    size_t sz;
    prologueCallback prologue;
    epilogueCallback epilogue;
    disposeCallback dispose;
    
    struct
    {
        struct att
        {
            strl name;
            ptrdiff_t offset;
            void *context;
            readHandlerCallback handler;
        } *att;
        size_t attcnt;
    };
    struct
    {
        xmlNode *dsc;
        size_t dsccnt;
    };
};

programObject *programObjectFromXML(xmlNode *, const char *, logInfo *);
