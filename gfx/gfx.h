#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx_color.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Pixel storage format of a canvas. */
typedef enum {
    GFX_FMT_RGB565 = 0,  /**< 16 bits/pixel, packed RGB565.            */
    GFX_FMT_MONO8,       /**< 8 bits/pixel, brightness 0..255.         */
} gfx_color_format_t;

/** @brief Return codes used by the gfx library. */
typedef enum {
    GFX_OK = 0,               /**< Success.                            */
    GFX_ERR_INVALID_ARG = -1, /**< A NULL or out-of-range argument.    */
    GFX_ERR_NO_MEM = -2,      /**< Allocation failed.                  */
} gfx_err_t;

/**
 * @brief Blit callback that pushes a rectangle of pixels to the panel.
 *
 * Implemented by the glue layer (outside this component). The pixel buffer is
 * tightly packed (no row padding), @p width * @p height elements, in the
 * canvas color format. The buffer is owned by the caller and valid only for
 * the duration of the call unless the glue copies it.
 *
 * @param user_ctx Opaque pointer copied from struct gfx_display::user_ctx.
 * @param x Left coordinate of the destination rectangle.
 * @param y Top coordinate of the destination rectangle.
 * @param width Rectangle width in pixels.
 * @param height Rectangle height in pixels.
 * @param pixels Packed source pixels (uint16_t for RGB565, uint8_t for MONO8).
 */
typedef void (*gfx_flush_cb_t)(void *user_ctx, uint16_t x, uint16_t y,
                               uint16_t width, uint16_t height,
                               const void *pixels);

/**
 * @brief Hardware description supplied by the glue layer.
 *
 * The gfx core only ever calls the display's @c flush callback.
 */
struct gfx_display {
    uint16_t width;             /**< Visible width in pixels.          */
    uint16_t height;            /**< Visible height in pixels.         */
    gfx_color_format_t format;  /**< Framebuffer pixel format.         */
    gfx_flush_cb_t flush;       /**< Blit callback (must be non-NULL). */
    void *user_ctx;             /**< Passed verbatim to @ref flush.    */
};

/**
 * @brief A drawing surface: a display plus its backing framebuffer.
 *
 * Treat the fields as private; use the API below. Stored by value so a
 * struct gfx can outlive the struct gfx_display passed to gfx_init().
 */
struct gfx {
    struct gfx_display display;  /**< Copy of the display description.      */
    uint8_t *fb;                 /**< Framebuffer base pointer.             */
    uint8_t bytes_per_px;        /**< 1 (MONO8) or 2 (RGB565).              */
    bool fb_owned;               /**< True if gfx_deinit() must free fb.    */
};

/**
 * @brief Initialize a canvas, allocating its framebuffer internally.
 *
 * Allocates width * height * bytes-per-pixel bytes via calloc, so the canvas
 * starts cleared to color 0 (black). Pair with gfx_deinit().
 *
 * @param gfx Canvas to initialize.
 * @param display Display description; its contents are copied.
 * @return ::GFX_OK on success, ::GFX_ERR_INVALID_ARG for bad arguments,
 *         ::GFX_ERR_NO_MEM if the framebuffer could not be allocated.
 */
gfx_err_t gfx_init(struct gfx *gfx, const struct gfx_display *display);

/**
 * @brief Initialize a canvas over a caller-provided framebuffer.
 *
 * Use this to place the framebuffer in DMA-capable or external (PSRAM)
 * memory, or to share a static buffer. The buffer is not freed by
 * gfx_deinit().
 *
 * @param gfx Canvas to initialize.
 * @param display Display description; its contents are copied.
 * @param framebuffer Caller-owned pixel buffer.
 * @param framebuffer_size Size of @p framebuffer in bytes; must be at least
 *        width * height * bytes-per-pixel.
 * @return ::GFX_OK on success, ::GFX_ERR_INVALID_ARG for bad arguments or a
 *         buffer that is too small.
 */
gfx_err_t gfx_init_static(struct gfx *gfx, const struct gfx_display *display,
                          void *framebuffer, size_t framebuffer_size);

/**
 * @brief Release resources held by a canvas.
 *
 * Frees the framebuffer only if it was allocated by gfx_init(). Safe to call
 * on a zero-initialized or already-deinitialized canvas.
 *
 * @param gfx Canvas to tear down (may be NULL).
 */
void gfx_deinit(struct gfx *gfx);

/**
 * @brief Map an RGB565 color to the canvas' native color encoding.
 *
 * On an RGB565 canvas the value is returned unchanged; on a MONO8 canvas it
 * is converted to perceptual brightness. Lets drawing code stay color-format
 * independent.
 *
 * @param gfx Canvas whose format selects the mapping.
 * @param rgb565 Source color in RGB565.
 * @return The color encoded for @p gfx.
 */
