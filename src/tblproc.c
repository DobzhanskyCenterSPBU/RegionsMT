#include "np.h"
#include "common.h"
#include "memory.h"
#include "ll.h"
#include "log.h"
#include "tblproc.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

DECLARE_PATH

size_t row_count(FILE *f, int64_t offset, size_t length)
{
    char buff[BLOCK_READ] = { '\0' };
    const int64_t orig = Ftelli64(f);
    int64_t row = 0;
    if (Fseeki64(f, offset, SEEK_SET)) return 0;
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
}

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
        if (Fseeki64(f, offset, SEEK_SET)) return UINT64_MAX; // Bad offset provided

        size_t rd = fread(buff, 1, read, f);
        if (rd != read) return UINT64_MAX; // Never happens?
        while (rd && buff[rd - 1] != '\n') rd--;        
        if (!rd && offset) continue;
        if (Fseeki64(f, offset += rd, SEEK_SET)) return UINT64_MAX; // Never happens?
        break;
    }
    return (uint64_t) offset;
}

/*
struct row_read_handlers {

};
*/

enum row_read_status {
    ROW_READ_STATUS_INVALID_PAR = 0,
    ROW_READ_STATUS_BAD_QUOTES,
    ROW_READ_STATUS_UNHANDLED_VALUE,
    ROW_READ_STATUS_EXPECTED_EOL,
    ROW_READ_STATUS_UNEXPECTED_EOL,
    ROW_READ_STATUS_UNEXPECTED_EOF,
    ROW_READ_STATUS_EXPECTED_MORE_ROWS
};

struct message_error_row_read {
    struct message base;
    enum row_read_status status;
    char *path;
    uint64_t byte;
    size_t row, col;
};

#define MESSAGE_ERROR_ROW_READ(Status, Byte, Row, Col) \
    ((struct message_error_row_read) { \
        .base = MESSAGE(message_error_row_read, MESSAGE_TYPE_ERROR), \
        .status = (Status), \
        .byte = (Byte), \
        .row = (Row), \
        .col = (Col) \
    })

static size_t message_error_row_read(char *buff, size_t buff_cnt, void *Context)
{
    struct message_error_row_read *restrict context = Context;
    const char *str[] = {
        "Invalid parameter",
        "Incorrect order of quotes",
        "Unable to handle value",
        "End of line expected",
        "Unexpected end of line",
        "Unexpected end of file",        
        "Read less rows than expected"
    };
    int res = !(context->byte + 1) || !(context->row + 1) || !(context->col + 1) ?
        snprintf(buff, buff_cnt, "Unable to  %s!\n", str[context->status]) :
        snprintf(buff, buff_cnt, "%s (byte %" PRIu64 ", row %zu, column %zu)!\n", str[context->status], context->byte + 1, context->row + 1, context->col + 1);
    return MAX(0, res);
}

