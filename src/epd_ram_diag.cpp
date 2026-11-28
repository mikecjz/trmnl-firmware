#include <epd_ram_diag.h>

#ifdef EPD_RAM_DIAG

#include <SPI.h>
#include <board_config.h>
#include <epd_driver.h>
#include <globals.h>
#include <trmnl_log.h>

// Raw copy of these byte columns is kept so mismatching rows can be dumped.
// Byte column c covers panel pixels x = 8c .. 8c+7; 88..99 = x 704..799 (the strip is at x ~745-765).
#ifndef EPD_DIAG_WIN_FIRST
#define EPD_DIAG_WIN_FIRST 88
#endif
#ifndef EPD_DIAG_WIN_COLS
#define EPD_DIAG_WIN_COLS 12
#endif
#define EPD_DIAG_MAX_PITCH 128 // up to 1024 pixels wide
#define EPD_DIAG_DUMP_ROWS 12 // mismatching rows dumped per plane and stage

static int iDiagPitch = 0, iDiagHeight = 0;
static int iRowsSeen[2];
static uint32_t u32Sent[2][EPD_DIAG_MAX_PITCH]; // per byte column checksum of what was sent
static uint8_t *pWinSent = NULL; // [plane][row][EPD_DIAG_WIN_COLS]

static inline void ck_add(uint32_t *pCk, uint8_t u8) { *pCk = (*pCk * 31u) + u8 + 1u; }

void epd_diag_begin_image(int iPitch, int iHeight) {
  epd_diag_end();
  if (iPitch <= 0 || iPitch > EPD_DIAG_MAX_PITCH || iHeight <= 0) return;
  iDiagPitch = iPitch;
  iDiagHeight = iHeight;
  iRowsSeen[0] = iRowsSeen[1] = 0;
  memset(u32Sent, 0, sizeof(u32Sent));
  pWinSent = (uint8_t *)malloc(2 * iHeight * EPD_DIAG_WIN_COLS);
  if (pWinSent) memset(pWinSent, 0xa5, 2 * iHeight * EPD_DIAG_WIN_COLS);
}

void epd_diag_record_row(int iPlane, int y, const uint8_t *pRow, int iLen) {
  if (!iDiagPitch || iPlane < 0 || iPlane > 1 || y < 0 || y >= iDiagHeight) return;
  if (y != iRowsSeen[iPlane] && iRowsSeen[iPlane] >= 0) { // rows must arrive in order for the checksums to line up
    Log_error("EPDDIAG plane %d: row %d arrived, expected %d", iPlane, y, iRowsSeen[iPlane]);
    iRowsSeen[iPlane] = -1000000; // report once; the read-back for this image is skipped
    return;
  }
  if (iRowsSeen[iPlane] < 0) return;
  for (int c = 0; c < iDiagPitch; c++) {
    ck_add(&u32Sent[iPlane][c], (c < iLen) ? pRow[c] : 0);
  }
  if (pWinSent) {
    uint8_t *d = &pWinSent[(iPlane * iDiagHeight + y) * EPD_DIAG_WIN_COLS];
    for (int i = 0; i < EPD_DIAG_WIN_COLS; i++) {
      int c = EPD_DIAG_WIN_FIRST + i;
      d[i] = (c < iLen) ? pRow[c] : 0;
    }
  }
  iRowsSeen[iPlane] = y + 1;
}

void epd_diag_end(void) {
  free(pWinSent);
  pWinSent = NULL;
  iDiagPitch = iDiagHeight = 0;
}

// ---- bit-banged 3-wire access (SDA = the MOSI pin, used in both directions) ----

static void bb_write_byte(uint8_t u8) {
  for (int i = 0; i < 8; i++) {
    digitalWrite(pDevice->epd_mosi_pin, (u8 & 0x80) ? 1 : 0);
    digitalWrite(pDevice->epd_sck_pin, 1);
    digitalWrite(pDevice->epd_sck_pin, 0);
    u8 <<= 1;
  }
}

// Same timing as get_panel_rev(), which reads SSD16xx panels correctly
static uint8_t bb_read_byte(void) {
  uint8_t u8 = 0;
  for (int i = 0; i < 8; i++) {
    digitalWrite(pDevice->epd_sck_pin, 1);
    digitalWrite(pDevice->epd_sck_pin, 0);
    u8 = (u8 << 1) | (digitalRead(pDevice->epd_mosi_pin) ? 1 : 0);
  }
  return u8;
}

