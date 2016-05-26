/*
 *      Copyright (C) 2016  Andrew Fateyev
 *      andrew.ftv@gmail.com
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
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
