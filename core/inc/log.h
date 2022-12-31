#ifndef __LOG_H__
#define __LOG_H__

typedef enum
{
    lNONE,
    lDEBUG,
    lINFO,
    lNOTICE,
    lWARNING,
    lERROR,
} log_level_t;

typedef void (*log_handler_t)(log_level_t level, const char *logstr);

/* TODO Configure this from CMake */
#define ENABLE_LOGGING

#ifdef ENABLE_LOGGING
void log_set_handler(log_handler_t handler);
void log_set_level(log_level_t level);
void log_print(log_level_t level, const char *fmt, ...);
#else
#define log_set_handler(...) ((void)0)
#define log_set_level(...) ((void)0)
#define log_print(...) ((void)0)
#endif


#endif /* end of include guard: __LOG_H__ */