static void bb_cmd(uint8_t u8Cmd, const uint8_t *pData, int iLen) {
  pinMode(pDevice->epd_mosi_pin, OUTPUT);
  digitalWrite(pDevice->epd_cs_pin, 0);
  digitalWrite(pDevice->epd_dc_pin, 0);
  bb_write_byte(u8Cmd);
  digitalWrite(pDevice->epd_dc_pin, 1);
  for (int i = 0; i < iLen; i++)
    bb_write_byte(pData[i]);
  digitalWrite(pDevice->epd_cs_pin, 1);
}

static void wait_not_busy(void) {
  uint32_t t0 = millis();
  while (digitalRead(pDevice->epd_busy_pin) && millis() - t0 < 5000)
    delay(1); // SSD16xx: busy = high
}

// Restores the same RAM window/counters bb_epaper uses for EP426 (orientation 0, data entry mode 0x02)
static void set_ram_window(int iWidth, int iHeight) {
  uint8_t u8[4];
  u8[0] = 0x02;
  bb_cmd(0x11, u8, 1); // data entry: X decrement, Y increment
  u8[0] = (uint8_t)(iWidth - 1);
  u8[1] = (uint8_t)((iWidth - 1) >> 8);
  u8[2] = u8[3] = 0;
  bb_cmd(0x44, u8, 4); // X start = width-1, end = 0
  u8[0] = u8[1] = 0;
  u8[2] = (uint8_t)(iHeight - 1);
  u8[3] = (uint8_t)((iHeight - 1) >> 8);
  bb_cmd(0x45, u8, 4); // Y start = 0, end = height-1
  u8[0] = (uint8_t)(iWidth - 1);
  u8[1] = (uint8_t)((iWidth - 1) >> 8);
  bb_cmd(0x4e, u8, 2); // X counter
  u8[0] = u8[1] = 0;
  bb_cmd(0x4f, u8, 2); // Y counter
}

static void append_col(char *sz, size_t iSize, int c, int iCount) {
  size_t len = strlen(sz);
  if (len + 16 < iSize) snprintf(&sz[len], iSize - len, iCount >= 0 ? " %d(x%d):%d" : " %d(x%d)", c, c * 8, iCount);
}

