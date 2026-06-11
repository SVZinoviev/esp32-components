#include "ili9341_esp_driver.h"

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LCD_PIXEL_CLOCK_HZ  (40 * 1000 * 1000)
#define LCD_SPI_HOST        SPI2_HOST

/* ILI9341 command set (datasheet section 8). Only the commands used by this
 * driver are listed. The 0xCB..0xF7 range are the vendor "extended" commands
 * recommended by the panel maker's power-on sequence. */
#define ILI9341_SWRESET     0x01  /* Software reset                            */
#define ILI9341_SLPOUT      0x11  /* Sleep out (exit sleep mode)               */
#define ILI9341_GAMMASET    0x26  /* Gamma curve preset select                 */
#define ILI9341_INVON       0x21  /* Display inversion on                      */
#define ILI9341_DISPON      0x29  /* Display on                                */
#define ILI9341_CASET       0x2A  /* Column address set                        */
#define ILI9341_PASET       0x2B  /* Page (row) address set                    */
#define ILI9341_RAMWR       0x2C  /* Memory write (pixel data follows)         */
#define ILI9341_MADCTL      0x36  /* Memory access control (orientation/order) */
#define ILI9341_COLMOD      0x3A  /* Pixel format set                          */
#define ILI9341_FRMCTR1     0xB1  /* Frame rate control (normal mode)          */
#define ILI9341_DFUNCTR     0xB6  /* Display function control                  */
#define ILI9341_PWCTR1      0xC0  /* Power control 1                           */
#define ILI9341_PWCTR2      0xC1  /* Power control 2                           */
#define ILI9341_VMCTR1      0xC5  /* VCOM control 1                            */
#define ILI9341_VMCTR2      0xC7  /* VCOM control 2                            */
#define ILI9341_PWCTRB      0xCB  /* Power control B (extended)                */
#define ILI9341_PWCTRA      0xCF  /* Power control A (extended)                */
#define ILI9341_DTCA        0xE8  /* Driver timing control A (extended)        */
#define ILI9341_DTCB        0xEA  /* Driver timing control B (extended)        */
#define ILI9341_PWRONSEQ    0xED  /* Power on sequence control (extended)      */
#define ILI9341_GMCTRP1     0xE0  /* Positive gamma correction                 */
#define ILI9341_GMCTRN1     0xE1  /* Negative gamma correction                 */
#define ILI9341_EN3GAM      0xF2  /* Enable 3-gamma function                   */
#define ILI9341_PUMPRC      0xF7  /* Pump ratio control (extended)             */

/* OR this into ili9341_cmd_t.len to wait 120 ms after sending the command. */
#define ILI9341_CMD_DELAY   0x80
#define ILI9341_LEN_MASK    0x7F

static esp_lcd_panel_io_handle_t io_handle = NULL;

typedef struct {
    uint8_t addr;
    uint8_t param[16];  /* 15 needed for gamma; round up for alignment */
    uint8_t len;        /* low 7 bits = byte count; ILI9341_CMD_DELAY = wait */
} ili9341_cmd_t;

