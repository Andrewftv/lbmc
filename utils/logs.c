#include <stdio.h>
#include <stdarg.h>

#include "log.h"

static log_dbg_lvl_t current_log_level = LOG_LVL_INFO;

static char *str_log_lvl[] = {"VERB", "INFO", "WARN", "ERROR", "ALERT", "FATAL"};
static char log_buff[512];
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
	vsnprintf(log_buff, 512, fmt, ap);
	va_end(ap);
	fprintf(output, "[%5s][%s:%d] %s", str_log_lvl[lvl], file, line, log_buff);
	if (output != stderr)
		fflush(output);
}

