#include "st7789v_esp_driver.h"

#include "driver/gpio.h"
#include "esp_dma_utils.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LCD_PIXEL_CLOCK_HZ (10 * 1000 * 1000)

/* ST7789V command set (datasheet section 9). Only the commands issued by this
 * driver's init table are listed. 0xB2..0xE1 are the panel-specific display
 * and power/gamma tuning commands. */
#define ST7789_SWRESET   0x01  // Software reset
#define ST7789_SLPOUT    0x11  // Sleep out (exit sleep mode)
#define ST7789_INVON     0x21  // Display inversion on
#define ST7789_DISPON    0x29  // Display on
#define ST7789_COLMOD    0x3A  // Interface pixel format
#define ST7789_MADCTL    0x36  // Memory data access control (orientation)
#define ST7789_PORCTRL   0xB2  // Porch setting
#define ST7789_GCTRL     0xB7  // Gate control (VGH/VGL levels)
#define ST7789_VCOMS     0xBB  // VCOM setting
#define ST7789_LCMCTRL   0xC0  // LCM control
#define ST7789_VDVVRHEN  0xC2  // VDV and VRH command enable
#define ST7789_VRHS      0xC3  // VRH set
#define ST7789_VDVSET    0xC4  // VDV set
#define ST7789_FRCTRL2   0xC6  // Frame rate control in normal mode
#define ST7789_PWCTRL1   0xD0  // Power control 1
#define ST7789_PVGAMCTRL 0xE0  // Positive voltage gamma control
#define ST7789_NVGAMCTRL 0xE1  // Negative voltage gamma control

/* OR this into st7789v_cmd_t.len to wait 120 ms after sending the command. */
#define ST7789_CMD_DELAY 0x80
#define ST7789_LEN_MASK  0x7F

static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;

typedef struct {
  uint8_t addr;
  uint8_t param[14];  // 14 is enough (gamma curves use all 14)
  uint8_t len;        // low 7 bits = byte count; ST7789_CMD_DELAY = wait
} st7789v_cmd_t;

static const st7789v_cmd_t st7789v_init_sequence[] = {
    // Command, {Parameters}, Parameter Length (| ST7789_CMD_DELAY to wait)
    {ST7789_SWRESET, {0}, 0},                 // software reset
    {ST7789_SLPOUT, {0}, ST7789_CMD_DELAY},   // wake panel, then wait 120 ms

    {ST7789_COLMOD, {
        0x05,  // 16 bits/pixel (RGB565) on the control interface
    }, 1},

    {ST7789_MADCTL, {
        0x60,  // MX=1, MV=1: row/col swap + column order -> landscape
    }, 1},

    {ST7789_PORCTRL, {
        0x0C,  // back porch, normal mode (BPA)
        0x0C,  // front porch, normal mode (FPA)
        0x00,  // separate porch control disabled (PSEN=0)
        0x33,  // back/front porch, idle mode (BPB/FPB)
        0x33,  // back/front porch, partial mode (BPC/FPC)
    }, 5},

    {ST7789_GCTRL, {
        0x35,  // VGH = +13.26 V, VGL = -10.43 V
    }, 1},

    {ST7789_VCOMS, {
        0x19,  // VCOM = 0.725 V
    }, 1},

    {ST7789_LCMCTRL, {
        0x2C,  // LCM control: XMH, XMX, XBGR enabled
    }, 1},

    {ST7789_VDVVRHEN, {
        0x01,  // CMDEN=1: take VDV/VRH from commands below, not from NVM
    }, 1},

    {ST7789_VRHS, {
        0x12,  // VRH = VAP/VAN drive level (~4.45 V)
    }, 1},

    {ST7789_VDVSET, {
        0x20,  // VDV = 0 V (default)
    }, 1},

    {ST7789_FRCTRL2, {
        0x0F,  // frame rate 60 Hz in normal mode
    }, 1},

    {ST7789_PWCTRL1, {
        0xA4,  // fixed (reserved) first byte
        0xA1,  // AVDD = 6.8 V, AVCL = -4.8 V, VDS = 2.3 V
    }, 2},

    {ST7789_PVGAMCTRL, {                      // positive voltage gamma curve
        0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F,
        0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23,
    }, 14},

    {ST7789_NVGAMCTRL, {                      // negative voltage gamma curve
        0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F,
        0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23,
    }, 14},

    {ST7789_INVON, {0}, 0},                   // inversion on (IPS RGB565 panel)
    {ST7789_DISPON, {0}, ST7789_CMD_DELAY},   // display on, then wait 120 ms
};

