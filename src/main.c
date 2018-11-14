#include "np.h"
#include "main.h"
#include "argv.h"
#include "memory.h"
#include "utf8.h"

#include "module_categorical.h"
#include "module_lde.h"

#include <stdlib.h>
#include <string.h>

#include <math.h>
#include <float.h>

#include <gsl/gsl_sf.h>
#include "gslsupp.h"

#include "sort.h"

#ifndef TEST_DEACTIVATE

#   include "test.h"
#   include "test_ll.h"
#   include "test_np.h"
#   include "test_sort.h"
#   include "test_utf8.h"

static bool test_main(struct log *log)
{
    const struct test_group group_arr[] = {
        {
            NULL,
            sizeof(struct test_ll_a),
            CLII((test_generator_callback[]) {
                test_ll_generator_a,
            }),
            CLII((test_callback[]) {
                test_ll_a_1,
                test_ll_a_2,
                test_ll_a_3
            })
        },
        {
            NULL,
            sizeof(struct test_ll_a),
            CLII((test_generator_callback[]) {
                test_ll_generator_b,
            }),
            CLII((test_callback[]) {
                test_ll_b,
            })
        },
        {
            test_np_disposer_a,
            sizeof(struct test_np_a),
            CLII((test_generator_callback[]) {
                test_np_generator_a,
            }),
            CLII((test_callback[]) {
                test_np_a,
            })
        },
        {
            test_sort_disposer_a,
            sizeof(struct test_sort_a),
            CLII((test_generator_callback[]) {
                //test_sort_generator_a_1,
                //test_sort_generator_a_2,
                test_sort_generator_a_3
            }),
            CLII((test_callback[]) {
                test_sort_a,
            })
        },
        {
            test_sort_disposer_b,
            sizeof(struct test_sort_b),
            CLII((test_generator_callback[]) {
                test_sort_generator_b_1
            }),
            CLII((test_callback[]) {
                test_sort_b_1,
                test_sort_b_2
            })
        },
        {
            test_sort_disposer_c,
            sizeof(struct test_sort_c),
            CLII((test_generator_callback[]) {
                test_sort_generator_c_1
            }),
            CLII((test_callback[]) {
                test_sort_c_1,
                test_sort_c_2
            })
        },
        {
            NULL,
            sizeof(struct test_utf8),
            CLII((test_generator_callback[]) {
                test_utf8_generator,
            }),
            CLII((test_callback[]) {
                test_utf8_len,
                test_utf8_encode,
                test_utf8_decode,
                test_utf16_encode,
                test_utf16_decode
            })
        }
    };
    log_message_generic(log, CODE_METRIC, MESSAGE_NOTE, "Test mode triggered!\n");
    return test(group_arr, countof(group_arr), log);
}

#else

static bool test_main(struct log *log)
{
    log_message_generic(log, CODE_METRIC, MESSAGE_NOTE, "Test mode deactivated!\n");
    return 1;
}

#endif

// Main routine arguments management
struct main_args main_args_default()
{
    struct main_args res = { .thread_cnt = get_processor_count() };
    uint8_bit_set(res.bits, MAIN_ARGS_BIT_POS_THREAD_CNT);
    return res;
}

struct main_args main_args_override(struct main_args args_hi, struct main_args args_lo)
{
    struct main_args res = { .log_path = args_hi.log_path ? args_hi.log_path : args_lo.log_path };
    if (res.log_path && !strcmp(res.log_path, "stderr")) res.log_path = NULL;
    memcpy(res.bits, args_hi.bits, UINT8_CNT(MAIN_ARGS_BIT_CNT));
    if (uint8_bit_test(args_hi.bits, MAIN_ARGS_BIT_POS_THREAD_CNT)) res.thread_cnt = args_hi.thread_cnt;
    else
    {
        res.thread_cnt = args_lo.thread_cnt;
        uint8_bit_set(res.bits, MAIN_ARGS_BIT_POS_THREAD_CNT);
    }
    return res;
}

