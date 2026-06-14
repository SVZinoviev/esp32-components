#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One glyph in a ::gfx_font.
 *
 * The bitmap is 1 bit per pixel, row-major, MSB first, packed as a continuous
 * bit stream (no per-row byte padding) starting at @ref bitmap_offset bytes
 * into the font's bitmap blob. Position is given relative to the pen: the
 * top-left of the glyph bitmap sits at
 * (pen_x + @ref x_offset, baseline_y + @ref y_offset).
 */
struct gfx_glyph {
    uint32_t bitmap_offset;  /**< Byte offset of this glyph in font->bitmap.   */
    uint16_t width;          /**< Glyph bitmap width in pixels.                */
    uint16_t height;         /**< Glyph bitmap height in pixels.               */
    uint16_t x_advance;      /**< Pen advance after drawing, in pixels.        */
    int16_t  x_offset;       /**< Left bearing (pen to bitmap left edge).      */
    int16_t  y_offset;       /**< Bitmap top relative to baseline (up = neg).  */
};

/**
 * @brief A bitmap font covering a contiguous range of code points.
 *
 * Generated from an OTF/TTF by `tools/otf_to_gfxfont.py`.
 */
struct gfx_font {
    const uint8_t *bitmap;          /**< Packed 1-bpp glyph bitmaps.           */
    const struct gfx_glyph *glyphs; /**< One entry per code point in range.    */
    uint16_t first_char;            /**< First code point (e.g. 0x20 space).   */
    uint16_t last_char;             /**< Last code point (inclusive).          */
    uint16_t y_advance;             /**< Line height (newline spacing), px.    */
    uint16_t ascent;                /**< Top of line to baseline, in pixels.   */
};

/**
 * @brief A placed block of text and its rendering style.
 *
 * Fill @ref font, the colors, and the flags, then call gfx_text_place(). The
 * box rectangle (@ref x0, @ref y0, @ref width, @ref height) can be read back
 * afterwards (see gfx_textbox_get_bounds()); when @ref auto_size is set, the
 * placing function computes @ref width and @ref height from the text.
 */
struct gfx_textbox {
    const struct gfx_font *font;  /**< Font (and therefore base pixel size).   */
    gfx_color_t fg_color;         /**< Glyph (font) color.                     */
    gfx_color_t bg_color;         /**< Background color (used if !transparent). */
    bool transparent;             /**< If true, only glyph pixels are drawn;
                                       the background shows through.            */
    bool auto_size;               /**< If true, gfx_text_place() sets width and
                                       height to fit the text.                  */
    uint8_t scale;                /**< Integer magnification, >= 1.            */
    uint16_t x0;                  /**< Box top-left X (input).                 */
    uint16_t y0;                  /**< Box top-left Y (input).                 */
    uint16_t width;               /**< Box width in pixels (in or out).        */
    uint16_t height;              /**< Box height in pixels (in or out).       */
};

/**
 * @brief Initialize a text box with sensible defaults.
 *
 * Sets @p font, scale = 1, transparent = false, auto_size = true, position
 * (0, 0), zero size, and both colors 0. Adjust fields afterwards as needed.
 *
 * @param tb Text box to initialize.
 * @param font Font to use (must outlive the box).
 */
void gfx_textbox_init(struct gfx_textbox *tb, const struct gfx_font *font);

/**
 * @brief Measure the pixel size a string would occupy.
 *
 * Honors @ref gfx_textbox::scale and embedded newlines (`\n`). Does not draw.
 *
 * @param tb Text box providing the font and scale.
 * @param text NUL-terminated string ('\n' starts a new line).
 * @param[out] width Destination for the width in pixels (may be NULL).
 * @param[out] height Destination for the height in pixels (may be NULL).
 */
void gfx_text_measure(const struct gfx_textbox *tb, const char *text,
                      uint16_t *width, uint16_t *height);

/**
 * @brief Render text into a canvas at the text box's position.
 *
 * If @ref gfx_textbox::auto_size is set, @ref gfx_textbox::width and
 * @ref gfx_textbox::height are first set to fit @p text. When
 * @ref gfx_textbox::transparent is false the box rectangle is filled with
 * @ref gfx_textbox::bg_color first; when true only glyph pixels are written
 * (leaving the existing background intact). Drawing is clipped to the box
 * rectangle. Newlines (`\n`) advance to the next line.
 *
 * @param gfx Target canvas.
 * @param tb Text box (style + position); updated in place if auto_size.
 * @param text NUL-terminated string to render.
 */
void gfx_text_place(struct gfx *gfx, struct gfx_textbox *tb, const char *text);

/**
 * @brief Retrieve the box rectangle in pixels.
 *
 * Reflects the latest values, including any size computed by gfx_text_place()
 * when @ref gfx_textbox::auto_size is set.
 *
 * @param tb Text box to query.
 * @param[out] x0 Destination for the top-left X (may be NULL).
 * @param[out] y0 Destination for the top-left Y (may be NULL).
 * @param[out] width Destination for the width (may be NULL).
 * @param[out] height Destination for the height (may be NULL).
 */
void gfx_textbox_get_bounds(const struct gfx_textbox *tb, uint16_t *x0,
                            uint16_t *y0, uint16_t *width, uint16_t *height);

#ifdef __cplusplus
}
#endif
