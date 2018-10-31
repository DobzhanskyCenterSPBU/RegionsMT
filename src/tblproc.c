#include "np.h"
#include "common.h"
#include "memory.h"
#include "ll.h"
#include "log.h"
#include "strproc.h"
#include "tblproc.h"
#include "utf8.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

DECLARE_PATH

enum row_read_status {
    TBL_STATUS_UNHANDLED_VALUE = 0,
    TBL_STATUS_BAD_QUOTES,
    TBL_STATUS_EXPECTED_EOL,
    TBL_STATUS_UNEXPECTED_EOL,
    TBL_STATUS_UNEXPECTED_EOF,
    TBL_STATUS_EXPECTED_MORE_ROWS,
    TBL_STATUS_UNHANDLED_EOL
};

struct row_read_context {
    const char *path;
    char *str;
    size_t len;
    struct text_metric metric;
    enum row_read_status status;
};

static bool message_error_tbl_read(char *buff, size_t *p_buff_cnt, void *Context)
{
    struct row_read_context *restrict context = Context;
    const char *fmt[] = {
        "Unable to handle the value \"%.*s\"",
        "Incorrect order of quotes",
        "End of line expected",
        "Unexpected end of line",
        "Unexpected end of file",
        "Read less rows than expected",
        "Unable to handle line"
    };
    if (context->status >= countof(fmt)) return 0;
    size_t cnt = 0, len = *p_buff_cnt;
    for (unsigned i = 0;; i++)
    {
        int tmp = -1;
        switch (i)
        {
        case 0:
            switch (context->status)
            {
            case TBL_STATUS_UNHANDLED_VALUE:
                tmp = snprintf(buff + cnt, len, fmt[context->status], INTP(context->len), context->str);
                break;
            default:
                tmp = snprintf(buff + cnt, len, "%s", fmt[context->status]);
            }
            break;
        case 1:
            tmp = snprintf(buff + cnt, len, " in the file \"%s\" (line: %zu; character: %zu; byte: %" PRIu64 ")!\n",
                context->path,
                context->metric.row + 1,
                context->metric.col + 1,
                context->metric.byte + 1
            );
            break;
        }
        if (tmp < 0) return 0;
        len = size_sub_sat(len, (size_t) tmp);
        if (i == 1) break;
        cnt = size_add_sat(cnt, (size_t) tmp);
    }
    *p_buff_cnt = cnt;
    return 1;
}

static bool log_message_error_tbl_read(struct log *restrict log, struct code_metric code_metric, const char *path, struct text_metric metric, char *str, size_t len, enum row_read_status status)
{
    return log_message(log, code_metric, MESSAGE_ERROR, message_error_tbl_read, &(struct row_read_context) { .path = path, .str = str, .len = len, .metric = metric, .status = status });
}

bool dsv_index(const char *path, uint64_t **p_ind, size_t *p_cnt, struct log *log)
{
    char buff[MAX(BLOCK_READ, countof(UTF8_BOM))] = { '\0' };
    FILE *f = fopen(path, "rb");
    do {
        if (!f) log_message_fopen(log, CODE_METRIC, MESSAGE_ERROR, path, errno);
        else break;
        return 0;
    } while (0);
    uint64_t byte = 0;
    size_t rd = fread(buff, 1, sizeof(buff), f), pos = 0, cnt = 0;
    if (rd >= strlenof(UTF8_BOM) && !strncmp(buff, UTF8_BOM, strlenof(UTF8_BOM))) pos += strlenof(UTF8_BOM), byte += strlenof(UTF8_BOM);
    bool halt = 0;
    unsigned quot = 0, line = 1;
    *p_ind = NULL;
    *p_cnt = 0;
    for (; !halt && rd; rd = fread(buff, 1, sizeof(buff), f), pos = 0) for (halt = 1; pos < rd; halt = 1)
    {
        if (line)
        {
            if (line == 2)
            {
                if (buff[pos] == '\n')
                {
                    pos++;
                    line--;
                    if (byte + 1 < byte) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, ERANGE);
                    else
                    {
                        halt = 0;
                        byte++;
                        continue;
                    }
                    break;
                }
            }
            line = 0;
            if (!array_test(p_ind, p_cnt, 1, sizeof(*p_ind), 0, ARG_SIZE(cnt, 1))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
            else (*p_ind)[cnt] = byte, halt = 0;
        }
        else halt = 0;
        if (halt) break;
        if (quot == 2)
        {
            if (buff[pos] == '\"')
            {
                quot--;
                pos++;
                continue;
            }
            else quot = 0;
        }
        size_t tmp = strcspn(buff + pos, "\n\r\""), diff = tmp - pos + 1;
        if (byte + diff < byte) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, ERANGE);
        else
        {
            byte += diff;
            pos = tmp + 1;
            if (buff[tmp] == '\"') quot++;
            else if (!quot)
            {
                if (buff[tmp] == '\n') line = 1;
                else if (buff[tmp] == '\r') line = 2;
            }
            continue;
        }
        halt = 1;
        break;
    }
    if (!halt) return 1;
    free(p_ind);
    *p_cnt = 0;
    return 0;
}