static const ili9341_cmd_t ili9341_init_sequence[] = {
    {ILI9341_SWRESET, {0}, ILI9341_CMD_DELAY},      /* reset, then wait 120 ms */

    {ILI9341_PWCTRB, {
        0x39,  /* power control B byte 1 (vendor recommended)                  */
        0x2C,  /* power control B byte 2                                       */
        0x00,  /* power control B byte 3                                       */
        0x34,  /* PCEQ / soft-start setting                                    */
        0x02,  /* DRV_ena                                                      */
    }, 5},

    {ILI9341_PWCTRA, {
        0x00,  /* reserved (fixed)                                            */
        0xC1,  /* reserved (fixed)                                            */
        0x30,  /* REG_VD: Vcore control                                       */
    }, 3},

    {ILI9341_DTCA, {
        0x85,  /* gate driver non-overlap timing                              */
        0x00,  /* EQ timing / pre-charge                                      */
        0x78,  /* CR timing                                                   */
    }, 3},

    {ILI9341_DTCB, {
        0x00,  /* gate driver timing                                          */
        0x00,  /* gate driver timing                                          */
    }, 2},

    {ILI9341_PWRONSEQ, {
        0x64,  /* soft-start keep 1 frame                                     */
        0x03,  /* power-on sequence control                                   */
        0x12,  /* power-on sequence control                                   */
        0x81,  /* DDVDH enhance mode enabled                                  */
    }, 4},

    {ILI9341_PUMPRC, {
        0x20,  /* pump ratio control: DDVDH = 2 x VCI                         */
    }, 1},

    {ILI9341_PWCTR1, {
        0x23,  /* GVDD = 4.60 V (panel drive voltage)                         */
    }, 1},

    {ILI9341_PWCTR2, {
        0x10,  /* step-up factor for operating voltage                        */
    }, 1},

    {ILI9341_VMCTR1, {
        0x3E,  /* VCOMH = 4.250 V                                             */
        0x28,  /* VCOML = -1.500 V                                            */
    }, 2},

    {ILI9341_VMCTR2, {
        0xB7,  /* VCOM offset, VMF = nVM enabled                              */
    }, 1},

    {ILI9341_MADCTL, {
        0x28,  /* MV=1 (row/col swap -> landscape), BGR=1 (BGR sub-pixel)     */
    }, 1},

    {ILI9341_COLMOD, {
        0x55,  /* 16 bits/pixel (RGB565) for both MCU and RGB interface       */
    }, 1},

    {ILI9341_FRMCTR1, {
        0x00,  /* division ratio DIVA = fOSC                                  */
        0x18,  /* frame rate ~79 Hz (RTNA)                                    */
    }, 2},

    {ILI9341_DFUNCTR, {
        0x08,  /* interval scan, non-display area settings                    */
        0x82,  /* normal scan, source/gate output on non-display              */
        0x27,  /* number of driving lines: (0x27+1)*8 = 320                   */
    }, 3},

    {ILI9341_EN3GAM, {
        0x00,  /* 3-gamma function disabled                                   */
    }, 1},

    {ILI9341_GAMMASET, {
        0x01,  /* select gamma curve 1 (preset)                               */
    }, 1},

    {ILI9341_GMCTRP1, {                              /* positive gamma curve  */
        0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
        0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
    }, 15},

    {ILI9341_GMCTRN1, {                              /* negative gamma curve  */
        0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
        0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
    }, 15},

    {ILI9341_SLPOUT, {0}, ILI9341_CMD_DELAY},        /* wake, then wait 120 ms */
    {ILI9341_INVON,  {0}, 0x00},                     /* invert display colors */
    {ILI9341_DISPON, {0}, ILI9341_CMD_DELAY},        /* on, then wait 120 ms  */
};

int ili9341_esp_driver_init(
    esp_lcd_panel_io_color_trans_done_cb_t bus_transmission_complete_cb) {
    spi_bus_config_t bus_config = {
        .sclk_io_num = CONFIG_HW_LCD_CLK_GPIO,
        .mosi_io_num = CONFIG_HW_LCD_MOSI_GPIO,
        .miso_io_num = -1,            /* LCD is write-only */
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(
        spi_bus_initialize(LCD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = CONFIG_HW_LCD_DC_GPIO,
        .cs_gpio_num = CONFIG_HW_LCD_CS_GPIO,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = bus_transmission_complete_cb,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &io_handle));

    /* Walk the init table - same shape as st7789v_esp_driver.c. */
    for (uint8_t i = 0;
         i < (sizeof(ili9341_init_sequence) / sizeof(ili9341_cmd_t)); i++) {
        esp_lcd_panel_io_tx_param(io_handle, ili9341_init_sequence[i].addr,
                                  ili9341_init_sequence[i].param,
                                  ili9341_init_sequence[i].len & ILI9341_LEN_MASK);
        if (ili9341_init_sequence[i].len & ILI9341_CMD_DELAY) {
            vTaskDelay(pdMS_TO_TICKS(120));
        }
    }

    /* Backlight on. */
    gpio_config_t bl_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << CONFIG_HW_LCD_BL_GPIO,
    };
    ESP_ERROR_CHECK(gpio_config(&bl_config));
    gpio_set_level(CONFIG_HW_LCD_BL_GPIO, 1);

    return 0;
}

/* The (x, y, width, hight) name set is preserved from st7789v_esp_driver.c
 * even though the trailing two values are actually the exclusive (x_end,
 * y_end), matching how zx_video.c calls it. The MIPI MADCTL_2A/2B/2C trio
 * works on ILI9341 just like ST7789. */
void ili9341_esp_driver_draw_bitmap(uint16_t x, uint16_t y, uint16_t width,
                                    uint16_t height, uint16_t *data) {
    uint16_t x_end = x + (uint16_t)(width - 1);
    uint16_t y_end = y + (uint16_t)(height - 1);

    uint8_t col[4] = {
        (uint8_t)(x >> 8),     (uint8_t)(x & 0xFF),      /* start column */
        (uint8_t)(x_end >> 8), (uint8_t)(x_end & 0xFF),  /* end column   */
    };
    uint8_t row[4] = {
        (uint8_t)(y >> 8),     (uint8_t)(y & 0xFF),      /* start row */
        (uint8_t)(y_end >> 8), (uint8_t)(y_end & 0xFF),  /* end row   */
    };

    size_t pixels = (size_t)(width) * (size_t)(height);

    esp_lcd_panel_io_tx_param(io_handle, ILI9341_CASET, col, sizeof(col));
    esp_lcd_panel_io_tx_param(io_handle, ILI9341_PASET, row, sizeof(row));
    esp_lcd_panel_io_tx_color(io_handle, ILI9341_RAMWR, data, pixels << 1);
}
