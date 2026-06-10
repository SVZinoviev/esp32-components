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

#define LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)
#define LCD_SPI_HOST        SPI2_HOST

static esp_lcd_panel_io_handle_t io_handle = NULL;

typedef struct {
  uint8_t addr;
  uint8_t param[16];  /* 15 needed for gamma; round up for alignment */
  uint8_t len;        /* bit 7 = wait 120 ms after cmd */
} ili9341_cmd_t;

static const ili9341_cmd_t ili9341_init_sequence[] = {
    {0x01, {0}, 0x80},                                  /* Software Reset (delay) */
    {0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02}, 5},          /* Power Control B */
    {0xCF, {0x00, 0xC1, 0x30}, 3},                      /* Power Control A */
    {0xE8, {0x85, 0x00, 0x78}, 3},                      /* Driver Timing Control A */
    {0xEA, {0x00, 0x00}, 2},                            /* Driver Timing Control B */
    {0xED, {0x64, 0x03, 0x12, 0x81}, 4},                /* Power on sequence */
    {0xF7, {0x20}, 1},                                  /* Pump ratio */
    {0xC0, {0x23}, 1},                                  /* Power Control 1 (VRH) */
    {0xC1, {0x10}, 1},                                  /* Power Control 2 (BT) */
    {0xC5, {0x3E, 0x28}, 2},                            /* VCOM Control 1 */
    {0xC7, {0xB7}, 1},                                  /* VCOM Control 2 */
    {0x36, {0x28}, 1},                                  /* MADCTL: MV=1 BGR=1 (landscape) */
    {0x3A, {0x55}, 1},                                  /* Pixel Format: 16-bit RGB565 */
    {0xB1, {0x00, 0x18}, 2},                            /* Frame Rate Control (61.7 Hz) */
    {0xB6, {0x08, 0x82, 0x27}, 3},                      /* Display Function Control */
    {0xF2, {0x00}, 1},                                  /* 3Gamma function disable */
    {0x26, {0x01}, 1},                                  /* Gamma curve preset 1 */
    {0xE0,                                              /* Positive Gamma Correction */
     {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
      0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00},
     15},
    {0xE1,                                              /* Negative Gamma Correction */
     {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
      0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F},
     15},
    {0x11, {0}, 0x80},                                  /* Sleep Out (delay) */
    {0x21, {0}, 0x00},                                  /* Invert display */
    {0x29, {0}, 0x80},                                  /* Display On (delay) */
};

int ili9341_esp_driver_init(
    esp_lcd_panel_io_color_trans_done_cb_t bus_transmission_complete_cb) {
  spi_bus_config_t bus_config = {
      .sclk_io_num = HW_LCD_CLK_GPIO,
      .mosi_io_num = HW_LCD_MOSI_GPIO,
      .miso_io_num = -1,            /* LCD is write-only */
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t),
  };
  ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_spi_config_t io_config = {
      .dc_gpio_num = HW_LCD_DC_GPIO,
      .cs_gpio_num = HW_LCD_CS_GPIO,
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
                              ili9341_init_sequence[i].len & 0x7f);
    if (ili9341_init_sequence[i].len & 0x80) {
      vTaskDelay(pdMS_TO_TICKS(120));
    }
  }

  /* Backlight on. */
  gpio_config_t bl_config = {.mode = GPIO_MODE_OUTPUT,
                             .pin_bit_mask = 1ULL << HW_LCD_BL_GPIO};
  ESP_ERROR_CHECK(gpio_config(&bl_config));
  gpio_set_level(HW_LCD_BL_GPIO, 1);

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
      (uint8_t)(x >> 8), (uint8_t)(x & 0xFF),
      (uint8_t)(x_end >> 8), (uint8_t)(x_end & 0xFF),
  };
  uint8_t row[4] = {
      (uint8_t)(y >> 8), (uint8_t)(y & 0xFF),
      (uint8_t)(y_end >> 8), (uint8_t)(y_end & 0xFF),
  };

  size_t pixels =
      (size_t)(width) * (size_t)(height);

  esp_lcd_panel_io_tx_param(io_handle, 0x2A, col, sizeof(col));    /* CASET  */
  esp_lcd_panel_io_tx_param(io_handle, 0x2B, row, sizeof(row));    /* PASET  */
  esp_lcd_panel_io_tx_color(io_handle, 0x2C, data,
                            pixels << 1);            /* RAMWR  */
}