/*
bool dsv_read(const char *path, uint64_t *ind, size_t off, size_t cnt, tbl_selector_callback selector, tbl_eol_callback eol, void *context, void *res, struct log *log)
{
    char buff[MAX(BLOCK_READ, countof(UTF8_BOM))] = { '\0' };
    FILE *f = fopen(path, "rb");
    if (!f) log_message_fopen(log, CODE_METRIC, MESSAGE_ERROR, path, errno);
    else return 0;
    if (Fseeki64(f, ind[off], SEEK_CUR)) log_message_fseek(log, CODE_METRIC, MESSAGE_ERROR, ind[off], path);
}
*/

enum newline { NEWLINE_LF = 0, NEWLINE_CR, NEWLINE_CR_LF };

// Scans forward to the new line
char *str_newline_forward(const char *buff, size_t len, enum newline newline)
{
    char *res = NULL;
    switch (newline)
    {
    case NEWLINE_LF:
        res = memchr(buff, '\n', len);
        break;
    case NEWLINE_CR:
        res = memchr(buff, '\r', len);
        break;
    case NEWLINE_CR_LF:
        for (char *ptr = memchr(buff, '\r', len); ptr && (size_t) (ptr - buff) < len - 1; ptr = memchr(ptr + 1, '\r', len)) if (ptr[1] == '\n') return ptr + 2;
        return NULL;
    }
    return res ? res + 1 : NULL;
}

// Scans backwards to the new line
/*char *str_newline_backward(const char *buff, size_t len, enum newline newline)
{
    char *res = NULL;
    switch (newline)
    {
    case NEWLINE_LF:
        res = memchr(buff, '\n', len);
        break;
    case NEWLINE_CR:
        res = memchr(buff, '\r', len);
        break;
    case NEWLINE_CR_LF:
        for (char *ptr = memchr(buff, '\r', len); ptr && (ptr - buff) < len - 1; ptr = memchr(ptr + 1, '\r', len)) if (ptr[1] == '\n') return ptr + 2;
        return NULL;
    }
    return res ? res + 1 : NULL;
}

size_t row_count(FILE *f, int64_t offset, size_t length, enum newline newline)
{
    char buff[BLOCK_READ] = { '\0' };
    int64_t orig = Ftelli64(f), row = 0;
    if (Fseeki64(f, offset, SEEK_CUR)) return 0;
    for (size_t rd = fread(buff, 1, sizeof buff, f); rd; rd = fread(buff, 1, sizeof buff, f))
    {
        for (char *ptr = memchr(buff, '\n', rd); ptr && (!length || (size_t) (ptr - buff) < length); row++, ptr = memchr(ptr + 1, '\n', buff + rd - ptr - 1));
        if (length)
        {
            if (rd < length) length -= rd;
            else break;
        }
    }
    if (Fseeki64(f, orig, SEEK_SET)) return 0;
    return (size_t) MIN(row, SIZE_MAX);
}*/

