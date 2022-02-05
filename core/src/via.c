#include "via.h"
#include <string.h>
#include <stdio.h>

#define DATAB 0x00
#define DATAA 0x01
#define DDRB  0x02
#define DDRA  0x03
#define T1CL  0x04
#define T1CH  0x05
#define T1LL  0x06
#define T1LH  0x07
#define T2CL  0x08
#define T2CH  0x09
#define SR    0x0A
#define ACR   0x0B
#define PCR   0x0C
#define IFR   0x0D
#define IER   0x0E
#define DA2   0x0F

typedef struct VIACxt_s
{
    uint8_t data_a;
    uint8_t dirmask_a;
    uint8_t data_b;
    uint8_t dirmask_b;

    uint8_t SPI_cnt;
    uint8_t SPI_out;
    bool spi_clk_state;
} VIACxt_t;

static VIACxt_t VIACxt;

bool via_init(void)
{
    memset(&VIACxt, 0, sizeof(VIACxt));
    return true;
}

void via_write(uint8_t reg, uint8_t val)
{
    switch(reg)
    {
        case DDRA:
            VIACxt.dirmask_a = val;
            break;
        case DATAA:
            VIACxt.data_a = val & VIACxt.dirmask_a;
            break;
        case DDRB:
            VIACxt.dirmask_b = val;
            printf("dirmask write: %02x\n", val);
            break;
        case DATAB:
            VIACxt.data_b = val & VIACxt.dirmask_b;

            if(VIACxt.data_b & 0x01 && !VIACxt.spi_clk_state)
            {
                VIACxt.SPI_out <<= 1;
                VIACxt.SPI_out |= (VIACxt.data_b & 0x02) ? 1 : 0;

                if(++VIACxt.SPI_cnt == 8)
                {
                    /* Byte complete. */
                    printf("SPI out 0x%02x\n", VIACxt.SPI_out);

                    VIACxt.SPI_cnt = 0;
                    VIACxt.SPI_out = 0;
                }
            }

            VIACxt.spi_clk_state = (VIACxt.data_b & 0x01) == 0x01;

            break;

    }
}

uint8_t via_read(uint8_t reg)
{
    uint8_t out;

    switch(reg)
    {
        case DDRA:
            return VIACxt.dirmask_a;
        case DDRB:
            return VIACxt.dirmask_b;
        case DATAB:
            out = VIACxt.data_b;
            return out;
    }
    return 0;
}
