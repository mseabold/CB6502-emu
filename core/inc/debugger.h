#ifndef __DEBUGGER_H__
#define __DEBUGGER_H__

#include <stdint.h>
#include <stdbool.h>
#include "sys.h"

/**
 * Takes control of the entire execution of the program. It drives the console as well
 * as the emulator. This will never return until the debugger is exited entirely.
 */
void debug_run(sys_cxt_t system_cxt, char *labels_file);

#endif /* end of include guard: __DEBUGGER_H__ */