gfx_color_t gfx_map_rgb565(const struct gfx *gfx, gfx_color_t rgb565);

/**
 * @brief Set a single pixel.
 *
 * Coordinates outside the canvas are silently clipped (no-op).
 *
 * @param gfx Target canvas.
 * @param x X coordinate, 0 = left.
 * @param y Y coordinate, 0 = top.
 * @param color Pixel color in the canvas format.
 */
void gfx_putpixel(struct gfx *gfx, uint16_t x, uint16_t y, gfx_color_t color);

/**
 * @brief Read a single pixel from the framebuffer.
 *
 * Reads back the in-RAM framebuffer (the panel itself is treated as
 * write-only), so this always reflects what was drawn since the last clear.
 *
 * @param gfx Source canvas.
 * @param x X coordinate, 0 = left.
 * @param y Y coordinate, 0 = top.
 * @return The pixel color, or 0 if the coordinates are out of range.
 */
gfx_color_t gfx_getpixel(const struct gfx *gfx, uint16_t x, uint16_t y);

/**
 * @brief Fill the entire canvas with one color.
 *
 * @param gfx Target canvas.
 * @param color Fill color in the canvas format.
 */
void gfx_clear(struct gfx *gfx, gfx_color_t color);

/**
 * @brief Draw a straight line with rounded thickness.
 *
 * Uses Bresenham's algorithm; for @p thick > 1 a filled disc of diameter
 * @p thick is stamped along the path, giving rounded ends and joins.
 *
 * @param gfx Target canvas.
 * @param x1 First endpoint X.
 * @param y1 First endpoint Y.
 * @param x2 Second endpoint X.
 * @param y2 Second endpoint Y.
 * @param thick Line thickness in pixels (0 or 1 = single pixel wide).
 * @param color Line color in the canvas format.
 */
void gfx_line(struct gfx *gfx, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
              uint16_t thick, gfx_color_t color);

/**
 * @brief Draw an axis-aligned rectangle, optionally filled.
 *
 * The two points are opposite corners in any order. When @p filled is true
 * the interior is painted with @p fill_color first, then a border of
 * @p thick pixels is drawn inset along the edges with @p line_color.
 *
 * @param gfx Target canvas.
 * @param x1 First corner X.
 * @param y1 First corner Y.
 * @param x2 Opposite corner X.
 * @param y2 Opposite corner Y.
 * @param thick Border thickness in pixels (0 = no border).
 * @param line_color Border color in the canvas format.
 * @param filled Whether to fill the interior.
 * @param fill_color Interior color when @p filled is true.
 */
void gfx_rect(struct gfx *gfx, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
              uint16_t thick, gfx_color_t line_color, bool filled,
              gfx_color_t fill_color);

/**
 * @brief Draw a circle, optionally filled.
 *
 * When @p filled is true a solid disc of @p fill_color is drawn first, then
 * an outline of @p thickness pixels in @p line_color is stamped on the
 * circumference.
 *
 * @param gfx Target canvas.
 * @param center_x Center X coordinate.
 * @param center_y Center Y coordinate.
 * @param radius_in_pixels Circle radius in pixels.
 * @param thickness Outline thickness in pixels (0 = no outline).
 * @param line_color Outline color in the canvas format.
 * @param filled Whether to fill the disc.
 * @param fill_color Fill color when @p filled is true.
 */
void gfx_circle(struct gfx *gfx, uint16_t center_x, uint16_t center_y,
                uint16_t radius_in_pixels, uint16_t thickness,
                gfx_color_t line_color, bool filled, gfx_color_t fill_color);

/**
 * @brief Push the whole framebuffer to the panel.
 *
 * Calls the display's ::gfx_flush_cb_t once covering the full canvas.
 *
 * @param gfx Canvas to present.
 * @return ::GFX_OK on success, ::GFX_ERR_INVALID_ARG if @p gfx or its flush
 *         callback is NULL.
 */
gfx_err_t gfx_flush(struct gfx *gfx);

/**
 * @brief Push a sub-rectangle of the framebuffer to the panel.
 *
 * The rectangle is clipped to the canvas. Because framebuffer rows are not
 * contiguous for a sub-region, the pixels are copied into a temporary packed
 * buffer before the single flush call.
 *
 * @param gfx Canvas to present from.
 * @param x Left coordinate of the region.
 * @param y Top coordinate of the region.
 * @param width Region width in pixels.
 * @param height Region height in pixels.
 * @return ::GFX_OK on success, ::GFX_ERR_INVALID_ARG for bad arguments,
 *         ::GFX_ERR_NO_MEM if the temporary copy could not be allocated.
 */
gfx_err_t gfx_flush_area(struct gfx *gfx, uint16_t x, uint16_t y, uint16_t width,
                         uint16_t height);

#ifdef __cplusplus
}
#endif
