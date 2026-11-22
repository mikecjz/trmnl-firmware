#pragma once

#include <Arduino.h>

// Decode a PNG straight into the panel driver's buffer/RAM; return the refresh mode to use, or an error
int png_to_epd(const uint8_t *pPNG, int iDataSize, bool bPrevious);

#ifdef BOARD_XTEINK_X4_CLASSIC
// Decode any PNG the panel can show into a caller-owned 1-bpp framebuffer
// (bit 1 = white, row-major, `stride` bytes per row). Rows past `height` stop
// the decode; pixels outside the source image stay white. Used to rebuild the
// frame that is already on the X4C glass so the UC8279 partial transport can
// diff against it.
bool png_decode_to_1bpp(const uint8_t *pPNG, int iDataSize, uint8_t *fb, int stride, int height);
#endif
