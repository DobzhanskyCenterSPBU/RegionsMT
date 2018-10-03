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
    //struct strl name;
    ptrdiff_t offset;
    void *context;
    read_callback handler;
};

struct xml_node {
    //struct strl name;
    size_t sz;
    prologue_callback prologue;
    epilogue_callback epilogue;
    disposer_callback dispose;    
    //struct {
    //    struct xml_node *dsc;
    //    size_t dsc_cnt;
    //};
};

typedef bool (*xml_node_selector_callback)(struct xml_node *, char *, size_t, void *);
typedef bool (*xml_att_selector_callback)(struct xml_att *, char *, size_t, void *, size_t *);

struct program_object *program_object_from_xml(const char *, xml_node_selector_callback, xml_att_selector_callback, void *, struct log *);

enum {
    OFF_HD,

    STP_HD0 = OFF_HD,
    STP_W00, STP_HD1, STP_W01, STP_EQ0, STP_W02, STP_QO0,

    OFF_QC,

    STP_QC0 = OFF_QC,
    STP_HH0, STP_HS0, STP_W03, STP_HD2, STP_W04, STP_EQ1, STP_W05, STP_QO1,
    STP_QC1, STP_HH1, STP_HS1, STP_W06, STP_HD3, STP_W07, STP_EQ2, STP_W08,
    STP_QO2, STP_QC2, STP_HH2, STP_HS2, STP_W09, STP_HD4, STP_HE0, STP_HE1,

    OFF_LA,

    STP_W10 = OFF_LA,
    STP_LT0,

    OFF_SL,

    STP_SL0 = OFF_SL,
    STP_ST0, STP_TG0,

    OFF_LB,

    STP_W11 = OFF_LB,
    STP_EA0,

    OFF_EB,

    STP_EB0 = OFF_EB,
    STP_ST1, STP_AH0, STP_W12, STP_EQ3, STP_W13, STP_QO3, STP_QC3, STP_AV0,

    OFF_LC,

    STP_W14 = OFF_LC,
    STP_LT1, STP_SL1, STP_ST2, STP_TG1, STP_ST3, STP_CT0, STP_EB1,

    STP_W15 = OFF_LC + STP_EB1 - OFF_EB,
    STP_LT2, STP_SL2, STP_ST4,

    OFF_SQ,

    STP_SQ0 = OFF_SQ,
    STP_SQ1 = OFF_SQ + STP_QC1 - OFF_QC,
    STP_SQ2 = OFF_SQ + STP_QC2 - OFF_QC,
    STP_SQ3 = OFF_SQ + STP_QC3 - OFF_QC,
    STP_SA0, STP_SA1, STP_SA2, STP_SA3, STP_SA4, STP_SA5, STP_SA6, STP_SA7,

    OFF_CM, STP_CM0 = OFF_CM,
    OFF_CA, STP_CA0 = OFF_CA,
    OFF_CB, STP_CB0 = OFF_CB,
    OFF_CC, STP_CC0 = OFF_CC,

    STP_CM1 = OFF_CM + STP_SL1 - OFF_SL,
    STP_CA1 = OFF_CA + STP_SL1 - OFF_SL,
    STP_CB1 = OFF_CB + STP_SL1 - OFF_SL,
    STP_CC1 = OFF_CC + STP_SL1 - OFF_SL,

    STP_CM2 = OFF_CM + STP_SL2 - OFF_SL,
    STP_CA2 = OFF_CA + STP_SL2 - OFF_SL,
    STP_CB2 = OFF_CB + STP_SL2 - OFF_SL,
    STP_CC2 = OFF_CC + STP_SL2 - OFF_SL
};

