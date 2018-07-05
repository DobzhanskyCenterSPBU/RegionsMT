#pragma once

#include "common.h"
#include "strproc.h"
#include "log.h"

#include <stdbool.h>

typedef bool (*prologue_callback)(void *, void **, void *);
typedef bool (*epilogue_callback)(void *, void *, void *);
typedef void (*disposer_callback)(void *);

struct program_object;

void program_object_dispose(struct program_object *);
bool program_object_execute(struct program_object *, void *);

struct xml_attribute {
    void *ptr;
    void *context;
    read_callback handler;
};

struct xml_att {
    struct strl name;
    ptrdiff_t offset;
    void *context;
    read_callback handler;
};

struct xml_node {
    struct strl name;
    size_t sz;
    prologue_callback prologue;
    epilogue_callback epilogue;
    disposer_callback dispose;    
    struct {
         *att;
        size_t att_cnt;
    };
    struct {
        struct xml_node *dsc;
        size_t dsc_cnt;
    };
};

struct program_object *program_object_from_xml(struct xml_node *, const char *, struct log *);
