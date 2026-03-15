#include <stdio.h>
#include <getopt.h>

#include "cb6502.h"
#include "dbgcli.h"

#define ACIA_SOCKNAME "acia.sock"

int main(int argc, char *argv[])
{
    char *labels_file = NULL;
    char *acia_socket = (char *)ACIA_SOCKNAME;
    int c;
    cbemu_t emu;

    dbgcli_config_t dbg_cfg;

    while((c = getopt(argc, argv, "l:s:")) != -1)
    {
        switch(c)
        {
            case 'l':
                labels_file = optarg;
                break;
            case 's':
                acia_socket = optarg;
                break;
            case '?':
                return 1;
            default:
                return 1;
        }
    }

    if(optind >= argc)
    {
        fprintf(stderr, "Usage: %s [-l LABEL_FILE] [-s ACIA_SOCKET_PATH ] rom_file\n", argv[0]);
        return 1;
    }

    if(!cb6502_init(argv[optind], acia_socket, &emu))
        return 1;

    dbg_cfg.valid_flags = 0;

    if(labels_file != NULL)
    {
        dbg_cfg.valid_flags |= DBGCLI_CONFIG_FLAG_LABEL_FILE_VALID;
        dbg_cfg.label_file = labels_file;
    }

    dbgcli_run(emu, &dbg_cfg);

    cb6502_destroy();

    return 0;
}

