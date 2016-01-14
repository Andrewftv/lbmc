#include <stdio.h>
#include <stdarg.h>

#include "log.h"

#define LOG_BUFF_LEN    512

#ifdef LBMC_DEBUG
static log_dbg_lvl_t current_log_level = LOG_LVL_INFO;
#else
static log_dbg_lvl_t current_log_level = LOG_LVL_WARN;
#endif

static char *str_log_lvl[] = {"VERB", "INFO", "WARN", "ERROR", "ALERT", "FATAL"};
static char log_buff[LOG_BUFF_LEN];
static FILE *output = NULL;

void logs_init(char *file)
{
    if (!file)
    {
        output = stderr;
    }
    else
    {
        output = fopen(file, "w+");
        if (!output)
            output = stderr;
    }
}

void logs_uninit(void)
{
    if (output && output != stderr)
        fclose(output);
}

void lbmc_log(const char *file, int line, log_dbg_lvl_t lvl, char *fmt, ...)
{
    va_list ap;

    if (lvl < current_log_level)
        return;

    va_start(ap, fmt);
    /* Reserve a last byte for string terminator */
    vsnprintf(log_buff, LOG_BUFF_LEN - 1, fmt, ap);
    va_end(ap);
    fprintf(output, "[%5s][%s:%d] %s", str_log_lvl[lvl], file, line, log_buff);
    if (output != stderr)
        fflush(output);
}

