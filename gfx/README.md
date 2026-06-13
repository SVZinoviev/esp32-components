# gfx — hardware-agnostic 2D graphics

A tiny framebuffer-backed drawing library: `putpixel`, `getpixel`, `line`,
`rect`, `circle`, plus RGB888→RGB565 / luma color helpers. It knows nothing
about any panel — all output goes through a single `flush` callback that the
**glue layer (outside this component)** provides.

## Model

- A `struct gfx` canvas owns an in-RAM framebuffer in either `GFX_FMT_RGB565`
  (`uint16_t` per pixel) or `GFX_FMT_MONO8` (`uint8_t` brightness 0..255).
- All primitives draw into that framebuffer, so `getpixel` works even though
  real panels are write-only.
- `gfx_flush()` / `gfx_flush_area()` hand packed pixels to your `flush`
  callback, which writes them to the panel.

## Color

`gfx_color_t` is RGB565 on a color canvas, or brightness 0..255 on a mono one.
Use `gfx_rgb888_to_rgb565()` to build colors and `gfx_map_rgb565()` to let
format-independent code target either canvas:

```c
gfx_color_t red = gfx_map_rgb565(&g, gfx_rgb888_to_rgb565(255, 0, 0));
gfx_circle(&g, 80, 60, 30, 3, red, true, gfx_map_rgb565(&g, 0xFFFF));
gfx_flush(&g);
```

## Glue layer (lives in your app, not in this component)

Both ILI9341 and ST7789 latch RGB565 **big-endian**, while the framebuffer is
host (little-endian) order, so the glue swaps bytes on the way out. If your
panel/driver is configured to swap in hardware, drop the swap.

```c
#include "gfx.h"

#if DISPLAY_DRIVER == DISPLAY_ILI9341
#include "ili9341_esp_driver.h"
#define lcd_draw_bitmap ili9341_esp_driver_draw_bitmap
#else
#include "st7789v_esp_driver.h"
#define lcd_draw_bitmap st7789v_esp_driver_draw_bitmap
#endif

/* One scratch line buffer reused across flush calls. */
static uint16_t s_swap[LCD_WIDTH];

static void panel_flush(void *ctx, uint16_t x, uint16_t y,
                        uint16_t w, uint16_t h, const void *pixels)
{
    const uint16_t *src = (const uint16_t *)pixels;
    for (uint16_t row = 0; row < h; row++) {
        for (uint16_t col = 0; col < w; col++) {
            s_swap[col] = gfx_rgb565_bswap(src[row * w + col]);
        }
        lcd_draw_bitmap(x, y + row, w, 1, s_swap);
    }
}

void display_gfx_init(struct gfx *g)
{
    static const struct gfx_display disp = {
        .width   = LCD_WIDTH,
        .height  = LCD_HEIGHT,
        .format  = GFX_FMT_RGB565,
        .flush   = panel_flush,
        .user_ctx = NULL,
    };
    gfx_init(g, &disp);          /* or gfx_init_static() with a PSRAM buffer */
}
```

A monochrome OLED glue is identical except `.format = GFX_FMT_MONO8` and a
`flush` that ships the `uint8_t` brightness buffer (no byte swap).

## Notes

- `gfx_init()` uses `calloc`; for DMA-capable or PSRAM framebuffers allocate it
  yourself and call `gfx_init_static()`.
- The core has no ESP-IDF dependency — it is plain C99 and portable.
