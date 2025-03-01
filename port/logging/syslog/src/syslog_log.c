#include <syslog.h>

#include "syslog_log.h"

static const int level_map[] =
{
    LOG_DEBUG,
    LOG_INFO,
    LOG_NOTICE,
    LOG_WARNING,
    LOG_ERR
};

void syslog_log_print(log_level_t level, const char *logstr)
{
    syslog(level_map[level], "%s", logstr);
}
