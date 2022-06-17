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
#include "mem.h"
#include "sdcard.h"
#include "debugger.h"
#include "at28c256.h"

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
        value = at28c256_read(rom, address-ROM_BASE);
    }
    else if(ADDR_IN_REGION(address, VIA_BASE, VIA_SIZE))
    {
        value = via_read(via, (uint8_t)(address - VIA_BASE));
    }
    else if(ADDR_IN_REGION(address, ACIA_BASE, ACIA_SIZE))
    {
        value = acia_read(acia, (uint8_t)(address - ACIA_BASE));
    }

#ifdef STEP
    printf("0x%04x 0x%02x R\n", address, value);
#endif
    //printf("0x%04x 0x%02x R\n", address, value);

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
        at28c256_write(rom, address-ROM_BASE, value);
    }

#ifdef STEP
    printf("0x%04x 0x%02x W\n", address, value);
#endif
    //printf("0x%04x 0x%02x W\n", address, value);
}

static mem_space_t mem_space = {
    memory_write,
    memory_read
};

static void cpu_tick_callback(uint32_t ticks)
{
}

int main(int argc, char *argv[])
{
    int rom_fd;
    int read_result;
    unsigned int total_read;

    char *labels_file = NULL;
    char *acia_socket = (char *)ACIA_DEFAULT_SOCKNAME;
    int c;
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

    rom_fd = open(argv[optind], O_RDONLY);

    if(rom_fd < 0)
    {
        fprintf(stderr, "Unable to open ROM file: %s\n", strerror(errno));
        return 1;
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
        return 1;
    }

    if(total_read != ROM_SIZE)
    {
        fprintf(stderr, "WARNING: ROM file does not fill up ROM. Some of ROM may be unitialized/0.\n");
    }

    sys = sys_init(memory_read, memory_write);
    if(sys == NULL)
    {
        fprintf(stderr, "feck\n");
        return 1;
    }
    acia = acia_init(acia_socket, sys);
    if(acia == NULL)
    {
        fprintf(stderr, "Unable to initialize ACIA\n");
        return 1;
    }

    via = via_init();
    if(via == NULL)
    {
        fprintf(stderr, "Unable to unitialize VIA\n");
        return 1;
    }

    rom = at28c256_init(sys, 0);
    if(rom == AT28C256_INVALID_HANDLE)
    {
        fprintf(stderr, "Unable to initialize AT28C256\n");
        return 1;
    }
    at28c256_load_image(rom, ROM_SIZE, ROM_DATA, 0);

    via_register_protocol(via, bitbang_spi_get_prot(), NULL);
    printf("sdcard init %s\n", sdcard_init("/mnt/sdcard_fs.bin") ? "success" : "failure");
    //printf("sdcard init %s\n", sdcard_init("/home/matt/git/CB6502/sddump.bin") ? "success" : "failure");
    cpu_init(sys, true);
    cpu_set_tick_callback(cpu_tick_callback);

    if(0)
    {
        char buf[16];
        fgets(buf, 16, stdin);
    }

    debug_run(sys, labels_file);

    acia_cleanup(acia);

#if 0
    while(1)
    {
        char buf[16];
        step6502();
#ifdef STEP
        fgets(buf, 16, stdin);
#endif
    }

    close(rom_fd);
#endif

    return 0;
}
