# ESP32 components

ESP-IDF project with reusable components.

## ILI9341 SPI driver

320×240 ILI9341 panel over SPI, RGB565.

- `ili9341_esp_driver_init(on_trans_done_cb)` — bring up the SPI bus and panel
  IO, run the datasheet init sequence (named commands, per-value comments),
  turn on the backlight. The callback fires when a color transfer completes.
- `ili9341_esp_driver_draw_bitmap(x, y, width, height, data)` — blit an RGB565
  buffer to a rectangle (CASET/PASET/RAMWR).
- `LCD_WIDTH` / `LCD_HEIGHT` exported from the header.

Pins are configured in **menuconfig** under *ILI9341* (Freenove ESP32-S3 preset
or a custom pin map).

## ST7789V parallel (i80) driver

320×170 ST7789V panel over an 8-bit i80 parallel bus, RGB565. Built on the
ESP-IDF `esp_lcd` ST7789 vendor driver plus a commented init table.

- `st7789v_esp_driver_init(on_trans_done_cb)` — configure the i80 bus, panel
  IO, orientation/inversion, run the init sequence, enable the backlight.
- `st7789v_esp_driver_draw_bitmap(x, y, width, height, data)` — blit an RGB565
  buffer via `esp_lcd_panel_draw_bitmap`.
- `LCD_WIDTH` / `LCD_HEIGHT` exported from the header.

Pins are configured in **menuconfig** under *ST7789V* (Lilygo T-Display S3
preset or a custom pin map).

Both drivers expose the **same shape** (`*_init` + `*_draw_bitmap` +
`LCD_WIDTH`/`LCD_HEIGHT`), so an app can switch panels with a single
compile-time define.

## Hardware-agnostic 2D graphics

Framebuffer-backed drawing library with **zero** panel/driver dependencies
(plain C99). Output goes through one `flush` callback that the application's
glue layer wires to a panel's `draw_bitmap`.

Capabilities:

- Canvas formats: `GFX_FMT_RGB565` (16-bit color) or `GFX_FMT_MONO8`
  (8-bit brightness, 0–255).
- In-RAM framebuffer, so pixels are readable back (`getpixel`).
- Internally allocated or caller-provided (PSRAM/DMA) framebuffer.

Functions:

- Lifecycle: `gfx_init`, `gfx_init_static`, `gfx_deinit`.
- Pixels: `gfx_putpixel`, `gfx_getpixel`, `gfx_clear`.
- Primitives: `gfx_line`, `gfx_rect`, `gfx_circle` (each with thickness; rect
  and circle support fill + border in separate colors).
- Present: `gfx_flush` (whole canvas), `gfx_flush_area` (sub-rectangle).
- Color helpers (`gfx_color.h`): `gfx_rgb888_to_rgb565`, `gfx_rgb565_to_rgb888`,
  `gfx_rgb565_to_mono`, `gfx_mono`, `gfx_rgb565_bswap`, and `gfx_map_rgb565`
  (map an RGB565 color to the canvas' native encoding).

See `component/gfx/README.md` for the API model and a glue-layer example.

