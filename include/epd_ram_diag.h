#pragma once

//
// SSD16xx RAM read-back diagnostics (built only with -D EPD_RAM_DIAG, see [env:xteink_x4_diag]).
//
// While a 1-bit image is streamed into the controller, a running checksum is kept for every
// byte column of each RAM plane (0x24 = new image, 0x26 = inverted copy used as the "old" image
// for the partial refresh), plus a raw copy of a narrow column window. Before and after the
// refresh both planes are read back over the panel's bidirectional SDA line and compared, so a
// stuck column can be pinned on the RAM contents (firmware/SPI/addressing) or on the panel
// drive (RAM correct, pixels not driven).
//

#include <Arduino.h>

#ifdef EPD_RAM_DIAG

// Starts recording a new 1-bit image of iPitch bytes x iHeight rows
void epd_diag_begin_image(int iPitch, int iHeight);
// Records one row as it is sent to RAM plane 0 (0x24) or 1 (0x26)
void epd_diag_record_row(int iPlane, int y, const uint8_t *pRow, int iLen);
// Reads both planes back and reports any byte column that differs from what was sent
void epd_diag_readback(const char *szStage);
// Frees the recording
void epd_diag_end(void);

#else

static inline void epd_diag_begin_image(int, int) {}
static inline void epd_diag_record_row(int, int, const uint8_t *, int) {}
static inline void epd_diag_readback(const char *) {}
static inline void epd_diag_end(void) {}

#endif // EPD_RAM_DIAG