bool tbl_read(const char *path, tbl_selector_callback selector, tbl_selector_callback selector_eol, void *context, void *res, size_t *p_row_skip, size_t *p_row_cnt, size_t *p_length, char delim, struct log *log)
{
    size_t row_skip = p_row_skip ? *p_row_skip : 0, row_cnt = p_row_cnt ? *p_row_cnt : 0, length = p_length ? *p_length : 0;

    // This guarantees that all conditions of type 'byte + rd < length' will be eventually triggered.
    if (length && length > SIZE_MAX - BLOCK_READ)
    {
        log_message(log, &MESSAGE_ERROR_ROW_READ(ROW_READ_STATUS_INVALID_PAR, UINT64_MAX, SIZE_MAX, SIZE_MAX).base);
        return 0;
    }

    FILE *f = fopen(path, "rt");
    if (!f)
    {
        log_message(log, &MESSAGE_ERROR_CRT(errno).base);
        return 0;
    }

    bool succ = 0;
    char buff[BLOCK_READ] = { '\0' }, *temp_buff = NULL;
    size_t rd = fread(buff, 1, sizeof(buff), f), ind = 0, skip = row_skip;
    uint64_t byte = 0;

    for (; rd; byte += rd, rd = fread(buff, 1, sizeof(buff), f))
    {
        for (char *ptr = memchr(buff, '\n', rd); skip && ptr && (!length || (size_t) (ptr - buff) < (size_t) (length - byte)); skip--, ind = ptr - buff + 1, ptr = memchr(ptr + 1, '\n', buff + rd - ptr - 1));
        if (!skip || (length && byte + rd >= length)) break;
    }
    if (p_row_skip) *p_row_skip = row_skip - skip;

    size_t row = 0, col = 0, len = 0, cap = 0;
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
                    if (!selector(&cl, row, col, res, context))
                        log_message(log, &MESSAGE_ERROR_ROW_READ(ROW_READ_STATUS_EXPECTED_EOL, byte + ind, row + row_skip, col).base);
                    else
                    {
                        if (cl.handler.read)
                        {
                            temp_buff[len] = '\0';
                            if (!cl.handler.read(temp_buff, len, cl.ptr, cl.context))
                                log_message(log, &MESSAGE_ERROR_ROW_READ(ROW_READ_STATUS_UNHANDLED_VALUE, byte + ind, row + row_skip, col).base);
                        }
                        quote = 0;
                        len = 0;
                        col++;
                        continue;
                    }
                    goto error;
                }
            }
            else switch (buff[ind])
            {
            default:
                if (quote == 1) log_message(log, &MESSAGE_ERROR_ROW_READ(ROW_READ_STATUS_BAD_QUOTES, byte + ind, row + row_skip, col).base);
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
                    log_message(log, &MESSAGE_ERROR_ROW_READ(ROW_READ_STATUS_BAD_QUOTES, byte + ind, row + row_skip, col).base);
                    goto error;
                case 2: 
                    break;
                }
                break;
            case '\n':
                if (quote == 2) log_message(log, &MESSAGE_ERROR_ROW_READ(ROW_READ_STATUS_BAD_QUOTES, byte + ind, row + row_skip, col).base);
                else
                {
                    if (!selector_eol(&cl, row, col, res, context))
                        log_message(log, &MESSAGE_ERROR_ROW_READ(ROW_READ_STATUS_UNEXPECTED_EOL, byte + ind, row + row_skip, col).base);
                    else
                    {
                        if (cl.handler.read)
                        {
                            temp_buff[len] = '\0';
                            if (!cl.handler.read(temp_buff, len, cl.ptr, cl.context))
                                log_message(log, &MESSAGE_ERROR_ROW_READ(ROW_READ_STATUS_UNHANDLED_VALUE, byte + ind, row + row_skip, col).base);
                        }
                        quote = 0;
                        len = col = 0;
                        row++;
                        continue;
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
            if (!array_test(&temp_buff, &cap, 1, 0, 0, ARG_SIZE(len, 2))) log_message(log, &MESSAGE_ERROR_CRT(errno).base);
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

    if (col) log_message(log, &MESSAGE_ERROR_ROW_READ(ROW_READ_STATUS_UNEXPECTED_EOF, byte + ind, row + row_skip, col).base);
    else if (row_cnt && row < row_cnt) log_message(log, &MESSAGE_ERROR_ROW_READ(ROW_READ_STATUS_EXPECTED_MORE_ROWS, byte + ind, row + row_skip, col).base);
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

bool tbl_selector(struct tbl_col *cl, size_t row, size_t col, void *tbl, void *Context)
{
    return 1;
}

struct tbl_head_context {
    size_t tmp;
};

bool tbl_head_handler(const char *str, size_t len, void *ptr, void *context)
{
    return 0;
}

bool tbl_head_selector(struct tbl_col *cl, size_t row, size_t col, void *tbl, void *Context)
{
    (void) row;
    *cl = (struct tbl_col) {
        .handler = tbl_head_handler,
        .ptr = NULL,
    };
    return 0;
}


/*
struct tbl_sch *tbl_sch_from_text(FILE *f, tbl_selector sel, tbl_finalizer finalizer, char delim, void *context)
{
    char buff[BLOCK_READ] = { '\0' }, *temp_buff = NULL;
    size_t ind = 0, byte = 0, col = 0, len = 0, cap = 0;
    bool row = 0;
    uint8_t quote = 0;
    rowReadErr err = ROWREAD_SUCC;
    
    tblsch *ressch = calloc(1, sizeof *ressch);
    size_t resschcap = 0;
    
    if (!ressch) { err = ROWREAD_ERR_MEM; goto ERR(); }
    
    for (size_t rd = fread(buff, 1, sizeof buff, f); rd && !row; byte += rd, rd = fread(buff, 1, sizeof buff, f), ind = 0)
    {
        for (; ind < rd && !row; ind++)
        {
            if (buff[ind] == delim)
            {
                if (quote != 2)
                {
                    if (!dynamicArrayTest((void **) &temp_buff, &cap, 1, len + 1)) { err = ROWREAD_ERR_MEM; goto ERR(); }
                    temp_buff[len] = '\0';
            
                    size_t colpos = SIZE_MAX, colind = sel(temp_buff, len, &colpos, context);
                    
                    if (colind < sch->colschcnt)
                    {
                        if (!dynamicArrayTest((void **) &ressch->colsch, &resschcap, sizeof *ressch->colsch, ressch->colschcnt + 1)) { err = ROWREAD_ERR_MEM; goto ERR(); }
                        sch->colsch[ressch->colschcnt] = sch->colsch[colind];
                        if (colpos + 1) sch->colsch[ressch->colschcnt].ind = colpos;
                        
                        ressch->colschcnt++;
                        quote = 0;
                        len = 0;
                        col++;
                    }
                    else { err = ROWREAD_ERR_FORM; goto ERR(); }
                    
                    continue;
                }
            }
            else switch (buff[ind])
            {
            default:
                if (quote == 1) { err = ROWREAD_ERR_QUOT; goto ERR(); }
                break;
                    
            case ' ':
            case '\t': // In 'tab separated value' mode this is overridden
                switch (quote)
                {
                case 0:
                    if (len) break;
                    continue;
                        
                case 1:
                    err = ROWREAD_ERR_QUOT;
                    goto ERR();
                        
                case 2: break;
                }
                break;
                    
            case '\n':
                if (quote != 2)
                {
                    if (!dynamicArrayTest((void **) &temp_buff, &cap, 1, len + 1)) { err = ROWREAD_ERR_MEM; goto ERR(); }
                    temp_buff[len] = '\0';

                    size_t colpos = SIZE_MAX, colind = sel(temp_buff, len, &colpos, context);
                   
                    if (colind < sch->colschcnt)
                    {
                        if (!dynamicArrayTest((void **) &ressch->colsch, &resschcap, sizeof *ressch->colsch, ressch->colschcnt + 1)) { err = ROWREAD_ERR_MEM; goto ERR(); }
                        sch->colsch[ressch->colschcnt] = sch->colsch[colind];
                        if (colpos + 1) sch->colsch[ressch->colschcnt].ind = colpos;
                        
                        quote = 0;
                        len = col = 0;
                        row = 1;
                    }
                    else { err = ROWREAD_ERR_FORM; goto ERR(); }

                    continue;
                }
                else { err = ROWREAD_ERR_QUOT; goto ERR(); }               
                    
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
            
            if (!dynamicArrayTest((void **) &temp_buff, &cap, 1, len + 1)) { err = ROWREAD_ERR_MEM; goto ERR(); }
            temp_buff[len++] = buff[ind];
        }
    }
    
    if (!dynamicArrayFinalize((void **) &ressch->colsch, &resschcap, sizeof *ressch->colsch, ressch->colschcnt)) goto ERR();
    
    for (;;)
    {
        break;
    
    ERR():
        tblschDispose(ressch);
        ressch = NULL;
        break;
    }
    
    free(temp_buff);
    if (res) *res = (rowReadRes) { .read = 1, .col = col, .byte = byte + ind, .err = err };
    
    return ressch;
}
*/

///////////////////////////////////////////////////////////////////////////////

/*
bool rowWrite(FILE *f, tblsch *sch, void **tbl, void **context, size_t rowskip, size_t rowread, size_t byteread, rowReadRes *res)
{
    return 0;
}
*/
