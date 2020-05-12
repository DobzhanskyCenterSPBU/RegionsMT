#include "Common.h"
#include "Compare.h"
#include "Debug.h"
#include "Density.h"
#include "Density-Report.h"
#include "DensityFold.h"
#include "DensityFold-Report.h"
#include "Framework.h"
#include "Genotypes.h"
#include "Log.h"
#include "LoadData.h"
#include "Memory.h"
#include "MySQL-Dispatch.h"
#include "MySQL-Fetch.h"
#include "Object.h"
#include "Regions.h"
#include "Sort.h"
#include "StringTools.h"
#include "x86_64/Tools.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
//
//  Command-line parser which is nearly equal to 'getopt_long' GNU function
//

enum
{
    CMDARGS_BIT_POS_HELP = 0,
    CMDARGS_BIT_CNT
};

typedef struct
{
    frameworkIn in;
    uint8_t bits[BYTE_CNT(CMDARGS_BIT_CNT)];
} cmdArgs;

///////////////////////////////////////////////////////////////////////////////

typedef struct
{
    struct
    {
        struct ltag
        {
            strl name;
            size_t id;
        } *ltag; // Array should be sorted by 'str' according to the 'strncmp'
        size_t ltagcnt;
    };
    struct
    {
        struct stag
        {
            char ch;
            size_t id;
        } *stag; // Array should be sorted by 'ch'
        size_t stagcnt;
    };
    struct
    {
        struct par
        {
            ptrdiff_t offset; // Offset of the field
            void *context; // Third argument of the handler
            readHandlerCallback handler;
            bool val; // Parameter requires value or not?
        } *par;
        size_t parcnt;
    };
} cmdArgsScheme;

