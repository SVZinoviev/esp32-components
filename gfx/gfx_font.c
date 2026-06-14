#include "gfx_font.h"

#include <stddef.h>

/* --- helpers -------------------------------------------------------------- */

static uint8_t tb_scale(const struct gfx_textbox *tb)
{
    return (tb->scale == 0) ? 1 : tb->scale;
}

/* Look up a glyph by code point, or NULL if outside the font's range. */
static const struct gfx_glyph *glyph_for(const struct gfx_font *font, char c)
{
    uint16_t cp = (uint8_t)c;
    if (cp < font->first_char || cp > font->last_char) {
        return NULL;
    }
    return &font->glyphs[cp - font->first_char];
}

/* --- public API ----------------------------------------------------------- */

void gfx_textbox_init(struct gfx_textbox *tb, const struct gfx_font *font)
{
    if (!tb) {
        return;
    }
    tb->font = font;
    tb->fg_color = 0;
    tb->bg_color = 0;
    tb->transparent = false;
    tb->auto_size = true;
    tb->scale = 1;
    tb->x0 = 0;
    tb->y0 = 0;
    tb->width = 0;
    tb->height = 0;
}

void gfx_text_measure(const struct gfx_textbox *tb, const char *text,
                      uint16_t *width, uint16_t *height)
{
    uint32_t max_w = 0;
    uint32_t line_w = 0;
    uint32_t lines = 1;

    if (tb && tb->font && text) {
        const struct gfx_font *font = tb->font;
        for (const char *p = text; *p; p++) {
            if (*p == '\n') {
                if (line_w > max_w) max_w = line_w;
                line_w = 0;
                lines++;
                continue;
            }
            const struct gfx_glyph *g = glyph_for(font, *p);
            if (g) {
                line_w += g->x_advance;
            }
        }
        if (line_w > max_w) max_w = line_w;
    }

    uint8_t scale = tb ? tb_scale(tb) : 1;
    uint32_t y_adv = (tb && tb->font) ? tb->font->y_advance : 0;
    if (width) *width = (uint16_t)(max_w * scale);
    if (height) *height = (uint16_t)(lines * y_adv * scale);
}

/* Draw one glyph's set pixels in fg_color, clipped to the box rectangle. */
static void draw_glyph(struct gfx *gfx, const struct gfx_textbox *tb,
                       const struct gfx_glyph *g, int pen_x, int baseline_y)
{
    const uint8_t *bmp = tb->font->bitmap + g->bitmap_offset;
    int scale = tb_scale(tb);
    int box_x1 = tb->x0 + tb->width;   /* exclusive */
    int box_y1 = tb->y0 + tb->height;  /* exclusive */
    uint32_t bit = 0;

    for (uint16_t row = 0; row < g->height; row++) {
        for (uint16_t col = 0; col < g->width; col++) {
            uint8_t on = (bmp[bit >> 3] << (bit & 7)) & 0x80;
            bit++;
            if (!on) {
                continue;
            }
            int px = pen_x + (g->x_offset + col) * scale;
            int py = baseline_y + (g->y_offset + row) * scale;
            for (int sy = 0; sy < scale; sy++) {
                for (int sx = 0; sx < scale; sx++) {
                    int x = px + sx;
                    int y = py + sy;
                    if (x >= tb->x0 && x < box_x1 && y >= tb->y0 &&
                        y < box_y1) {
                        gfx_putpixel(gfx, (uint16_t)x, (uint16_t)y, tb->fg_color);
                    }
                }
            }
        }
    }
}

void gfx_text_place(struct gfx *gfx, struct gfx_textbox *tb, const char *text)
{
    if (!gfx || !tb || !tb->font || !text) {
        return;
    }

    if (tb->auto_size) {
        gfx_text_measure(tb, text, &tb->width, &tb->height);
    }
    if (tb->width == 0 || tb->height == 0) {
        return;
    }

    if (!tb->transparent) {
        gfx_rect(gfx, tb->x0, tb->y0, tb->x0 + tb->width - 1,
                 tb->y0 + tb->height - 1, 0, tb->bg_color, true, tb->bg_color);
    }

    int scale = tb_scale(tb);
    int pen_x = tb->x0;
    int baseline_y = tb->y0 + tb->font->ascent * scale;

    for (const char *p = text; *p; p++) {
        if (*p == '\n') {
            pen_x = tb->x0;
            baseline_y += tb->font->y_advance * scale;
            continue;
        }
        const struct gfx_glyph *g = glyph_for(tb->font, *p);
        if (!g) {
            continue;
        }
        draw_glyph(gfx, tb, g, pen_x, baseline_y);
        pen_x += g->x_advance * scale;
    }
}

void gfx_textbox_get_bounds(const struct gfx_textbox *tb, uint16_t *x0,
                            uint16_t *y0, uint16_t *width, uint16_t *height)
{
    if (!tb) {
        return;
    }
    if (x0) *x0 = tb->x0;
    if (y0) *y0 = tb->y0;
    if (width) *width = tb->width;
    if (height) *height = tb->height;
}
