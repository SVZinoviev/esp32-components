#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Packed pixel color used across the gfx library.
 *
 * For ::GFX_FMT_RGB565 canvases the value is a packed RGB565 word
 * (R[15:11], G[10:5], B[4:0]). For ::GFX_FMT_MONO8 canvases only the low
 * 8 bits are used and hold a brightness in the range 0..255.
 */
typedef uint16_t gfx_color_t;

/**
 * @brief Pack 24-bit RGB888 into a 16-bit RGB565 color.
 *
 * The low bits that do not fit (3 of red/blue, 2 of green) are discarded.
 *
 * @param r Red channel, 0..255.
 * @param g Green channel, 0..255.
 * @param b Blue channel, 0..255.
 * @return The equivalent RGB565 color.
 */
static inline gfx_color_t gfx_rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (gfx_color_t)(((uint16_t)(r & 0xF8) << 8) |
                         ((uint16_t)(g & 0xFC) << 3) |
                         ((uint16_t)(b) >> 3));
}

/**
 * @brief Expand an RGB565 color back to its 8-bit-per-channel components.
 *
 * Each channel is scaled to the full 0..255 range (not just left-shifted),
 * so white stays white.
 *
 * @param color RGB565 color to unpack.
 * @param[out] r Destination for the red channel (may be NULL).
 * @param[out] g Destination for the green channel (may be NULL).
 * @param[out] b Destination for the blue channel (may be NULL).
 */
static inline void gfx_rgb565_to_rgb888(gfx_color_t color, uint8_t *r,
                                        uint8_t *g, uint8_t *b)
{
    uint8_t r5 = (color >> 11) & 0x1F;
    uint8_t g6 = (color >> 5) & 0x3F;
    uint8_t b5 = color & 0x1F;
    if (r) *r = (uint8_t)((r5 * 255 + 15) / 31);
    if (g) *g = (uint8_t)((g6 * 255 + 31) / 63);
    if (b) *b = (uint8_t)((b5 * 255 + 15) / 31);
}

/**
 * @brief Convert an RGB565 color to an 8-bit perceptual brightness.
 *
 * Uses the Rec.601 luma weights (0.299 R, 0.587 G, 0.114 B). Useful when
 * driving a monochrome panel from RGB artwork.
 *
 * @param color RGB565 color.
 * @return Brightness in the range 0..255.
 */
static inline uint8_t gfx_rgb565_to_mono(gfx_color_t color)
{
    uint8_t r, g, b;
    gfx_rgb565_to_rgb888(color, &r, &g, &b);
    return (uint8_t)(((uint16_t)r * 77 + (uint16_t)g * 150 +
                      (uint16_t)b * 29) >> 8);
}

/**
 * @brief Build a monochrome color from a brightness value.
 *
 * @param brightness Brightness in the range 0..255.
 * @return A ::gfx_color_t carrying the brightness in its low 8 bits.
 */
static inline gfx_color_t gfx_mono(uint8_t brightness)
{
    return (gfx_color_t)brightness;
}

/**
 * @brief Swap the two bytes of an RGB565 word.
 *
 * Most SPI/i80 panels latch RGB565 big-endian (high byte first) while the
 * host stores it little-endian. A glue layer that talks to such a panel can
 * use this when copying the framebuffer out.
 *
 * @param color RGB565 color in host byte order.
 * @return The same color with its bytes swapped.
 */
static inline gfx_color_t gfx_rgb565_bswap(gfx_color_t color)
{
    return (gfx_color_t)((color >> 8) | (color << 8));
}

#ifdef __cplusplus
}
#endif
