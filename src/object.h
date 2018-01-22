#pragma once

#include "common.h"
#include "strproc.h"
#include "log.h"

#include <stdbool.h>

typedef bool (*prologue_callback)(void *, void **, void *);
typedef bool (*epilogue_callback)(void *, void *, void *);
typedef void (*dispose_callback)(void *);

struct program_object;

void program_object_dispose(struct program_object *);
bool program_object_execute(struct program_object *, void *);

struct xml_node
{
    struct strl name;
    size_t sz;
    prologue_callback prologue;
    epilogue_callback epilogue;
    dispose_callback dispose;    
    struct {
        struct att {
            struct strl name;
            ptrdiff_t offset;
            void *context;
            read_callback handler;
        } *att;
        size_t att_cnt;
    };
    struct {
        struct xml_node *dsc;
        size_t dsc_cnt;
    };
};

struct program_object *program_object_from_xml(struct xml_node *, const char *, struct message *);