// Back searches a number of bytes in order to align to the begin of current line.
// Returns '-1' on error and the correct offset on success.
uint64_t row_align(FILE *f, int64_t offset)
{
    char buff[BLOCK_READ] = { '\0' };
    for (;;)
    {
        size_t read = countof(buff);
        if (offset > countof(buff)) offset -= read;
        else read = (size_t) offset, offset = 0;
        if (Fseeki64(f, offset, SEEK_CUR)) return UINT64_MAX; // Bad offset provided

        size_t rd = fread(buff, 1, read, f);
        if (rd != read) return UINT64_MAX; // Never happens?

        while (rd && buff[rd - 1] != '\n') rd--;        
        if (!rd && offset) continue;
        
        offset += rd;
        if (rd < read && Fseeki64(f, -(int64_t) (read - rd), SEEK_CUR)) return UINT64_MAX; // Never happens?
        break;
    }
    return (uint64_t) offset;
}

/*
struct row_read_handlers {

};
*/



static FILE *tbl_read_io(const char *path, struct log *log)
{
    FILE *f = fopen(path, "rb");
    if (!f) log_message_fopen(log, CODE_METRIC, MESSAGE_ERROR, path, errno);
    else
    {

    }
}

struct tbl_symbols {
    char delim, quote;
};

