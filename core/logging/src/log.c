#include <stdio.h>
#include <stdarg.h>

#include "log.h"

#ifdef ENABLE_LOGGING

#define LOG_BUFFER_SIZE 256

static log_handler_t log_handler = NULL;
static log_level_t log_level;

void log_set_handler(log_handler_t handler)
{
    log_handler = handler;
}

void log_set_level(log_level_t level)
{
    log_level = level;
}

void log_print(log_level_t level, const char *fmt, ...)
{
    va_list args;
    char buffer[LOG_BUFFER_SIZE];

    if(level == lNONE || log_level == lNONE || log_handler == NULL)
    {
        return;
    }

    if(level < log_level)
    {
        return;
    }

    va_start(args, fmt);
    vsnprintf(buffer, LOG_BUFFER_SIZE, fmt, args);
    va_end(args);

    log_handler(level, buffer);
}

#endif