static void cmdArgsParse(cmdArgsScheme *sch, void *out, char **arr, size_t cnt, char **input, size_t *inputcnt, logInfo *inf)
{
    const char *strings[] =
    {
        __FUNCTION__,
        "WARNING (%s): %s \"%s\" (par. no. %zu)!\n",
        "WARNING (%s): %s \'%c\' (par. no. %zu)!\n",
        "Invalid command-line parameter",
        "Unexpected command-line parameter",
        "Unable to handle value", 
        "Unexpected symbol",
        "No value is provided for the command-line parameter"
    };

    enum
    {
        STR_FN = 0,
        STR_FR_WL, 
        STR_FR_WS,
        STR_M_INV,
        STR_M_UNE,
        STR_M_HAN,
        STR_M_SYM,
        STR_M_VAL
    };

    size_t prev = 0;
    bool capture = 0, stop = 0;
    *inputcnt = 0;
        
    for (size_t i = 1; i < cnt; i++)
    {
        if (arr[i][0] == '-')
        {
            if (capture || stop)
            {
                logMsg(inf, strings[STR_FR_WL], strings[STR_FN], strings[STR_M_SYM], arr[i], i);
                continue;
            }

            if (arr[i][1] == '-') // Long mode
            {
                if (!arr[i][2]) // Stop on '--'
                {
                    stop = 1;
                    continue;
                }

                size_t j = binarySearch(arr[i] + 2, sch->ltag, sizeof *sch->ltag, sch->ltagcnt, (compareCallbackStable) strCompDict, NULL);
                
                if (j + 1)
                {
                    size_t len = sch->ltag[j].name.len, id = sch->ltag[j].id;
                    
                    if (sch->par[id].val) // Parameter expects value
                    {
                        if (arr[i][len + 2])
                        {
                            if (arr[i][len + 2] == '=') // Executing valued parameter handler
                            {
                                bool res = sch->par[id].handler(arr[i] + len + 3, 0, (char *) out + sch->par[id].offset, sch->par[id].context);
                                if (!res) logMsg(inf, strings[STR_FR_WL], strings[STR_FN], strings[STR_M_HAN], arr[i] + len + 3, i);
                            }
                            else logMsg(inf, strings[STR_FR_WL], strings[STR_FN], strings[STR_M_SYM], arr[i] + len + 2, i);
                        }
                        else if (i + 1 < cnt) capture = 1, prev = id;// Parameter value is separated with whitespace
                        else logMsg(inf, strings[STR_FR_WL], strings[STR_FN], strings[STR_M_VAL], arr[i] + 2, i);
                    }
                    else sch->par[id].handler(NULL, 0, (char *) out + sch->par[id].offset, sch->par[id].context);
                }
                else logMsg(inf, strings[STR_FR_WL], strings[STR_FN], strings[STR_M_INV], arr[i] + 2, i);
            }
            else // Short mode
            {
                for (size_t k = 1; arr[i][k]; k++) // Inner loop for handling multiple option-like parameters
                {
                    size_t j = binarySearch(&arr[i][k], sch->stag, sizeof *sch->stag, sch->stagcnt, (compareCallbackStable) charComp, NULL);
                    
                    if (j + 1)
                    {
                        size_t id = sch->stag[j].id;
                        
                        if (sch->par[id].val) // Parameter expects value
                        {
                            if (k == 1) // Valued parameter can't be stacked
                            {
                                if (arr[i][2]) // Executing valued parameter handler
                                {
                                    bool res = sch->par[id].handler(arr[i] + 2, 0, (char *) out + sch->par[id].offset, sch->par[id].context);
                                    if (!res) logMsg(inf, strings[STR_FR_WL], strings[STR_FN], strings[STR_M_HAN], arr[i] + 2, i);
                                }
                                else if (i + 1 < cnt) capture = 1, prev = id; // Parameter value is separated by whitespace
                                else logMsg(inf, strings[STR_FR_WS], strings[STR_FN], strings[STR_M_VAL], arr[i][k], i);
                                
                                break; // We need to exit from the inner loop
                            }
                            else logMsg(inf, strings[STR_FR_WS], strings[STR_FN], strings[STR_M_UNE], arr[i][k], i);
                        }
                        else sch->par[id].handler(NULL, 0, (char *) out + sch->par[id].offset, sch->par[id].context);
                    }
                    else logMsg(inf, strings[STR_FR_WS], strings[STR_FN], strings[STR_M_INV], arr[i][k], i);
                }
            }
        }
        else
        {
            if (capture) // Capturing parameters separated by whitespace with corresponding title
            {
                bool res = sch->par[prev].handler(arr[i], 0, (char *) out + sch->par[prev].offset, sch->par[prev].context);
                if (!res) logMsg(inf, strings[STR_FR_WL], strings[STR_FN], strings[STR_M_HAN], arr[i], i);
                capture = 0;
            }
            else input[(*inputcnt)++] = arr[i]; // Storing input files
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
//
//  Main routine 
//

int main(int argc, char **argv) 
{
    const char *strings[] =
    {
        __FUNCTION__,
        "NOTE (%s): No input data specified.\n",
        "INFO (%s): Help mode triggered.\n",
        "WARNING (%s): Unable to compile XML document \"%s\"!\n",
        "WARNING (%s): Unable to execute one or more branches of the XML document \"%s\"!\n",
        "Overall program execution"
    };

    enum
    {
        STR_FN = 0,
        STR_FR_ND,
        STR_FR_IH,
        STR_FR_WC,
        STR_FR_WE,
        STR_M_EXE
    };
    
    // All names should be sorted according to 'strncmp'!!!
    cmdArgsScheme argsch =
    {
        CLII((struct ltag[]) { { STRI("help"), 0 }, { STRI("log"), 1 }, { STRI("threads"), 2 } }),
        CLII((struct stag[]) { { 'h', 0 }, { 'l', 1 }, { 't', 2 } }),
        CLII((struct par[])
        {
            { 0, &(handlerContext) { offsetof(cmdArgs, bits), CMDARGS_BIT_POS_HELP } , (readHandlerCallback) emptyHandler, 0 },
            { offsetof(cmdArgs, in) + offsetof(frameworkIn, logFile), NULL, (readHandlerCallback) strPtrHandler, 1 },
            { offsetof(cmdArgs, in) + offsetof(frameworkIn, threadCount), &(handlerContext) { offsetof(frameworkIn, bits) - offsetof(frameworkIn, threadCount), FRAMEWORKIN_BIT_POS_THREADCOUNT }, (readHandlerCallback) uint32Handler, 1 },
        })
    };
    
    // All names on every sub-level should be sorted according to 'strncmp'!!!
    struct { xmlNode *node; size_t cnt; } dscsch = CLII((xmlNode[])
    {
        {
            .name = STRI("Density"),
            .sz = sizeof(densityContext),
            .prologue = (prologueCallback) densityPrologue,
            .epilogue = (epilogueCallback) densityEpilogue,
            .dispose = (disposeCallback) densityContextDispose,
            CLII((struct att[])
            {
                { STRI("pos"), 0, &(handlerContext) { offsetof(densityContext, bits), DENSITYCONTEXT_BIT_POS_POS }, (readHandlerCallback) boolHandler },
                { STRI("radius"), offsetof(densityContext, radius), &(handlerContext) { offsetof(densityContext, bits) - offsetof(densityContext, radius), DENSITYCONTEXT_BIT_POS_RADIUS }, (readHandlerCallback) uint32Handler },
                { STRI("type"), offsetof(densityContext, type), NULL, (readHandlerCallback) densityTypeHandler }
            }),
            CLII((xmlNode[])
            {
                {
                    .name = STRI("Fold"),
                    .sz = sizeof(densityFoldContext),
                    .prologue = (prologueCallback) densityFoldPrologue,
                    .epilogue = (epilogueCallback) densityFoldEpilogue,
                    .dispose = (disposeCallback) densityFoldContextDispose,
                    CLII((struct att[])
                    {
                        { STRI("group"), offsetof(densityFoldContext, group), NULL, (readHandlerCallback) strHandler },
                        { STRI("type"), offsetof(densityFoldContext, type), NULL, (readHandlerCallback) densityFoldHandler }
                    }),
                    CLII((xmlNode[])
                    {
                        {
                            .name = STRI("Report"),
                            .sz = sizeof(dfReportContext),
                            .prologue = (prologueCallback) dfReportPrologue,
                            .epilogue = (epilogueCallback) dfReportEpilogue,
                            .dispose = (disposeCallback) dfReportContextDispose,
                            CLII((struct att[])
                            {
                                { STRI("header"), 0, &(handlerContext) { offsetof(dfReportContext, bits), DFREPORTCONTEXT_BIT_POS_HEADER }, (readHandlerCallback) boolHandler },
                                { STRI("limit"), offsetof(dfReportContext, limit), &(handlerContext) { offsetof(dfReportContext, bits) - offsetof(dfReportContext, limit), DFREPORTCONTEXT_BIT_POS_LIMIT }, (readHandlerCallback) uint32Handler },
                                { STRI("path"), offsetof(dfReportContext, path), NULL, (readHandlerCallback) strHandler },
                                { STRI("semicolon"), 0, &(handlerContext) { offsetof(dfReportContext, bits), DFREPORTCONTEXT_BIT_POS_SEMICOLON }, (readHandlerCallback) boolHandler },
                                { STRI("threshold"), offsetof(dfReportContext, threshold), &(handlerContext) { offsetof(dfReportContext, bits) - offsetof(dfReportContext, threshold), DFREPORTCONTEXT_BIT_POS_THRESHOLD }, (readHandlerCallback) float64Handler },
                                { STRI("type"), offsetof(dfReportContext, type), NULL, (readHandlerCallback) dfReportHandler }
                            })
                        }
                    })
                },
                {
                    .name = STRI("Regions"),
                    .sz = sizeof(regionsContext),
                    .prologue = (prologueCallback) regionsPrologue,
                    .epilogue = (epilogueCallback) regionsEpilogue,
                    .dispose = (disposeCallback) regionsContextDispose,
                    CLII((struct att[])
                    {
                        { STRI("decay"), offsetof(regionsContext, decay), &(handlerContext) { offsetof(regionsContext, bits) - offsetof(regionsContext, decay), REGIONSCONTEXT_BIT_POS_DECAY }, (readHandlerCallback) uint32Handler },
                        { STRI("depth"), offsetof(regionsContext, depth), &(handlerContext) { offsetof(regionsContext, bits) - offsetof(regionsContext, depth), REGIONSCONTEXT_BIT_POS_DEPTH }, (readHandlerCallback) uint32Handler },
                        { STRI("length"), offsetof(regionsContext, length), &(handlerContext) { offsetof(regionsContext, bits) - offsetof(regionsContext, length), REGIONSCONTEXT_BIT_POS_LENGTH }, (readHandlerCallback) uint32Handler },
                        { STRI("threshold"), offsetof(regionsContext, threshold), &(handlerContext) { offsetof(regionsContext, bits) - offsetof(regionsContext, threshold), REGIONSCONTEXT_BIT_POS_THRESHOLD }, (readHandlerCallback) float64Handler },
                        { STRI("tolerance"), offsetof(regionsContext, tolerance), &(handlerContext) { offsetof(regionsContext, bits) - offsetof(regionsContext, tolerance), REGIONSCONTEXT_BIT_POS_TOLERANCE }, (readHandlerCallback) float64Handler },
                        { STRI("slope"), offsetof(regionsContext, slope), &(handlerContext) { offsetof(regionsContext, bits) - offsetof(regionsContext, slope), REGIONSCONTEXT_BIT_POS_SLOPE }, (readHandlerCallback) float64Handler }
                    })
                },
                {
                    .name = STRI("Report"),
                    .sz = sizeof(dReportContext),
                    .prologue = (prologueCallback) dReportPrologue,
                    .epilogue = (epilogueCallback) dReportEpilogue,
                    .dispose = (disposeCallback) dReportContextDispose,
                    CLII((struct att[])
                    {
                        { STRI("header"), 0, &(handlerContext) { offsetof(dReportContext, bits), DREPORTCONTEXT_BIT_POS_HEADER }, (readHandlerCallback) boolHandler },
                        { STRI("limit"), offsetof(dReportContext, limit), &(handlerContext) { offsetof(dReportContext, bits) - offsetof(dReportContext, limit), DREPORTCONTEXT_BIT_POS_LIMIT }, (readHandlerCallback) uint32Handler },
                        { STRI("path"), offsetof(dReportContext, path), NULL, (readHandlerCallback) strHandler },
                        { STRI("semicolon"), 0, &(handlerContext) { offsetof(dReportContext, bits), DREPORTCONTEXT_BIT_POS_SEMICOLON }, (readHandlerCallback) boolHandler },
                        { STRI("threshold"), offsetof(dReportContext, threshold), &(handlerContext) { offsetof(dReportContext, bits) - offsetof(dReportContext, threshold), DREPORTCONTEXT_BIT_POS_THRESHOLD }, (readHandlerCallback) float64Handler },
                        { STRI("type"), offsetof(dReportContext, type), NULL, (readHandlerCallback) dReportHandler }
                    })
                }
            })
        },
        {
            .name = STRI("MySQL.Dispatch"),
            .sz = sizeof(mysqlDispatchContext),
            .prologue = (prologueCallback) mysqlDispatchPrologue,
            .epilogue = (epilogueCallback) mysqlDispatchEpilogue,
            .dispose = (disposeCallback) mysqlDispatchContextDispose,
            CLII((struct att[])
            {
                { STRI("host"), offsetof(mysqlDispatchContext, host), NULL, (readHandlerCallback) strHandler },
                { STRI("login"), offsetof(mysqlDispatchContext, login), NULL, (readHandlerCallback) strHandler },
                { STRI("password"), offsetof(mysqlDispatchContext, password), NULL, (readHandlerCallback) strHandler },
                { STRI("port"), offsetof(mysqlDispatchContext, port), NULL, (readHandlerCallback) uint32Handler },
                { STRI("schema"), offsetof(mysqlDispatchContext, schema), NULL, (readHandlerCallback) strHandler },
                { STRI("temp.prefix"), offsetof(mysqlDispatchContext, temppr), NULL, (readHandlerCallback) strHandler },
            })
        }
    });
    
    // All names on every sub-level should be sorted according to 'strncmp'!!!
    xmlNode xmlsch =
    {
        .name = STRI("RegionsMT"),
        .sz = sizeof(frameworkContext),
        .prologue = (prologueCallback) frameworkPrologue,
        .epilogue = (epilogueCallback) frameworkEpilogue,
        .dispose = (disposeCallback) frameworkContextDispose,
        CLII((struct att[])
        {
            { STRI("log"), offsetof(frameworkContext, logFile), NULL, (readHandlerCallback) strHandler },
            { STRI("threads"), offsetof(frameworkContext, threadCount), &(handlerContext) { offsetof(frameworkContext, bits) - offsetof(frameworkContext, threadCount), FRAMEWORKCONTEXT_BIT_POS_THREADCOUNT }, (readHandlerCallback) uint32Handler }
        }),
        CLII((xmlNode[])
        {
            {
                .name = STRI("Data.Load"),
                .sz = sizeof(loadDataContext),
                .prologue = (prologueCallback) loadDataPrologue,
                .epilogue = (epilogueCallback) loadDataEpilogue,
                .dispose = (disposeCallback) loadDataContextDispose,
                CLII((struct att[])
                {
                    { STRI("header"), 0, &(handlerContext) { offsetof(loadDataContext, bits), LOADDATACONTEXT_BIT_POS_HEADER }, (readHandlerCallback) boolHandler },
                    { STRI("logarithm"), 0, &(handlerContext) { offsetof(loadDataContext, bits), LOADDATACONTEXT_BIT_POS_LOGARITHM }, (readHandlerCallback) boolHandler },
                    { STRI("no.ranks"), 0, &(handlerContext) { offsetof(loadDataContext, bits), LOADDATACONTEXT_BIT_POS_NORANKS }, (readHandlerCallback) boolHandler },
                    { STRI("path.chr"), offsetof(loadDataContext, pathchr), NULL, (readHandlerCallback) strHandler },
                    { STRI("path.row"), offsetof(loadDataContext, pathrow), NULL, (readHandlerCallback) strHandler },
                    { STRI("path.test"), offsetof(loadDataContext, pathtest), NULL, (readHandlerCallback) strHandler },
                    { STRI("path.val"), offsetof(loadDataContext, pathval), NULL, (readHandlerCallback) strHandler },
                }),
                { dscsch.node, dscsch.cnt }
            },
            {
                .name = STRI("Genotypes"),
                .sz = sizeof(genotypesContext),
                .prologue = (prologueCallback) genotypesPrologue,
                .epilogue = (epilogueCallback) genotypesEpilogue,
                .dispose = (disposeCallback) genotypesContextDispose,
                CLII((struct att[])
                {
                    { STRI("option0"), 0, &(handlerContext) { offsetof(genotypesContext, bits), GENOTYPESCONTEXT_BIT_POS_OPTION0 }, (readHandlerCallback) boolHandler },
                    { STRI("option1"), 0, &(handlerContext) { offsetof(genotypesContext, bits), GENOTYPESCONTEXT_BIT_POS_OPTION1 }, (readHandlerCallback) boolHandler },
                    { STRI("option2"), 0, &(handlerContext) { offsetof(genotypesContext, bits), GENOTYPESCONTEXT_BIT_POS_OPTION2 }, (readHandlerCallback) boolHandler },
                    { STRI("option3"), 0, &(handlerContext) { offsetof(genotypesContext, bits), GENOTYPESCONTEXT_BIT_POS_OPTION3 }, (readHandlerCallback) boolHandler },
                    { STRI("path.bed"), offsetof(genotypesContext, path_bed), NULL, (readHandlerCallback) strHandler },
                    { STRI("path.bim"), offsetof(genotypesContext, path_bim), NULL, (readHandlerCallback) strHandler },
                    { STRI("path.fam"), offsetof(genotypesContext, path_fam), NULL, (readHandlerCallback) strHandler }
                }),
                /*CLII((xmlNode[])
                {
                    {// start
                        .name = STRI("ChiSq"),
                        .sz = sizeof(adjustPValueContext),
                        .prologue = (prologueCallback) adjustPValuePrologue,
                        .epilogue = (epilogueCallback) adjustPValueEpilogue,
                        .dispose = (disposeCallback) adjustPValueContextDispose,
                        CLII((struct att[])
                        {
                            { STRI("filename"), offsetof(adjustPValueContext, filename), NULL, (readHandlerCallback) strHandler },
                            { STRI("isAdaptive"), 0, &(handlerContext) { offsetof(adjustPValueContext, bits), ADJUSTPVALUECONTEXT_BIT_POS_ISADAPTIVE }, (readHandlerCallback) boolHandler },
                            { STRI("k"), offsetof(adjustPValueContext, k), &(handlerContext) { offsetof(adjustPValueContext, bits) - offsetof(adjustPValueContext, k), ADJUSTPVALUECONTEXT_BIT_POS_K }, (readHandlerCallback) sizeHandler },
                            { STRI("maxReplications"), offsetof(adjustPValueContext, maxReplications), &(handlerContext) { offsetof(adjustPValueContext, bits) - offsetof(adjustPValueContext, maxReplications), ADJUSTPVALUECONTEXT_BIT_POS_MAXREPLICATIONS }, (readHandlerCallback) sizeHandler },
                            { STRI("topHitsNum"), offsetof(adjustPValueContext, topHitsNum), &(handlerContext) { offsetof(adjustPValueContext, bits) - offsetof(adjustPValueContext, topHitsNum), ADJUSTPVALUECONTEXT_BIT_POS_TOPHITSNUM }, (readHandlerCallback) sizeHandler }
                                //{ STRI("path"), offsetof(genotypesContext, path), NULL, (readHandlerCallback) strHandler }
                        }),
                    },
                })*/
            },
            {
                .name = STRI("MySQL.Fetch"),
                .sz = sizeof(mysqlFetchContext),
                .prologue = (prologueCallback) mysqlFetchPrologue,
                .epilogue = (epilogueCallback) mysqlFetchEpilogue,
                .dispose = (disposeCallback) mysqlFetchContextDispose,
                CLII((struct att[])
                {
                    { STRI("host"), offsetof(mysqlFetchContext, host), NULL, (readHandlerCallback) strHandler },
                    { STRI("login"), offsetof(mysqlFetchContext, login), NULL, (readHandlerCallback) strHandler },
                    { STRI("no.ranks"), 0, &(handlerContext) { offsetof(mysqlFetchContext, bits), MYSQLFETCHCONTEXT_BIT_POS_NORANKS }, (readHandlerCallback) boolHandler },
                    { STRI("password"), offsetof(mysqlFetchContext, password), NULL, (readHandlerCallback) strHandler },
                    { STRI("port"), offsetof(mysqlFetchContext, port), NULL, (readHandlerCallback) uint32Handler },
                    { STRI("schema"), offsetof(mysqlFetchContext, schema), NULL, (readHandlerCallback) strHandler }
                }),
                { dscsch.node, dscsch.cnt }
            }
        })
    };

    crtDbgInit();
    
    logInfo loginfo = { .dev = stderr };
        
    size_t inputcnt;
    char **input = alloca(argc * sizeof *input);
    cmdArgs args = { { .logDef = &loginfo } };
    
    cmdArgsParse(&argsch, &args, argv, argc, input, &inputcnt, &loginfo);

    bool succ = 1;
    if (bitTest(args.bits, CMDARGS_BIT_POS_HELP))
    {
        logMsg(&loginfo, strings[STR_FR_IH], strings[STR_FN]);

        // Help message should be here
    }
    else
    {
        if (!inputcnt) logMsg(&loginfo, strings[STR_FR_ND], strings[STR_FN]);

        for (size_t i = 0; i < inputcnt; i++) // Processing input files
        {
            programObject *obj = programObjectFromXML(&xmlsch, input[i], &loginfo);

            if (obj)
            {
                if (!programObjectExecute(obj, &args.in)) logMsg(&loginfo, strings[STR_FR_WE], strings[STR_FN], input[i]), succ = 0;
                programObjectDispose(obj);
            }
            else logMsg(&loginfo, strings[STR_FR_WC], strings[STR_FN], input[i]);
        }
    }
   
    exit(succ ? EXIT_SUCCESS : EXIT_FAILURE);
}
