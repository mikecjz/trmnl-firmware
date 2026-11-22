#pragma once

#include <stdint.h>

// X4C UC8279 full-refresh transport.
//
// bb_epaper's generic UC81xx path cannot drive the X4C glass: refresh() sends no
// plane data, and its EP75_800x480 profile streams 480 rows from gate 0 while
// the panel addresses 800x600 with the visible rows starting at gate 120.
//
// This transport implements the sequence validated on the unit (ported from
// CrossPoint's Uc8279X4Driver): 600-row plane uploads (120 white + visible
// rows), DTM1 seeded white, then CDI/CCSET/TSSET/PON/PSR/DRF/POF. It replaces
// both writePlane() and refresh() for this panel.
//
// Only the UC8279 profile is implemented. UC8179 units also need their own
// PLL/booster/VCOM bring-up and stay on the previous path until validated.

// True when the X4C UC8279 transport should handle display updates.
bool x4c_uc8279_active();

// Blocking full update of `fb` (1 bpp, MSB first, bit 1 = white, stride
// width/8). Returns false if the geometry is unsupported or a BUSY wait times
// out.
bool x4c_uc8279_full_update(const uint8_t *fb, uint16_t width, uint16_t height);
