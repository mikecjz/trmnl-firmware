#pragma once

#include <stdint.h>

// X4C UC8279 transport: full OTP flash plus the differential (DU) partial.
//
// bb_epaper's generic UC81xx path cannot drive the X4C glass: refresh() sends no
// plane data, and its EP75_800x480 profile streams 480 rows from gate 0 while
// the panel addresses 800x600 with the visible rows starting at gate 120.
//
// These transports implement the sequences validated on the unit (ported from
// CrossPoint's Uc8279X4Driver): 600-row plane uploads (120 white + visible
// rows), then either the GC/OTP full-refresh trigger or the PTIN/PTL windowed
// DU partial. They replace both writePlane() and refresh() for this panel.
//
// Only the UC8279 profile is implemented. UC8179 units also need their own
// PLL/booster/VCOM bring-up and stay on the previous path until validated.

// True when the X4C UC8279 transport should handle display updates.
bool x4c_uc8279_active();

// Blocking full update of `fb` (1 bpp, MSB first, bit 1 = white, stride
// width/8) with the OTP GC waveform. Returns false if the geometry is
// unsupported or a BUSY wait times out.
bool x4c_uc8279_full_update(const uint8_t *fb, uint16_t width, uint16_t height);

// A rectangle of the visible 480-row image. x and w must be byte aligned
// (multiples of 8); the transport adds the panel's 120-gate visible offset.
struct X4cPartialWindow {
  uint16_t x;
  uint16_t y;
  uint16_t w;
  uint16_t h;
};

// Blocking differential update: `prev` is the frame currently on the glass and
// `fb` the new frame. Both go through the normal 600-row upload (DTM1/DTM2);
// the refresh itself is restricted to `win` with the DU waveform, so unchanged
// pixels do not move and there is no black/white flash. The caller must have a
// frame that matches the glass; pass the full panel as the window when the
// dirty rectangle is unknown.
bool x4c_uc8279_partial_update(const uint8_t *fb, const uint8_t *prev, uint16_t width, uint16_t height,
                               const X4cPartialWindow &win);

// True when the last update through this transport succeeded, so the frame
// identified by last_frame_hash() is what the glass shows.
bool x4c_uc8279_baseline_valid();

// FNV-1a of a 1-bpp framebuffer, in this transport's geometry.
uint32_t x4c_uc8279_frame_hash(const uint8_t *fb, uint16_t width, uint16_t height);

// Hash of the last frame successfully pushed through the transport.
uint32_t x4c_uc8279_last_frame_hash();

// Copy the last frame pushed through the transport into `dst`, when it is still
// in RAM (same boot). Returns false after a deep-sleep cycle, where the caller
// has to rebuild the frame from the cached image instead.
bool x4c_uc8279_last_frame(uint8_t *dst, uint16_t width, uint16_t height);

// Forget the baseline: the next update must be a full OTP flash.
void x4c_uc8279_invalidate_baseline();
