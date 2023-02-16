#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>

#include "sys.h"
#include "cpu.h"
#include "acia.h"
#include "via.h"
#include "sdcard.h"
#include "at28c256.h"

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
#define ROM_BASE 0x8080
#define ACIA_BASE 0x8000
#define VIA_BASE 0x8010

//#define STEP

static uint8_t RAM[RAM_SIZE];
static uint8_t ROM_DATA[ROM_SIZE];
static sys_cxt_t sys;
static acia_t acia;
static via_t via;
static at28c256_t rom;

#define ADDR_IN_REGION(_addr, _base, _size) ((_addr) >= (_base) && (uint32_t)(_addr) < (uint32_t)(_base) + (_size))

static uint8_t memory_read(uint16_t address)
{
    uint8_t value = 0xff;

    if(ADDR_IN_REGION(address, RAM_BASE, RAM_SIZE))
    {
        value = RAM[address-RAM_BASE];
    }
    else if(ADDR_IN_REGION(address, ROM_BASE, ROM_SIZE-0x80))
    {
        value = at28c256_read(rom, address-0x8000);
    }
    else if(ADDR_IN_REGION(address, VIA_BASE, VIA_SIZE))
    {
        value = via_read(via, (uint8_t)(address - VIA_BASE));
    }
    else if(ADDR_IN_REGION(address, ACIA_BASE, ACIA_SIZE))
    {
        value = acia_read(acia, (uint8_t)(address - ACIA_BASE));
    }

    return value;
}

static void memory_write(uint16_t address, uint8_t value)
{
    if(address >= RAM_BASE && (uint32_t)address < (uint32_t)RAM_BASE+RAM_SIZE)
    {
        RAM[address-RAM_BASE] = value;
    }
    else if(address >= ACIA_BASE && (uint32_t)address < (uint32_t)ACIA_BASE+ACIA_SIZE)
    {
        acia_write(acia, (uint8_t)(address - ACIA_BASE), value);
    }
    else if(ADDR_IN_REGION(address, VIA_BASE, VIA_SIZE))
    {
        via_write(via, (uint8_t)(address - VIA_BASE), value);
    }
    else if(ADDR_IN_REGION(address, ROM_BASE, ROM_SIZE))
    {
        at28c256_write(rom, address-0x8000, value);
    }
}

static const mem_space_t mem_space = {
    memory_write,
    memory_read,
    memory_read //TODO separate peek handler
};

static void cpu_tick_callback(uint32_t ticks)
{
}

bool cb6502_init(const char *rom_file, const char *acia_socket)
{
    int rom_fd;
    int read_result;
    unsigned int total_read;
    const acia_trans_interface_t *acia_transport;
    void *acia_transport_params;
#ifdef __linux__
    acia_unix_sock_params_t unix_params;
#endif

    rom_fd = open(rom_file, O_RDONLY);

    if(rom_fd < 0)
    {
        fprintf(stderr, "Unable to open ROM file: %s\n", strerror(errno));
        return false;
    }

    total_read = 0;

    do
    {
        read_result = read(rom_fd, &ROM_DATA[total_read], ROM_SIZE-total_read);

        if(read_result > 0)
        {
            total_read += read_result;
        }
    } while(read_result > 0);

    if(read_result < 0)
    {
        fprintf(stderr, "Unable to read from ROM file: %s\n", strerror(errno));
        close(rom_fd);
        return false;
    }

    if(total_read != ROM_SIZE)
    {
        fprintf(stderr, "WARNING: ROM file does not fill up ROM. Some of ROM may be unitialized/0.\n");
    }

    sys = sys_init(&mem_space);
    if(sys == NULL)
    {
        fprintf(stderr, "feck\n");
        return false;
    }

#ifdef __linux__
    unix_params.sockname = acia_socket;
    acia_transport = acia_unix_get_iface();
    acia_transport_params = &unix_params;
#else
    acia_transport = acia_console_get_iface();
    acia_transport_params = NULL;
#endif

    acia = acia_init(sys, acia_transport, acia_transport_params);
    if(acia == NULL)
    {
        fprintf(stderr, "Unable to initialize ACIA\n");
        return false;
    }

    via = via_init();
    if(via == NULL)
    {
        fprintf(stderr, "Unable to unitialize VIA\n");
        return false;
    }

    rom = at28c256_init(sys, 0);
    if(rom == AT28C256_INVALID_HANDLE)
    {
        fprintf(stderr, "Unable to initialize AT28C256\n");
        return false;
    }
    at28c256_load_image(rom, ROM_SIZE, ROM_DATA, 0);

    via_register_protocol(via, bitbang_spi_get_prot(), NULL);
    sdcard_init("/mnt/sdcard_fs.bin");
    //printf("sdcard init %s\n", sdcard_init("/mnt/sdcard_fs.bin") ? "success" : "failure");

    cpu_init(sys, true);
    cpu_set_tick_callback(cpu_tick_callback);

    return true;
}

sys_cxt_t cb6502_get_sys(void)
{
    return sys;
}

void cb6502_destroy(void)
{
    acia_cleanup(acia);
}