void epd_diag_readback(const char *szStage) {
  if (!iDiagPitch) return;
  if (iRowsSeen[0] != iDiagHeight || iRowsSeen[1] != iDiagHeight) {
    Log_error("EPDDIAG %s: skipped, rows sent p0=%d p1=%d of %d", szStage, iRowsSeen[0], iRowsSeen[1], iDiagHeight);
    return;
  }
  if (bbep.getPanelType() != EP426_800x480 || pDevice->epd_mosi_pin == 0) {
    Log_error("EPDDIAG %s: only implemented for the EP426 (Xteink X4) panel", szStage);
    return;
  }
  const int iWidth = iDiagPitch * 8;
  const bool bSerial = (bool)Serial;
  uint32_t u32Read[EPD_DIAG_MAX_PITCH];
  int iWinBad[2][EPD_DIAG_WIN_COLS];
  uint16_t u16Same[EPD_DIAG_MAX_PITCH]; // rows where plane0 byte has a bit equal to plane1 (no drive)
  uint8_t *pP0 = (uint8_t *)malloc(iDiagPitch * iDiagHeight); // whole plane 0, for the inversion check
  uint8_t u8Row[EPD_DIAG_MAX_PITCH];
  uint8_t u8Opt;
  char szBad[2][384];
  int iDumped;
  uint32_t t0 = millis();

  memset(u16Same, 0, sizeof(u16Same));
  memset(iWinBad, 0, sizeof(iWinBad));

  wait_not_busy();
  SPI.end(); // hand SCK/SDA to us; bb_epaper's hardware SPI is re-attached at the end
  pinMode(pDevice->epd_sck_pin, OUTPUT);
  digitalWrite(pDevice->epd_sck_pin, 0);
  pinMode(pDevice->epd_cs_pin, OUTPUT);
  pinMode(pDevice->epd_dc_pin, OUTPUT);
  digitalWrite(pDevice->epd_cs_pin, 1);

  for (int iPlane = 0; iPlane < 2; iPlane++) {
    for (int c = 0; c < iDiagPitch; c++)
      u32Read[c] = 0;
    iDumped = 0;
    set_ram_window(iWidth, iDiagHeight);
    u8Opt = (uint8_t)iPlane; // 0x41: 0 = read 0x24 (BW RAM), 1 = read 0x26 (RED/"old" RAM)
    bb_cmd(0x41, &u8Opt, 1);
    pinMode(pDevice->epd_mosi_pin, OUTPUT);
    digitalWrite(pDevice->epd_cs_pin, 0);
    digitalWrite(pDevice->epd_dc_pin, 0);
    bb_write_byte(0x27); // read RAM
    digitalWrite(pDevice->epd_dc_pin, 1);
    pinMode(pDevice->epd_mosi_pin, INPUT);
    (void)bb_read_byte(); // dummy byte
    for (int y = 0; y < iDiagHeight; y++) {
      for (int c = 0; c < iDiagPitch; c++)
        u8Row[c] = bb_read_byte();
      for (int c = 0; c < iDiagPitch; c++)
        ck_add(&u32Read[c], u8Row[c]);
      if (iPlane == 0 && pP0) memcpy(&pP0[y * iDiagPitch], u8Row, iDiagPitch);
      if (iPlane == 1 && pP0) {
        const uint8_t *p0 = &pP0[y * iDiagPitch];
        for (int c = 0; c < iDiagPitch; c++) {
          if ((uint8_t)(p0[c] ^ u8Row[c]) != 0xff) u16Same[c]++;
        }
      }
      if (pWinSent) {
        const uint8_t *pExp = &pWinSent[(iPlane * iDiagHeight + y) * EPD_DIAG_WIN_COLS];
        bool bRowBad = false;
        for (int i = 0; i < EPD_DIAG_WIN_COLS; i++) {
          if (pExp[i] != u8Row[EPD_DIAG_WIN_FIRST + i]) {
            iWinBad[iPlane][i]++;
            bRowBad = true;
          }
        }
        if (bRowBad && bSerial && iDumped < EPD_DIAG_DUMP_ROWS) {
          iDumped++;
          Serial.printf("EPDDIAG %s p%d y=%3d sent:", szStage, iPlane, y);
          for (int i = 0; i < EPD_DIAG_WIN_COLS; i++)
            Serial.printf(" %02x", pExp[i]);
          Serial.printf("  read:");
          for (int i = 0; i < EPD_DIAG_WIN_COLS; i++)
            Serial.printf(" %02x", u8Row[EPD_DIAG_WIN_FIRST + i]);
          Serial.printf("\n");
        }
      }
    }
    digitalWrite(pDevice->epd_cs_pin, 1);
    pinMode(pDevice->epd_mosi_pin, OUTPUT);

    szBad[iPlane][0] = 0;
    for (int c = 0; c < iDiagPitch; c++) {
      if (u32Read[c] != u32Sent[iPlane][c]) {
        int i = c - EPD_DIAG_WIN_FIRST;
        append_col(szBad[iPlane], sizeof(szBad[iPlane]), c,
                   (i >= 0 && i < EPD_DIAG_WIN_COLS && pWinSent) ? iWinBad[iPlane][i] : -1);
      }
    }
  }

  // Give the pins back to bb_epaper's hardware SPI (same settings as bbepInitIO)
  SPI.begin(pDevice->epd_sck_pin, -1, pDevice->epd_mosi_pin, -1);
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  SPI.endTransaction();

  char szSame[384];
  szSame[0] = 0;
  if (pP0) {
    for (int c = 0; c < iDiagPitch; c++) {
      if (u16Same[c]) append_col(szSame, sizeof(szSame), c, u16Same[c]);
    }
  } else {
    strcpy(szSame, " (no memory for the check)");
  }
  free(pP0);

  // One line per stage: stored and sent to the server's /api/log with the next log upload
  Log_error("EPDDIAG %s %lums: 0x24 bad:%s | 0x26 bad:%s | undriven:%s", szStage, (unsigned long)(millis() - t0),
            szBad[0][0] ? szBad[0] : " none", szBad[1][0] ? szBad[1] : " none", szSame[0] ? szSame : " none");
  if (bSerial) {
    Serial.printf("EPDDIAG %s window cols %d..%d (x %d..%d) bad rows p0/p1:", szStage, EPD_DIAG_WIN_FIRST,
                  EPD_DIAG_WIN_FIRST + EPD_DIAG_WIN_COLS - 1, EPD_DIAG_WIN_FIRST * 8,
                  (EPD_DIAG_WIN_FIRST + EPD_DIAG_WIN_COLS) * 8 - 1);
    for (int i = 0; i < EPD_DIAG_WIN_COLS; i++)
      Serial.printf(" %d/%d", iWinBad[0][i], iWinBad[1][i]);
    Serial.printf("\n");
  }
}

#endif // EPD_RAM_DIAG
