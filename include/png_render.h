#pragma once

#include <Arduino.h>

// Decode a PNG straight into the panel driver's buffer/RAM; return the refresh mode to use, or an error
int png_to_epd(const uint8_t *pPNG, int iDataSize, bool bPrevious);

#ifdef BOARD_XTEINK_X4_CLASSIC
// Rebuild the previously displayed frame over the new framebuffer.
struct X4cPrevFrame {
  bool decoded;   // the whole frame was reconstructed
  uint32_t hash;  // FNV-1a over the reconstructed 1-bpp frame
  bool any;       // false when the previous and new frames are identical
  uint16_t x;     // byte-aligned bounds of the pixels that differ
  uint16_t y;
  uint16_t w;
  uint16_t h;
};

// Decode a cached PNG of the frame on the glass into `fb` (1 bpp, bit 1 =
// white), diffing every row against the bytes already there before overwriting
// them. `fb` therefore holds the new frame on entry and the previous frame on
// return, so the UC8279 partial transport needs no second framebuffer.
bool png_decode_prev_frame(const uint8_t *pPNG, int iDataSize, uint8_t *fb, int stride, int height, X4cPrevFrame *out);
#endif
