#include "gfx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* --- internal helpers ----------------------------------------------------- */

static uint8_t bytes_per_px(gfx_color_format_t fmt)
{
    return (fmt == GFX_FMT_MONO8) ? 1 : 2;
}

/* Write one pixel with bounds checking. Coordinates are signed so callers
 * (line/circle math) can pass transiently out-of-range values. */
static inline void put_raw(struct gfx *gfx, int x, int y, gfx_color_t color)
{
    if (x < 0 || y < 0 || x >= gfx->display.width || y >= gfx->display.height) {
        return;
    }
    size_t idx = (size_t)y * gfx->display.width + (size_t)x;
    if (gfx->bytes_per_px == 2) {
        ((uint16_t *)gfx->fb)[idx] = color;
    } else {
        gfx->fb[idx] = (uint8_t)color;
    }
}

/* Inclusive axis-aligned fill, clipped to the canvas. */
static void fill_span(struct gfx *gfx, int x0, int x1, int y, gfx_color_t color)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    for (int x = x0; x <= x1; x++) {
        put_raw(gfx, x, y, color);
    }
}

static void fill_rect_raw(struct gfx *gfx, int x0, int y0, int x1, int y1,
                          gfx_color_t color)
{
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; y++) {
        fill_span(gfx, x0, x1, y, color);
    }
}

/* Stamp a filled disc of the given diameter, centered at (cx, cy). Used to
 * give lines and circle outlines their thickness. */
static void stamp(struct gfx *gfx, int cx, int cy, int thick, gfx_color_t color)
{
    if (thick <= 1) {
        put_raw(gfx, cx, cy, color);
        return;
    }
    int r = thick / 2;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                put_raw(gfx, cx + dx, cy + dy, color);
            }
        }
    }
}

/* --- lifecycle ------------------------------------------------------------ */

gfx_err_t gfx_init_static(struct gfx *gfx, const struct gfx_display *display,
                          void *framebuffer, size_t framebuffer_size)
{
    if (!gfx || !display || !display->flush || !framebuffer ||
        display->width == 0 || display->height == 0) {
        return GFX_ERR_INVALID_ARG;
    }
    uint8_t bpp = bytes_per_px(display->format);
    size_t need = (size_t)display->width * display->height * bpp;
    if (framebuffer_size < need) {
        return GFX_ERR_INVALID_ARG;
    }
    gfx->display = *display;
    gfx->fb = (uint8_t *)framebuffer;
    gfx->bytes_per_px = bpp;
    gfx->fb_owned = false;
    return GFX_OK;
}

gfx_err_t gfx_init(struct gfx *gfx, const struct gfx_display *display)
{
    if (!gfx || !display || !display->flush || display->width == 0 ||
        display->height == 0) {
        return GFX_ERR_INVALID_ARG;
    }
    uint8_t bpp = bytes_per_px(display->format);
    size_t count = (size_t)display->width * display->height;
    void *fb = calloc(count, bpp);
    if (!fb) {
        return GFX_ERR_NO_MEM;
    }
    gfx->display = *display;
    gfx->fb = (uint8_t *)fb;
    gfx->bytes_per_px = bpp;
    gfx->fb_owned = true;
    return GFX_OK;
}

void gfx_deinit(struct gfx *gfx)
{
    if (!gfx) {
        return;
    }
    if (gfx->fb_owned) {
        free(gfx->fb);
    }
    gfx->fb = NULL;
    gfx->fb_owned = false;
}

/* --- color ---------------------------------------------------------------- */

gfx_color_t gfx_map_rgb565(const struct gfx *gfx, gfx_color_t rgb565)
{
    if (gfx && gfx->display.format == GFX_FMT_MONO8) {
        return gfx_mono(gfx_rgb565_to_mono(rgb565));
    }
    return rgb565;
}

/* --- pixels --------------------------------------------------------------- */

void gfx_putpixel(struct gfx *gfx, uint16_t x, uint16_t y, gfx_color_t color)
{
    if (!gfx) {
        return;
    }
    put_raw(gfx, x, y, color);
}

gfx_color_t gfx_getpixel(const struct gfx *gfx, uint16_t x, uint16_t y)
{
    if (!gfx || x >= gfx->display.width || y >= gfx->display.height) {
        return 0;
    }
    size_t idx = (size_t)y * gfx->display.width + (size_t)x;
    if (gfx->bytes_per_px == 2) {
        return ((const uint16_t *)gfx->fb)[idx];
    }
    return gfx->fb[idx];
}

void gfx_clear(struct gfx *gfx, gfx_color_t color)
{
    if (!gfx) {
        return;
    }
    size_t count = (size_t)gfx->display.width * gfx->display.height;
    if (gfx->bytes_per_px == 1) {
        memset(gfx->fb, (uint8_t)color, count);
    } else {
        uint16_t *p = (uint16_t *)gfx->fb;
        for (size_t i = 0; i < count; i++) {
            p[i] = color;
        }
    }
}

/* --- primitives ----------------------------------------------------------- */

