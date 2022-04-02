#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "sdcard.h"

#define CMD0 0x40
#define CMD8 0x48
#define CMD17 0x51
#define CMD55 0x77
#define ACMD41 0x69

typedef enum {
    SD_MODE,
    INIT,
    READY
} card_state_t;

typedef enum {
    IDLE,
    CMD,
    R1,
    R7,
    R1_DATA,
    DATA_START_TOKEN,
    DATA,
    CRC1,
    CRC2
} cmd_state_t;

typedef struct sdcard_context_s
{
    bool init;
    int image_fd;
    uint8_t sector_buf[512];
    card_state_t card_state;
    cmd_state_t cmd_state;
    uint8_t cmd_buf[6];
    uint8_t rsp_buf[6];
    uint8_t cmd_buf_idx;
    uint8_t rsp_buf_idx;
    uint8_t out_reg;
    bool acmd_pend;
    uint8_t acmd41_cnt;
    uint16_t block_cnt;
} sdcard_context_t;

static sdcard_context_t cxt;

static void handle_cmd(void)
{
    off_t block;
    printf("Handle cmd: %u, 0x%02x\n", cxt.card_state, cxt.cmd_buf[0]);
    switch(cxt.card_state)
    {
        case SD_MODE:
            // No response to anything but CMD0 in SD mode
            if(cxt.cmd_buf[0] == CMD0)
            {
                cxt.card_state = INIT;
            }

            cxt.cmd_state = R1;
            cxt.out_reg = 0x01;
            break;
        case INIT:
            switch(cxt.cmd_buf[0])
            {
                case CMD8:
                    cxt.cmd_state = R7;
                    cxt.rsp_buf_idx = 1;
                    cxt.rsp_buf[0] = 0x01;
                    memcpy(&cxt.rsp_buf[1], &cxt.cmd_buf[1], 4);
                    break;
                case CMD55:
                    cxt.acmd_pend = true;
                    cxt.cmd_state = R1;
                    cxt.out_reg = 0x01;
                    break;
            }
            break;
        case READY:
            switch(cxt.cmd_buf[0])
            {
                case CMD17:
                    cxt.cmd_state = R1_DATA;
                    cxt.out_reg = 0x00;
                    block = ((uint32_t)cxt.cmd_buf[1] << 24) | ((uint32_t)cxt.cmd_buf[2] << 16) | ((uint32_t)cxt.cmd_buf[3] << 8) | (cxt.cmd_buf[4]);
                    block *= 512;
                    printf("Read block: %lu, 0x%08x\n", block, (uint32_t)block);
                    if(lseek(cxt.image_fd, block, SEEK_SET) < 0)
                    {
                        fprintf(stderr, "Seek failure: %s\n", strerror(errno));
                        cxt.out_reg = 0x20;
                        cxt.cmd_state = R1;
                    }

                    if(read(cxt.image_fd, cxt.sector_buf, 512) < 0)
                    {
                        fprintf(stderr, "Read failure: %s\n", strerror(errno));
                        cxt.out_reg = 0x20;
                        cxt.cmd_state = R1;
                    }
                    break;
                defaut:
                    cxt.cmd_state = R1;
                    cxt.out_reg = 0x40;
                    break;
            }
            break;
        default:
            cxt.cmd_state = R1;
            cxt.out_reg = 0x01;
    }
}

static void handle_acmd(void)
{
    if(cxt.card_state == INIT)
    {
        if(cxt.cmd_buf[0] == ACMD41)
        {
            if(++cxt.acmd41_cnt == 6)
            {
                cxt.card_state = READY;
                cxt.out_reg = 0;
            }
            else
            {
                cxt.out_reg = 0x01;
            }

            cxt.cmd_state = R1;
        }
    }
    else
    {
        cxt.cmd_state = R1;
        cxt.out_reg = 0x40;
    }

    cxt.acmd_pend = false;
}

bool sdcard_init(const char *image_file)
{
    memset(&cxt, 0, sizeof(cxt));

    cxt.image_fd = open(image_file, O_RDONLY);

    if(cxt.image_fd < 0)
    {
        fprintf(stderr, "Unable to open sdcard image: %s\n", strerror(errno));
        return false;
    }

    cxt.out_reg = 0xff;
    cxt.init = true;

    return true;
}

void sdcard_spi_write(uint8_t byte)
{
    if(!cxt.init)
        return;

    if(cxt.cmd_state == IDLE)
    {
        if(byte & 0x80)
        {
            /* No start bit, so ignore the byte */
            return;
        }
        else
        {
            cxt.cmd_state = CMD;
        }
    }

    if(cxt.cmd_state != CMD)
    {
        /* Ignore input bytes while processing responses or data. */
        return;
    }

    cxt.cmd_buf[cxt.cmd_buf_idx++] = byte;

    if(cxt.cmd_buf_idx == 6)
    {
        if(cxt.acmd_pend)
            handle_acmd();
        else
            handle_cmd();

        cxt.cmd_buf_idx = 0;
    }

}

uint8_t sdcard_spi_get(void)
{
    uint8_t out = cxt.out_reg;

    if(!cxt.init)
        return 0xff;

    switch(cxt.cmd_state)
    {
        case R1:
            printf("Return to idle\n");
            cxt.out_reg = 0xff;
            cxt.cmd_state = IDLE;
            break;
        case R7:
            if(cxt.rsp_buf_idx == 5)
            {
                cxt.out_reg = 0xff;
                cxt.cmd_state = IDLE;
                cxt.rsp_buf_idx = 0;
            }
            else
            {
                cxt.out_reg = cxt.rsp_buf[cxt.rsp_buf_idx++];
            }
            break;
        case R1_DATA:
            /* After R1, do 1 byte of 0xff ("processing") */
            cxt.out_reg = 0xff;
            cxt.cmd_state = DATA_START_TOKEN;
            break;
        case DATA_START_TOKEN:
            /* Start token is next. */
            cxt.out_reg = 0xfe;
            cxt.cmd_state = DATA;
            cxt.block_cnt = 0;
            break;
        case DATA:
            cxt.out_reg = cxt.sector_buf[cxt.block_cnt];
            if(++cxt.block_cnt == 512)
            {
                cxt.cmd_state = CRC1;
            }
            break;
        case CRC1:
            cxt.out_reg = 0x00;
            cxt.cmd_state = CRC2;
            break;
        case CRC2:
            cxt.out_reg = 0x00;
            cxt.cmd_state = IDLE;
            printf("Block done\n");
            break;
        default:
            cxt.out_reg = 0xff;
            break;
    }

    return out;
}
