#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>

#include "emulator.h"
#include "acia.h"
#include "via.h"
#include "sdcard.h"
#include "at28c256.h"
#include "memory.h"
#include "cb6502.h"
#include "log.h"
#include "syslog_log.h"
#include "console_log.h"

#ifdef __linux__
#include "acia_unix_sock.h"
#else
#include "acia_console.h"
#endif


#include "bitbang_spi.h"

#define ROM_SIZE 0x8000
#define RAM_SIZE 0x8000
#define ACIA_SIZE 0x0010
#define VIA_SIZE 0x0010

#define RAM_BASE 0x0000
#define ROM_BASE 0x8000
#define ROM_MAP_START 0x8080
#define ACIA_BASE 0x8000
#define VIA_BASE 0x8010

//#define STEP

static uint8_t rom_buffer[ROM_SIZE];

typedef struct
{
    cbemu_t emulator;
    acia_t acia;
    via_t via;
    at28c256_t rom;
    clk_t acia_clk;
    memory_t ram;
} cb6502_cxt_t;

static cb6502_cxt_t cb6502_cxt;


static bool cb6502_rom_init(const char *rom_file)
{
    FILE *rom_f;
    size_t file_read;
    bus_decode_params_t decoder;

    rom_f = fopen(rom_file, "rb");

    if(rom_f == NULL)
    {
        return false;
    }

    file_read = fread(rom_buffer, 1, ROM_SIZE, rom_f);

    if((file_read == 0) || (ferror(rom_f)))
    {
        fclose(rom_f);
        return false;
    }

    fclose(rom_f);

    cb6502_cxt.rom = at28c256_init(clock_get_core_clk(cb6502_cxt.emulator), 0);

    if(cb6502_cxt.rom == NULL)
    {
        return false;
    }

    decoder.type = BUSDECODE_RANGE;
    decoder.value.range.addr_start = ROM_MAP_START;
    decoder.value.range.addr_end = 0xFFFF;
    at28c256_register(cb6502_cxt.rom, cb6502_cxt.emulator, &decoder, ROM_BASE);
    at28c256_load_image(cb6502_cxt.rom, file_read, rom_buffer, 0);

    return true;
}

static bool cb6502_acia_init(const char *acia_socket)
{
    clock_config_t clk_cfg;
    const acia_trans_interface_t *acia_transport;
    void *acia_transport_params;
    bus_decode_params_t decoder;
#ifdef __linux__
    acia_unix_sock_params_t unix_params;
#endif

    clk_cfg.timing_type = CLOCK_FREQ;
    clk_cfg.timing.freq = 1843200;


    cb6502_cxt.acia_clk = clock_add(cb6502_cxt.emulator, &clk_cfg);

#ifdef __linux__
    unix_params.sockname = acia_socket;
    acia_transport = acia_unix_get_iface();
    acia_transport_params = &unix_params;
#else
    acia_transport = acia_console_get_iface();
    acia_transport_params = NULL;
#endif

    if(cb6502_cxt.acia_clk == NULL)
    {
        return false;
    }

    cb6502_cxt.acia = acia_init(cb6502_cxt.emulator, acia_transport, acia_transport_params, cb6502_cxt.acia_clk);

    /* Note clock will be removed by destroy call if there is an error. */
    if(cb6502_cxt.acia == NULL)
    {
        return false;
    }

    decoder.type = BUSDECODE_RANGE;
    decoder.value.range.addr_start = ACIA_BASE;
    decoder.value.range.addr_end = ACIA_BASE + ACIA_SIZE - 1;
    acia_register(cb6502_cxt.acia, &decoder, ACIA_BASE, false);

    return true;
}

static bool cb6502_via_init(void)
{
    bus_decode_params_t decoder;

    cb6502_cxt.via = via_init();

    if(cb6502_cxt.via == NULL)
    {
        return false;
    }

    decoder.type = BUSDECODE_RANGE;
    decoder.value.range.addr_start = VIA_BASE;
    decoder.value.range.addr_end = VIA_BASE + VIA_SIZE - 1;
    via_register(cb6502_cxt.via, cb6502_cxt.emulator, &decoder, VIA_BASE);

    return true;
}

static bool cb6502_ram_init(void)
{
    bus_decode_params_t decoder;

    cb6502_cxt.ram = memory_init(RAM_SIZE, 0);

    if(cb6502_cxt.ram == NULL)
    {
        return false;
    }

    decoder.type = BUSDECODE_RANGE;
    decoder.value.range.addr_start = RAM_BASE;
    decoder.value.range.addr_end = RAM_BASE + RAM_SIZE - 1;
    memory_register(cb6502_cxt.ram, cb6502_cxt.emulator, &decoder, RAM_BASE);

    return true;
}

bool cb6502_init(const char *rom_file, const char *acia_socket, cbemu_t *emulator)
{
    emu_config_t config;

    if(emulator == NULL)
    {
        return false;
    }

    log_set_handler(console_log_print);
    log_set_level(lDEBUG);
    memset(&cb6502_cxt, 0, sizeof(cb6502_cxt));

    config.mainclk_config.timing_type = CLOCK_FREQ;
    config.mainclk_config.timing.freq = 1000000;

    *emulator = emu_init(&config);

    if(*emulator == NULL)
    {
        fprintf(stderr, "Emulator init fail\n");
        return false;
    }

    cb6502_cxt.emulator = *emulator;

    if(!cb6502_rom_init(rom_file))
    {
        goto error;
    }

    if(!cb6502_acia_init(acia_socket))
    {
        goto error;
    }

    if(!cb6502_via_init())
    {
        goto error;
    }

    if(!cb6502_ram_init())
    {
        goto error;
    }

    sdcard_init("/mnt/sdcard_fs.bin");

    if(!bitbang_spi_init(cb6502_cxt.via))
    {
        goto error;
    }
    //printf("sdcard init %s\n", sdcard_init("/mnt/sdcard_fs.bin") ? "success" : "failure");
    //

    return true;

error:
    cb6502_destroy();
    return false;
}

void cb6502_destroy(void)
{
    bitbang_spi_cleanup();

    if(cb6502_cxt.rom != NULL)
    {
        at28c256_destroy(cb6502_cxt.rom);
    }

    if(cb6502_cxt.ram != NULL)
    {
        memory_cleanup(cb6502_cxt.ram);
    }

    if(cb6502_cxt.via != NULL)
    {
        via_cleanup(cb6502_cxt.via);
    }

    if(cb6502_cxt.acia != NULL)
    {
        acia_cleanup(cb6502_cxt.acia);
    }

    if(cb6502_cxt.acia_clk != NULL)
    {
        clock_remove(cb6502_cxt.emulator, cb6502_cxt.acia_clk);
    }

    if(cb6502_cxt.emulator != NULL)
    {
        emu_cleanup(cb6502_cxt.emulator);
    }

    memset(&cb6502_cxt, 0, sizeof(cb6502_cxt));
}
