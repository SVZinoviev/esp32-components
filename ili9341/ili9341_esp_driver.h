#pragma once

#include "esp_lcd_types.h"

#define LCD_WIDTH             320
#define LCD_HEIGHT            240

int ili9341_esp_driver_init(
    esp_lcd_panel_io_color_trans_done_cb_t bus_transmission_complete_cb);
void ili9341_esp_driver_draw_bitmap(uint16_t x, uint16_t y, uint16_t width,
                                    uint16_t hight, uint16_t *data);
