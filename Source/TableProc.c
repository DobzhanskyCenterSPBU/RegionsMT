#include "Common.h"
#include "Debug.h"
#include "Memory.h"
#include "TableProc.h"
#include "Wrappers.h"

#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////

void tblClose(void **tbl, tblsch *sch)
{
    if (tbl) for (size_t i = 0; i < sch->colschcnt; free(tbl[sch->colsch[i++].ind]));
}

bool tblInit(void **tbl, tblsch *sch, size_t rowcnt, bool zer)
{
    // Initialization with zeros is required only for the sake of proper disposal
    if (zer) for (size_t i = 0; i < sch->colschcnt; tbl[sch->colsch[i++].ind] = NULL);

    for (size_t i = 0; i < sch->colschcnt; i++) if (sch->colsch[i].handler.read && sch->colsch[i].handler.write)
    {
        void *col = malloc(sch->colsch[i].size * rowcnt);
        if (!col) goto ERR();

        tbl[sch->colsch[i].ind] = col;
    }

    return 1;

 ERR():
    tblClose(tbl, sch);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////

size_t rowCount(FILE *f, size_t offset, size_t length)
{
    char buff[BLOCK_READ] = { '\0' };
    const size_t orig = ftell64(f);
    size_t row = 0;

    if (fseek64(f, offset, SEEK_SET)) goto ERR();

    for (size_t rd = fread(buff, 1, sizeof buff, f); rd; rd = fread(buff, 1, sizeof buff, f))
    {
        for (char *ptr = memchr(buff, '\n', rd); ptr && ptr < buff + length; row++, ptr = memchr(ptr + 1, '\n', buff + rd - ptr - 1));
        if (rd < length) length -= rd;
        else break;
    }

    if (fseek64(f, orig, SEEK_SET)) goto ERR();
    return row;

ERR():
    return 0;
}

// Back searches a number of bytes in order to align to the begin of current line.
// Returns '-1' on error and the correct offset on success.
size_t rowAlign(FILE *f, size_t offset)
{
    char buff[BLOCK_READ] = { '\0' };

    for (;;)
    {
        size_t read = sizeof buff;

        if (offset > sizeof buff) offset -= read;
        else read = offset, offset = 0;

        if (fseek64(f, offset, SEEK_SET)) goto ERR(); // Bad offset provided

        size_t rd = fread(buff, 1, read, f);
        if (rd != read) goto ERR(); // Never happens?

        while (rd && buff[rd - 1] != '\n') rd--;

        if (!rd && offset) continue;

        if (fseek64(f, offset += rd, SEEK_SET)) goto ERR(); // Never happens?
        break;
    }

    return offset;

ERR():
    return SIZE_MAX;
}

///////////////////////////////////////////////////////////////////////////////

extern inline const char *rowReadError(rowReadErr);

const char *rowReadErrStr[] =
{
    "Success",
    "Incorrect usage of quotes",
    "Unable to handle value",
    "Insufficient memory",
    "Unexpected end of line",
    "Unexpected end of file",
    "End of line expected",
    "Read less rows than expected"
};

bool rowRead(FILE *f, tblsch *sch, void **tbl, void **context, size_t rowskip, size_t rowread, size_t byteread, rowReadRes *res, char delim)
{
    bool succ = 0;
    char buff[BLOCK_READ] = { '\0' }, *tempbuff = NULL;
    size_t rd = 0, row = 0, skip = rowskip, ind = 0, byte = 0;
    rowReadErr err = ROWREAD_SUCC;

    for (rd = fread(buff, 1, sizeof buff, f); rd; byte += rd, rd = fread(buff, 1, sizeof buff, f))
    {
        for (char *ptr = memchr(buff, '\n', rd); skip && ptr && (!rowread || row < rowread) && (!byteread || byte + ptr < buff + byteread); row++, skip--, ind = ptr - buff + 1, ptr = memchr(ptr + 1, '\n', buff + rd - ptr - 1));
        if (skip && (!rowread || row < rowread) && (!byteread || byte + rd < byteread)) continue;
        break;
    }

    size_t col = 0, len = 0, cap = 0;
    uint8_t quote = 0;
    
    if (rowread) rowread -= row;
    row = 0;
        
    for (; rd; byte += rd, rd = fread(buff, 1, sizeof buff, f), ind = 0)
    {
        for (; ind < rd && (!rowread || row < rowread) && (!byteread || byte + ind < byteread); ind++)
        {
            if (buff[ind] == delim)
            {
                if (quote != 2)
                {
                    if (col + 1 < sch->colschcnt)
                    {
                        tblcolsch *cl = &sch->colsch[col];
                        
                        if (!dynamicArrayTest((void **) &tempbuff, &cap, 1, len + 1)) { err = ROWREAD_ERR_MEM; goto ERR(); }
                        tempbuff[len] = '\0';
                        
                        if (cl->handler.read && !cl->handler.read(tempbuff, len, (char *) tbl[cl->ind] + row * cl->size, context[cl->ind])) { err = ROWREAD_ERR_VAL; goto ERR(); }

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
                if (quote == 2) { err = ROWREAD_ERR_QUOT; goto ERR(); } 

                if (col + 1 == sch->colschcnt)
                {
                    tblcolsch *cl = &sch->colsch[col];
                    
                    if (!dynamicArrayTest((void **) &tempbuff, &cap, 1, len + 1)) { err = ROWREAD_ERR_MEM; goto ERR(); }
                    tempbuff[len] = '\0';
            
                    if (cl->handler.read && !cl->handler.read(tempbuff, len, (char *) tbl[cl->ind] + row * cl->size, context[cl->ind])) { err = ROWREAD_ERR_VAL; goto ERR(); }

                    quote = 0;
                    len = col = 0;
                    row++;
                }
                else { err = ROWREAD_ERR_EOL; goto ERR(); } 

                continue;

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

            if (!dynamicArrayTest((void **) &tempbuff, &cap, 1, len + 1)) { err = ROWREAD_ERR_MEM; goto ERR(); }
            tempbuff[len++] = buff[ind];
        }

        if ((!rowread || row < rowread) && (!byteread || byte + rd < byteread)) continue;
        break;
    }

    if (col) { err = ROWREAD_ERR_EOF; goto ERR(); }
    if (rowread && row < rowread) { err = ROWREAD_ERR_CNT; goto ERR(); }

    succ = 1;

ERR():
    free(tempbuff);
    if (res) *res = (rowReadRes) { .read = row, .row = rowskip - skip + row, .col = col, .byte = byte + ind, .err = err };

    return succ;
}

void tblschDispose(tblsch *sch)
{
    if (!sch) return;
    
    free(sch->colsch);
    free(sch);
}

tblsch *headRead(FILE *f, tblsch *sch, tblselector sel, rowReadRes *res, char delim, void *context)
{
    char buff[BLOCK_READ] = { '\0' }, *tempbuff = NULL;
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
                    if (!dynamicArrayTest((void **) &tempbuff, &cap, 1, len + 1)) { err = ROWREAD_ERR_MEM; goto ERR(); }
                    tempbuff[len] = '\0';
            
                    size_t colpos = SIZE_MAX, colind = sel(tempbuff, len, &colpos, context);
                    
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
                    if (!dynamicArrayTest((void **) &tempbuff, &cap, 1, len + 1)) { err = ROWREAD_ERR_MEM; goto ERR(); }
                    tempbuff[len] = '\0';

                    size_t colpos = SIZE_MAX, colind = sel(tempbuff, len, &colpos, context);
                   
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
            
            if (!dynamicArrayTest((void **) &tempbuff, &cap, 1, len + 1)) { err = ROWREAD_ERR_MEM; goto ERR(); }
            tempbuff[len++] = buff[ind];
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
    
    free(tempbuff);
    if (res) *res = (rowReadRes) { .read = 1, .col = col, .byte = byte + ind, .err = err };
    
    return ressch;
}

///////////////////////////////////////////////////////////////////////////////

/*
bool rowWrite(FILE *f, tblsch *sch, void **tbl, void **context, size_t rowskip, size_t rowread, size_t byteread, rowReadRes *res)
{
    return 0;
}
*/