bool tbl_read(const char *path, int64_t offset, tbl_selector_callback selector, tbl_eol_callback eol, void *context, void *res, size_t *p_row_skip, size_t *p_row_cnt, size_t *p_length, char delim, struct log *log)
{
    struct text_metric metric = { 0 };
    
    bool succ = 0;
    size_t row_skip = p_row_skip ? *p_row_skip : 0, row_cnt = p_row_cnt ? *p_row_cnt : 0, length = p_length ? *p_length : 0;

    // This guarantees that all conditions of type 'byte + rd < length' will be eventually triggered.
    if (length && length > SIZE_MAX - BLOCK_READ) log_message_generic(log, CODE_METRIC, MESSAGE_ERROR, "Invalid value of the parameter!\n");
    else succ = 1;

    if (!succ) return 0;
    succ = 0;
    FILE *f = fopen(path, "rb");
    if (!f) log_message_fopen(log, CODE_METRIC, MESSAGE_ERROR, path, errno);
    else succ = 1;

    if (!succ) return 0;
    succ = 0;
    char buff[BLOCK_READ] = { '\0' }, *temp_buff = NULL;
    if (offset && Fseeki64(f, offset, SEEK_CUR)) log_message_fseek(log, CODE_METRIC, MESSAGE_ERROR, offset, path);
    else succ = 1;
    
    size_t rd = fread(buff, 1, sizeof(buff), f), ind = 0, skip = row_skip;
    uint64_t byte = 0;
    size_t row = 0, col = 0, len = 0, cap = 0;
    
    if (!succ) goto error;
    succ = 0;
    for (; rd; byte += rd, rd = fread(buff, 1, sizeof(buff), f))
    {
        for (char *ptr = memchr(buff, '\n', rd); skip && ptr && (!length || (size_t) (ptr - buff) < (size_t) (length - byte)); skip--, ind = ptr - buff + 1, ptr = memchr(ptr + 1, '\n', buff + rd - ptr - 1));
        if (skip && (!length || byte + rd < length)) continue;
        break;
    }
    if (p_row_skip) *p_row_skip = row_skip - skip;

    uint8_t quote = 0;
    struct tbl_col cl;
    for (; rd; byte += rd, rd = fread(buff, 1, sizeof(buff), f), ind = 0)
    {
        for (; ind < rd && (!row_cnt || row < row_cnt) && (!length || byte + ind < length); ind++)
        {
            if (buff[ind] == delim)
            {
                if (quote != 2)
                {
                    if (!selector(&cl, row, col, res, context)) log_message_error_tbl_read(log, CODE_METRIC, path, metric, NULL, 0, TBL_STATUS_EXPECTED_EOL);
                    else
                    {
                        if (cl.handler.read)
                        {
                            temp_buff[len] = '\0';
                            if (!cl.handler.read(temp_buff, len, cl.ptr, cl.context)) log_message_error_tbl_read(log, CODE_METRIC, path, metric, temp_buff, len, TBL_STATUS_UNHANDLED_VALUE);
                            else succ = 1;
                        }
                        else succ = 1;

                        if (succ)
                        {
                            succ = 0;
                            quote = 0;
                            len = 0;
                            col++;
                            continue;
                        }
                    }
                    goto error;
                }
            }
            else switch (buff[ind])
            {
            default:
                if (quote == 1) log_message_error_tbl_read(log, CODE_METRIC, path, metric, NULL, 0, TBL_STATUS_BAD_QUOTES);
                else break;
                goto error;
            case ' ':
            case '\t': // In 'tab separated value' mode this is overridden
                switch (quote)
                {
                case 0:
                    if (len) break;
                    continue;
                case 1:
                    log_message_error_tbl_read(log, CODE_METRIC, path, metric, NULL, 0, TBL_STATUS_BAD_QUOTES);
                    goto error;
                case 2:
                    break;
                }
                break;
            case '\n':
                if (quote == 2) log_message_error_tbl_read(log, CODE_METRIC, path, metric, NULL, 0, TBL_STATUS_BAD_QUOTES);
                else
                {
                    if (!selector(&cl, row, col, res, context)) log_message_error_tbl_read(log, CODE_METRIC, path, metric, NULL, 0, TBL_STATUS_UNEXPECTED_EOL);
                    else
                    {
                        if (cl.handler.read)
                        {
                            temp_buff[len] = '\0';
                            if (!cl.handler.read(temp_buff, len, cl.ptr, cl.context)) log_message_error_tbl_read(log, CODE_METRIC, path, metric, temp_buff, len, TBL_STATUS_UNHANDLED_VALUE);
                            else succ = 1;
                        }
                        else succ = 1;

                        if (succ)
                        {
                            succ = 0;
                            if (eol && !eol(row, col, res, context)) log_message_error_tbl_read(log, CODE_METRIC, path, metric, NULL, 0, TBL_STATUS_UNHANDLED_EOL);
                            else
                            {
                                quote = 0;
                                len = col = 0;
                                row++;
                                continue;
                            }
                        }
                    }
                }
                goto error;
            case '\"':
                switch (quote)
                {
                case 0:
                    if (len) break;
                    quote = 2;
                    continue;
                case 1:
                    quote++;
                    break;
                case 2:
                    quote--;
                    continue;
                }
                break;
            }
            if (!array_test(&temp_buff, &cap, sizeof(*temp_buff), 0, 0, ARG_SIZE(len, 2))) log_message_crt(log, CODE_METRIC, MESSAGE_ERROR, errno);
            else
            {
                temp_buff[len++] = buff[ind];
                continue;
            }
            goto error;
        }

        if ((!row_cnt || row < row_cnt) && (!length || byte + rd < length)) continue;
        break;
    }

    if (col) log_message_error_tbl_read(log, CODE_METRIC, path, metric, NULL, 0, TBL_STATUS_UNEXPECTED_EOF);
    else if (row_cnt && row < row_cnt) log_message_error_tbl_read(log, CODE_METRIC, path, metric, NULL, 0, TBL_STATUS_EXPECTED_MORE_ROWS);
    else succ = 1;

error:
    if (p_length) *p_length = (size_t) MIN(byte + ind, SIZE_MAX);
    if (p_row_cnt) *p_row_cnt = row;
    free(temp_buff);
    fclose(f);
    return succ;
}


//struct tbl_sch {
//
//};

/*
bool tbl_selector(struct tbl_col *cl, size_t row, size_t col, void *tbl, void *Context)
{
    return 1;
}

struct tbl_head_context {
    size_t tmp;
};

bool tbl_head_handler(const char *fmt, size_t len, void *ptr, void *context)
{
    return 0;
}

bool tbl_head_selector(struct tbl_col *cl, size_t row, size_t col, void *tbl, void *Context)
{
    (void) row;
    *cl = (struct tbl_col) {
        .handler = { . read = tbl_head_handler },
        .ptr = NULL,
    };
    return 0;
}

bool row_write(const char *path, int64_t offset, tbl_selector_callback selector, tbl_eol_callback eol, void *context, void *res, size_t *p_row_skip, size_t *p_row_cnt, size_t *p_length, char delim, struct log *log)
{
    return 0;
}
*/