// Main routine (see below for actual entry point)
static int Main(int argc, char **argv) 
{
    /*const char *strings[] = {
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
    */

    // All names should be sorted according to 'strncmp'!!!
    struct argv_par_sch argv_par_sch =
    {
        CLII((struct tag[]) { { STRI("help"), 0 }, { STRI("log"), 1 }, { STRI("test"), 2 }, { STRI("threads"), 3 }}),
        CLII((struct tag[]) { { STRI("C"), 4 }, { STRI("L"), 5 }, { STRI("T"), 2 }, { STRI("h"), 0 }, { STRI("l"), 1 }, { STRI("t"), 3 } }),
        CLII((struct par_sch[])
        {
            { 0, &(struct handler_context) { offsetof(struct main_args, bits), MAIN_ARGS_BIT_POS_HELP }, empty_handler, 1 },
            { offsetof(struct main_args, log_path), NULL, p_str_handler, 0 },
            { 0, &(struct handler_context) { offsetof(struct main_args, bits), MAIN_ARGS_BIT_POS_TEST }, empty_handler, 1 },
            { offsetof(struct main_args, thread_cnt), &(struct handler_context) { offsetof(struct main_args, bits), MAIN_ARGS_BIT_POS_THREAD_CNT }, size_handler, 0 },
            { 0, &(struct handler_context) { offsetof(struct main_args, bits), MAIN_ARGS_BIT_POS_CAT }, empty_handler, 1 },
            { 0, &(struct handler_context) { offsetof(struct main_args, bits), MAIN_ARGS_BIT_POS_LDE }, empty_handler, 1 },
        })
    };

    /*
    
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
                        { STRI("threshold"), offsetof(dReportContext, threshold), &(handlerContext) { offsetof(dReportContext, bits) - offsetof(dReportContext, threshold), DREPORTCONTEXT_BIT_POS_THRESHOLD }, (readHandlerCallback) float64Handler }
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
                { dscsch.node, dscsch.cnt }
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

    */
    
    //FILE *f = stderr; // fopen("f:\\test\\h", "w");
    //fputs("\xef\xbb\xbf", f);
    //for (size_t i = 0; i < (size_t) argc; i++) fprintf(stderr, "%s\n", argv[i]);
    //fclose(f);

    struct style style = {
        .ttl = { INIT_ENV_COL(FG_GREEN), INIT_ENV_COL(FG_RED), INIT_ENV_COL(FG_YELLOW), INIT_ENV_COL(FG_MAGENTA), INIT_ENV_COL(FG_CYAN) },
        .ts = INIT_ENV_COL(FG_BR_BLACK),
        .src = INIT_ENV_COL(FG_BR_BLACK),
        .str = INIT_ENV_COL(FG_BR_MAGENTA),
        .num = INIT_ENV_COL(FG_BR_CYAN),
        .time = INIT_ENV_COL(FG_BR_YELLOW)
    };

    struct log log;
    if (log_init(&log, NULL, 1, 0, style, NULL))
    {
        size_t pos_cnt;
        char **pos_arr;
        struct main_args main_args = { 0 };
        if (argv_parse(argv_par_selector, &argv_par_sch, &main_args, argv, argc, &pos_arr, &pos_cnt, &log))
        {
            main_args = main_args_override(main_args, main_args_default());
            if (uint8_bit_test(main_args.bits, MAIN_ARGS_BIT_POS_HELP))
            {
                // Help mode
                log_message_generic(&log, CODE_METRIC, MESSAGE_INFO, "Help mode triggered!\n");
            }
            else if (uint8_bit_test(main_args.bits, MAIN_ARGS_BIT_POS_TEST))
            {
                test_main(&log);
            }
            else if (uint8_bit_test(main_args.bits, MAIN_ARGS_BIT_POS_CAT))
            {
                if (pos_cnt >= 6)
                {
                    size_t rpl = (size_t) strtoull(pos_arr[4], NULL, 10);
                    uint64_t seed = (uint64_t) strtoull(pos_arr[5], NULL, 10);
                    categorical_run(pos_arr[0], pos_arr[1], pos_arr[2], pos_arr[3], rpl, seed, &log);
                }
            }
            else if (uint8_bit_test(main_args.bits, MAIN_ARGS_BIT_POS_LDE))
            {
                if (pos_cnt >= 2) lde_run(pos_arr[0], pos_arr[1], &log);
            }
            else
            {
                if (!pos_cnt) log_message_generic(&log, CODE_METRIC, MESSAGE_NOTE, "No input data specified.\n");
                else
                {
                    
                }
            }
            free(pos_arr);
        }
        log_close(&log);
    }
    
    /*   
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
                if (!programObjectExecute(obj, &args.in)) logMsg(&loginfo, strings[STR_FR_WE], strings[STR_FN], input[i]);
                programObjectDispose(obj);
            }
            else logMsg(&loginfo, strings[STR_FR_WC], strings[STR_FN], input[i]);
        }
    }
    */
   
    return EXIT_SUCCESS;
}

#if _WIN32
#   include <windows.h>

int wmain(int argc, wchar_t **wargv)
{
    // Memory leaks will be reported at the program exit
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);

    // Making console output UTF-8 friendly
    if (!SetConsoleOutputCP(CP_UTF8)) return EXIT_FAILURE;
    
    int main_res = EXIT_FAILURE;

    // Translating UTF-16 command-line parameters to UTF-8
    char **argv;
    if (array_init(&argv, NULL, argc, sizeof(*argv), 0, ARRAY_STRICT))
    {
        size_t base_cnt = 0, i;
        for (i = 0; i < (size_t) argc; i++) // Determining total length and performing error checking
        {
            wchar_t *word = wargv[i];
            uint32_t val;
            uint8_t context = 0;
            for (; *word; word++)
            {
                if (!utf16_decode((uint16_t) *word, &val, NULL, NULL, &context)) break;
                if (!context) base_cnt += utf8_len(val);
            }
            if (*word) break;
            else base_cnt++;
        }
        if (i == (size_t) argc)
        {
            char *base;
            if (array_init(&base, NULL, base_cnt, sizeof(*base), 0, ARRAY_STRICT))
            {
                char *byte = base;
                for (i = 0; i < (size_t) argc; i++) // Performing translation
                {
                    argv[i] = byte;
                    wchar_t *word = wargv[i];
                    uint32_t val;
                    uint8_t context = 0;
                    for (; *word; word++)
                    {
                        if (!utf16_decode((uint16_t) *word, &val, NULL, NULL, &context)) break;
                        if (!context)
                        {
                            uint8_t len;
                            utf8_encode(val, (uint8_t *) byte, &len);
                            byte += len;
                        }
                    }
                    if (*word) break;
                    else *(byte++) = '\0';
                }
                if (i == (size_t) argc) main_res = Main(argc, argv);
                free(base);
            }
        }
        free(argv);
    }
    return main_res;
}

#else

int main(int argc, char **argv)
{
    return Main(argc, argv);
}

#endif
