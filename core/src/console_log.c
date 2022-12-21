#include <stdio.h>

#include "log.h"

void console_log_print(log_level_t level, const char *logstr)
{
    printf("%s", logstr);
}