int st7789v_esp_driver_init(
    esp_lcd_panel_io_color_trans_done_cb_t bus_transmission_complete_cb) {
  gpio_config_t rd_gpio_config = {.mode = GPIO_MODE_OUTPUT,
                                  .pin_bit_mask = 1ULL << CONFIG_ST7789_HW_LCD_RD_GPIO};
  ESP_ERROR_CHECK(gpio_config(&rd_gpio_config));
  gpio_set_level(CONFIG_ST7789_HW_LCD_RD_GPIO, 1);

  esp_lcd_i80_bus_handle_t i80_bus = NULL;
  esp_lcd_i80_bus_config_t i80_bus_config = {
      .dc_gpio_num = CONFIG_ST7789_HW_LCD_DC_GPIO,
      .wr_gpio_num = CONFIG_ST7789_HW_LCD_WR_GPIO,
      .clk_src = LCD_CLK_SRC_DEFAULT,
      .data_gpio_nums =
          {
              CONFIG_ST7789_HW_LCD_DATA0_GPIO,
              CONFIG_ST7789_HW_LCD_DATA1_GPIO,
              CONFIG_ST7789_HW_LCD_DATA2_GPIO,
              CONFIG_ST7789_HW_LCD_DATA3_GPIO,
              CONFIG_ST7789_HW_LCD_DATA4_GPIO,
              CONFIG_ST7789_HW_LCD_DATA5_GPIO,
              CONFIG_ST7789_HW_LCD_DATA6_GPIO,
              CONFIG_ST7789_HW_LCD_DATA7_GPIO,
          },
      .bus_width = 8,
      .max_transfer_bytes = LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t),
      .psram_trans_align = 64,
      .sram_trans_align = 4};
  esp_lcd_new_i80_bus(&i80_bus_config, &i80_bus);

  esp_lcd_panel_io_i80_config_t io_config = {
      .cs_gpio_num = CONFIG_ST7789_HW_LCD_CS_GPIO,
      .pclk_hz = LCD_PIXEL_CLOCK_HZ,
      .trans_queue_depth = 10,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
      .dc_levels =
          {
              .dc_idle_level = 0,
              .dc_cmd_level = 0,
              .dc_dummy_level = 0,
              .dc_data_level = 1,
          },
      .on_color_trans_done = bus_transmission_complete_cb,
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &io_handle));

  esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = CONFIG_ST7789_HW_LCD_RST_GPIO,
      .color_space = ESP_LCD_COLOR_SPACE_RGB,
      .bits_per_pixel = 16,
      .vendor_config = NULL};
  esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle);

  esp_lcd_panel_reset(panel_handle);

  esp_lcd_panel_init(panel_handle);

  esp_lcd_panel_invert_color(panel_handle, true);

  esp_lcd_panel_swap_xy(panel_handle, true);

  esp_lcd_panel_mirror(panel_handle, false, true);

  esp_lcd_panel_set_gap(panel_handle, 0, 35);

  for (uint8_t i = 0;
       i < (sizeof(st7789v_init_sequence) / sizeof(st7789v_cmd_t)); i++) {
    esp_lcd_panel_io_tx_param(io_handle, st7789v_init_sequence[i].addr,
                              st7789v_init_sequence[i].param,
                              st7789v_init_sequence[i].len & ST7789_LEN_MASK);
    if (st7789v_init_sequence[i].len & ST7789_CMD_DELAY) {
      vTaskDelay(pdMS_TO_TICKS(120));
    }
  }

  esp_lcd_panel_disp_on_off(panel_handle, true);

  gpio_config_t bl_config = {.mode = GPIO_MODE_OUTPUT,
                             .pin_bit_mask = 1ULL << CONFIG_ST7789_HW_LCD_BL_GPIO};
  ESP_ERROR_CHECK(gpio_config(&bl_config));
  gpio_set_level(CONFIG_ST7789_HW_LCD_BL_GPIO, 1);

  return 0;
}

void st7789v_esp_driver_draw_bitmap(uint16_t x, uint16_t y, uint16_t width,
                                    uint16_t height, uint16_t *data) {
  esp_lcd_panel_draw_bitmap(panel_handle, x, y, x + width, y + height, data);
}
