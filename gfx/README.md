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
`GFX_RGB888_TO_RGB565(r, g, b)` builds colors. It is a **macro / constant
expression**, so it can initialize ROM-stored tables and `static const`
palettes; `gfx_rgb888_to_rgb565()` remains as a lowercase alias.

```c
static const gfx_color_t palette[] = {     /* lives in flash, not RAM */
    GFX_RGB888_TO_RGB565(255, 0, 0),
    GFX_RGB888_TO_RGB565(0, 255, 0),
};
gfx_color_t red = gfx_map_rgb565(&g, GFX_RGB888_TO_RGB565(255, 0, 0));
gfx_circle(&g, 80, 60, 30, 3, red, true, gfx_map_rgb565(&g, 0xFFFF));
gfx_flush(&g);
```

### Byte order (`GFX_SWAP_COLOR_BYTES`)

Most SPI/i80 panels latch RGB565 big-endian while the host is little-endian.
Define `GFX_SWAP_COLOR_BYTES` to `1` (a `-DGFX_SWAP_COLOR_BYTES=1` build flag,
or before including the header) and `GFX_RGB888_TO_RGB565` emits each color
pre-swapped. The framebuffer then already holds display byte order, so the glue
can blit it straight to the panel with **no per-pixel swap** — trading a one-off
cost at color-definition time for none at flush time. Defaults to `0` (host
order); when enabled, treat stored colors as opaque (don't feed them back to
`gfx_rgb565_to_mono()` / `gfx_rgb565_to_rgb888()`).

## Glue layer (lives in your app, not in this component)

Both ILI9341 and ST7789 latch RGB565 **big-endian**. With
`GFX_SWAP_COLOR_BYTES = 1` the framebuffer already holds that order, so the
glue is a straight passthrough:

```c
#define GFX_SWAP_COLOR_BYTES 1
#include "gfx.h"

#if DISPLAY_DRIVER == DISPLAY_ILI9341
#include "ili9341_esp_driver.h"
#define lcd_draw_bitmap ili9341_esp_driver_draw_bitmap
#else
#include "st7789v_esp_driver.h"
#define lcd_draw_bitmap st7789v_esp_driver_draw_bitmap
#endif

static void panel_flush(void *ctx, uint16_t x, uint16_t y,
                        uint16_t w, uint16_t h, const void *pixels)
{
    lcd_draw_bitmap(x, y, w, h, (uint16_t *)pixels);
    /* wait for the transfer to finish before the next flush reuses the fb */
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

## Text (`gfx_font.h`)

A `struct gfx_textbox` carries the style: `font`, `fg_color`, `bg_color`,
`transparent` (when true, only glyph pixels are drawn so the existing
background shows through), `auto_size` (when true, `gfx_text_place()` sets
`width`/`height` to fit the text), an integer `scale`, and the `x0,y0,width,
height` rectangle — readable back via `gfx_textbox_get_bounds()`.

```c
#include "gfx_font.h"
#include "font_dejavu16.h"        /* generated; declares extern font_dejavu16 */

struct gfx_textbox tb;
gfx_textbox_init(&tb, &font_dejavu16);   /* scale=1, auto_size, opaque */
tb.fg_color = white;
tb.bg_color = black;       /* set tb.transparent = true to skip the fill */
tb.x0 = 20; tb.y0 = 20;
gfx_text_place(&g, &tb, "Hello\nworld");  /* '\n' starts a new line */

uint16_t x0, y0, w, h;
gfx_textbox_get_bounds(&tb, &x0, &y0, &w, &h);   /* fits the text */
```

### Generating fonts

Fonts are bitmap fonts rasterized at a fixed pixel size by
`tools/otf_to_gfxfont.py` (needs Pillow). It emits `<name>.c` (data) and
`<name>.h` (the `extern const struct gfx_font`):

```sh
python3 tools/otf_to_gfxfont.py \
    --font /path/to/Font.otf --size 16 --range 32-126 \
    --name font_myfont --output main/font_myfont.c
```

Two source kinds are supported:

- **Scalable outline fonts** (TTF/OTF): rendered at any `--size`.
- **Embedded-bitmap fonts** (OTB "OpenType Bitmap", and BDF/PCF): these exist
  only at fixed *strike* sizes. List them with `--list-sizes`, then pass a
  matching `--size`. If `--size` isn't a strike, the nearest strike is used and
  a note is printed.

```sh
python3 tools/otf_to_gfxfont.py --font Bm437_IBM_VGA_8x16.otb --list-sizes
# bitmap strike sizes: 16
python3 tools/otf_to_gfxfont.py --font Bm437_IBM_VGA_8x16.otb \
    --size 16 --name font_vga --output main/font_vga.c
```

For DOS/PC fonts whose extended range (128-255) holds box-drawing and shade
glyphs, pass `--codepage cp437`: table indices are then treated as CP437 bytes
(so `0xDB` → █, `0xB0` → ░), letting the C side index glyphs by raw byte value.

```sh
python3 tools/otf_to_gfxfont.py --font Bm437_IBM_VGA_8x16.otb \
    --size 16 --range 32-255 --codepage cp437 \
    --name font_vga437 --output main/font_vga437.c
```

Add the generated `.c` to your component's `SRCS`. Glyphs are 1 bpp, so the
font color is solid; `transparent` controls whether non-glyph pixels are
filled with `bg_color` or left untouched.

## Notes

- `gfx_init()` uses `calloc`; for DMA-capable or PSRAM framebuffers allocate it
  yourself and call `gfx_init_static()`.
- The core has no ESP-IDF dependency — it is plain C99 and portable.
