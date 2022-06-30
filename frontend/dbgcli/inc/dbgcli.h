#ifndef __DBGCLI_H__
#define __DBGCLI_H__

#include "sys.h"

typedef struct dbgcli_config_s
{
    uint32_t valid_flags;
    const char *label_file;
} dbgcli_config_t;

#define DBGCLI_CONFIG_FLAG_LABEL_FILE_VALID 0x00000001

/**
 * Take control of the program execution and begins the debugger CLI
 * frontend. Will not return until program exit is requested.
 *
 * @param[in] system System context that will be controlled by the debugger.
 * @param[in] config Configuration information for the dbgcli instance.
 *
 * @return Exit code suitable to be returned by main()
 */

int dbgcli_run(sys_cxt_t system, dbgcli_config_t *config);

#endif /* end of include guard: __DBGCLI_H__ */
