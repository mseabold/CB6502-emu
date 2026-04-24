#include <string.h>

#include "bitbang_spi.h"
#include "via.h"
#include "sdcard.h"

#define SPI_CLK         0x01
#define SPI_MOSI        0x02
#define SPI_MISO        0x04
#define SPI_SS_SDCARD   0x10
#define SPI_DETECT      0x80

typedef struct bitbang_spi_cxt_s
{
    via_t via;
    via_cb_handle_t via_cb;

    uint8_t SPI_cnt;
    uint8_t SPI_out;
    bool spi_clk_state;

    bool sdcard_sel;
    uint8_t sdcard_in;
} bitbang_spi_cxt_t;

static bitbang_spi_cxt_t cxt;

static void bitbang_spi_update_outputs()
{
    uint8_t data = 0;

    if(cxt.sdcard_sel)
    {
        if(cxt.sdcard_in & 0x80)
        {
            data |= SPI_MISO;
        }
    }
    else
    {
        /* Pull up. */
        data |= SPI_MISO;
    }

    if(sdcard_detect())
    {
        data |= SPI_DETECT;
    }

    via_write_data_port(cxt.via, false, (SPI_MISO | SPI_DETECT), data);
}

static void bitbang_portb_write(uint8_t data)
{
    /* SS is active low. */
    cxt.sdcard_sel = (data & SPI_SS_SDCARD) == 0;

    if(data & SPI_CLK && !cxt.spi_clk_state)
    {
        /* Before we process the first clock edge, go ahead and latch what the SD Card
         * wants to transmit. Note, this is presuming that the SS pin remains asserted
         * for all 8 SPI pulses. It should or else it's a FW bug. */
        if(cxt.sdcard_sel)
        {
            if(cxt.SPI_cnt == 0)
            {
                /* Latch from the SD Card. */
                cxt.sdcard_in = sdcard_spi_get();
                //printf("SDCard: 0x%02x\n", cxt.sdcard_in);
            }
            else
            {
                /* Shift the SD card buffer. Note that while technically this should
                 * happen on the falling edge, since FW is bit-banging, it has to
                 * drive CLK high *then* test MISO, so we are OK just shifting on
                 * the rising edge here. */
                cxt.sdcard_in <<= 1;
            }
        }

        cxt.SPI_out <<= 1;
        cxt.SPI_out |= (data & SPI_MOSI) ? 1 : 0;

        if(++cxt.SPI_cnt == 8)
        {
            /* Byte complete. */
            //printf("SPI out 0x%02x\n", cxt.SPI_out);

            if(cxt.sdcard_sel)
            {
                sdcard_spi_write(cxt.SPI_out);
            }

            cxt.SPI_cnt = 0;
            cxt.SPI_out = 0;
        }
    }

    cxt.spi_clk_state = (data & SPI_CLK) == SPI_CLK;

    bitbang_spi_update_outputs();
}

static void bitbang_spi_via_cb(via_t via, const via_event_data_t *event, void *userdata)
{
    if((event->type == VIA_EV_PORT_CHANGE) && (event->data.port == VIA_PORTB))
    {
        bitbang_portb_write(via_read_data_port(via, false));
    }
}

bool bitbang_spi_init(via_t via)
{
    if(via == NULL)
    {
        return false;
    }

    memset(&cxt, 0, sizeof(cxt));

    cxt.via = via;
    cxt.via_cb = via_register_callback(via, bitbang_spi_via_cb, &cxt);

    if(cxt.via_cb == NULL)
    {
        return false;
    }

    bitbang_spi_update_outputs();

    return true;
}

void bitbang_spi_cleanup(void)
{
    if(cxt.via_cb != NULL)
    {
        via_unregister_callback(cxt.via, cxt.via_cb);
        cxt.via_cb = NULL;
    }
}