void gfx_line(struct gfx *gfx, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
              uint16_t thick, gfx_color_t color)
{
    if (!gfx) {
        return;
    }
    int x0 = x1, y0 = y1, xe = x2, ye = y2;
    int dx = abs(xe - x0);
    int dy = -abs(ye - y0);
    int sx = (x0 < xe) ? 1 : -1;
    int sy = (y0 < ye) ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        stamp(gfx, x0, y0, thick, color);
        if (x0 == xe && y0 == ye) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_rect(struct gfx *gfx, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
              uint16_t thick, gfx_color_t line_color, bool filled,
              gfx_color_t fill_color)
{
    if (!gfx) {
        return;
    }
    int xa = x1, xb = x2, ya = y1, yb = y2;
    if (xa > xb) { int t = xa; xa = xb; xb = t; }
    if (ya > yb) { int t = ya; ya = yb; yb = t; }

    if (filled) {
        fill_rect_raw(gfx, xa, ya, xb, yb, fill_color);
    }
    if (thick == 0) {
        return;
    }

    /* Clamp the border so the four bands never overshoot the rectangle. */
    int t = thick;
    int w = xb - xa + 1;
    int h = yb - ya + 1;
    if (t * 2 > w) t = w / 2 + 1;
    if (t * 2 > h) t = h / 2 + 1;

    fill_rect_raw(gfx, xa, ya, xb, ya + t - 1, line_color);  /* top    */
    fill_rect_raw(gfx, xa, yb - t + 1, xb, yb, line_color);  /* bottom */
    fill_rect_raw(gfx, xa, ya, xa + t - 1, yb, line_color);  /* left   */
    fill_rect_raw(gfx, xb - t + 1, ya, xb, yb, line_color);  /* right  */
}

void gfx_circle(struct gfx *gfx, uint16_t center_x, uint16_t center_y,
                uint16_t radius_in_pixels, uint16_t thickness,
                gfx_color_t line_color, bool filled, gfx_color_t fill_color)
{
    if (!gfx) {
        return;
    }
    int cx = center_x, cy = center_y, r = radius_in_pixels;

    if (filled && r > 0) {
        for (int dy = -r; dy <= r; dy++) {
            int span = (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
            fill_span(gfx, cx - span, cx + span, cy + dy, fill_color);
        }
    }
    if (thickness == 0) {
        return;
    }

    /* Midpoint circle: stamp a thick disc at the eight symmetric points. */
    int x = r, y = 0, err = 0;
    while (x >= y) {
        stamp(gfx, cx + x, cy + y, thickness, line_color);
        stamp(gfx, cx + y, cy + x, thickness, line_color);
        stamp(gfx, cx - y, cy + x, thickness, line_color);
        stamp(gfx, cx - x, cy + y, thickness, line_color);
        stamp(gfx, cx - x, cy - y, thickness, line_color);
        stamp(gfx, cx - y, cy - x, thickness, line_color);
        stamp(gfx, cx + y, cy - x, thickness, line_color);
        stamp(gfx, cx + x, cy - y, thickness, line_color);
        y++;
        if (err <= 0) {
            err += 2 * y + 1;
        }
        if (err > 0) {
            x--;
            err -= 2 * x + 1;
        }
    }
}

/* --- presentation --------------------------------------------------------- */

gfx_err_t gfx_flush(struct gfx *gfx)
{
    if (!gfx || !gfx->display.flush) {
        return GFX_ERR_INVALID_ARG;
    }
    gfx->display.flush(gfx->display.user_ctx, 0, 0, gfx->display.width,
                       gfx->display.height, gfx->fb);
    return GFX_OK;
}

gfx_err_t gfx_flush_area(struct gfx *gfx, uint16_t x, uint16_t y, uint16_t width,
                         uint16_t height)
{
    if (!gfx || !gfx->display.flush) {
        return GFX_ERR_INVALID_ARG;
    }
    /* Clip to the canvas. */
    if (x >= gfx->display.width || y >= gfx->display.height || width == 0 ||
        height == 0) {
        return GFX_ERR_INVALID_ARG;
    }
    if (x + width > gfx->display.width) {
        width = gfx->display.width - x;
    }
    if (y + height > gfx->display.height) {
        height = gfx->display.height - y;
    }

    /* Fast path: a full-width region is already contiguous in the fb. */
    uint8_t bpp = gfx->bytes_per_px;
    if (x == 0 && width == gfx->display.width) {
        const uint8_t *row = gfx->fb + (size_t)y * width * bpp;
        gfx->display.flush(gfx->display.user_ctx, x, y, width, height, row);
        return GFX_OK;
    }

    /* General path: copy the sub-rectangle into a packed temporary buffer. */
    uint8_t *tmp = (uint8_t *)malloc((size_t)width * height * bpp);
    if (!tmp) {
        return GFX_ERR_NO_MEM;
    }
    size_t dst_pitch = (size_t)width * bpp;
    size_t src_pitch = (size_t)gfx->display.width * bpp;
    for (uint16_t row = 0; row < height; row++) {
        const uint8_t *src =
            gfx->fb + (size_t)(y + row) * src_pitch + (size_t)x * bpp;
        memcpy(tmp + (size_t)row * dst_pitch, src, dst_pitch);
    }
    gfx->display.flush(gfx->display.user_ctx, x, y, width, height, tmp);
    free(tmp);
    return GFX_OK;
}
