/*
 * tft_ili9488.cpp — ILI9488 SPI driver with RP2040 DMA bulk transfers
 *
 * Hardware (from TFT_setup.h):
 *   SPI1, MOSI=11, MISO=12, SCLK=10
 *   TFT CS=13, DC=4, RST=5
 *   Touch CS=9
 *   Display SPI: 66 MHz  /  Touch SPI: 2.5 MHz
 *
 * ILI9488 uses 18-bit colour (3 bytes per pixel) over SPI.
 *
 * Optimisations over the original blocking implementation:
 *   • fillRect / fillScreen pre-fill a 1440-byte line buffer with the target
 *     colour once, then DMA each row rather than transferring 3 bytes per pixel.
 *   • pushRect / pushImage convert each row of RGB565 → RGB888 into the line
 *     buffer and DMA-send the row in one shot.
 *   • _drawChar keeps the SPI transaction open across the whole glyph,
 *     detects horizontal runs of foreground pixels, and sends each run as a
 *     single DMA burst — eliminating per-pixel begin/end transaction overhead.
 *   • _setWindow sends the 4 address bytes after each command as a block via
 *     spi_write_blocking(), halving the number of individual SPI calls.
 *   • gpio_put() replaces digitalWrite() for DC toggling (faster register write).
 *
 * DMA setup (RP2040):
 *   _dma_tx_ch — reads from _txbuf (incrementing), writes to SPI1 DR (fixed).
 *   _dma_rx_ch — reads from SPI1 DR (fixed), writes to _rx_dummy (fixed).
 *   Both channels are started together so the RX FIFO never overflows.
 *   Transfers < DMA_THRESHOLD bytes fall back to spi_write_blocking().
 */

#include "tft_ili9488.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"

// ─── Pin assignments ──────────────────────────────────────────────────────────
#define PIN_TFT_CS   13
#define PIN_TFT_DC    4
#define PIN_TFT_RST   5
#define PIN_TOUCH_CS  9
#define SPI_FREQ_DISPLAY  66000000UL
#define SPI_FREQ_TOUCH     2500000UL

// ─── ILI9488 commands ─────────────────────────────────────────────────────────
#define ILI_SWRST   0x01
#define ILI_SLPOUT  0x11
#define ILI_DISPON  0x29
#define ILI_CASET   0x2A
#define ILI_PASET   0x2B
#define ILI_RAMWR   0x2C
#define ILI_MADCTL  0x36
#define ILI_COLMOD  0x3A

// MADCTL bits
#define MADCTL_MY   0x80
#define MADCTL_MX   0x40
#define MADCTL_MV   0x20
#define MADCTL_BGR  0x08

// Rotation 3 (inverted landscape, 480×320)
#define MADCTL_ROT3 (MADCTL_MX | MADCTL_MY | MADCTL_MV | MADCTL_BGR)

// ─── DMA / bulk-transfer state ────────────────────────────────────────────────
// One full row of 18-bit pixels (480 × 3 bytes = 1440 bytes).
// fillRect pre-fills this once per colour; pushRect converts one row at a time.
#define _TXBUF_PIX  480
static uint8_t _txbuf[_TXBUF_PIX * 3];

// Destination for the RX-drain DMA channel (discarded).
static uint8_t _rx_dummy;

// DMA channel handles (–1 until _dmaInit() succeeds).
static int _dma_tx_ch = -1;
static int _dma_rx_ch = -1;

// Transfers smaller than this use spi_write_blocking() to avoid DMA overhead.
#define DMA_THRESHOLD 32

// ─── Font data ────────────────────────────────────────────────────────────────
#include "fonts/glcdfont.c"
#include "fonts/Font16.c"
#include "fonts/Font32rle.c"

#define chr_hgt_f16   16
#define baseline_f16  13
#define chr_hgt_f32   26
#define baseline_f32  19

static const uint8_t _stub_width[1] = {0};
static const uint8_t _stub_chr[1]   = {0};
static const uint8_t * const _stub_tbl[1] = {_stub_chr};

const fontinfo fontdata[] = {
    { (const uint8_t *)_stub_tbl,   _stub_width,  0, 0 }, // 0
    { (const uint8_t *)_stub_tbl,   _stub_width,  8, 7 }, // 1 GLCD
    { (const uint8_t *)chrtbl_f16, widtbl_f16,   chr_hgt_f16, baseline_f16 }, // 2
    { (const uint8_t *)_stub_tbl,   _stub_width,  0, 0 }, // 3
    { (const uint8_t *)chrtbl_f32, widtbl_f32,   chr_hgt_f32, baseline_f32 }, // 4
};

// ─── Constructor ──────────────────────────────────────────────────────────────
TFT_ILI9488::TFT_ILI9488()
    : _textFont(2), _textColor(TFT_WHITE), _swapBytes(false),
      _width(480), _height(320),
      _dma_tx_ch(-1), _dma_rx_ch(-1),
      _tcal_x0(1), _tcal_x1(1), _tcal_y0(1), _tcal_y1(1),
      _tcal_rotate(false), _tcal_invert_x(false), _tcal_invert_y(false)
{}

// ─── DMA initialisation ───────────────────────────────────────────────────────

void TFT_ILI9488::_dmaInit() {
    _dma_tx_ch = dma_claim_unused_channel(false);
    _dma_rx_ch = dma_claim_unused_channel(false);
    if (_dma_tx_ch < 0 || _dma_rx_ch < 0) return; // fall back to blocking SPI

    // TX channel: read from _txbuf (incrementing), write to SPI1 DR (fixed).
    dma_channel_config c = dma_channel_get_default_config(_dma_tx_ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_dreq(&c, spi_get_dreq(spi1, true /*tx*/));
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    dma_channel_configure(_dma_tx_ch, &c,
        &spi_get_hw(spi1)->dr,  // dst: SPI data register
        NULL,                    // src: set per-transfer
        0,                       // count: set per-transfer
        false);

    // RX drain channel: read from SPI1 DR (fixed), write to _rx_dummy (fixed).
    c = dma_channel_get_default_config(_dma_rx_ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_dreq(&c, spi_get_dreq(spi1, false /*rx*/));
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, false);
    dma_channel_configure(_dma_rx_ch, &c,
        &_rx_dummy,              // dst: dummy (discard RX)
        &spi_get_hw(spi1)->dr,   // src: SPI data register
        0,                       // count: set per-transfer
        false);
}

// ─── Core DMA write ───────────────────────────────────────────────────────────
// Must be called inside an active SPI transaction (CS asserted, DC high).
// Sends exactly `len` bytes from `buf` to the display with no byte skipped.

void TFT_ILI9488::_dmaWrite(const uint8_t *buf, uint32_t len) {
    if (!len) return;

    if (len < DMA_THRESHOLD || _dma_tx_ch < 0) {
        // Small transfer — spi_write_blocking handles FIFO and RX drain.
        spi_write_blocking(spi1, buf, len);
        return;
    }

    // Configure and start TX channel.
    dma_channel_set_read_addr(_dma_tx_ch, buf, false);
    dma_channel_set_trans_count(_dma_tx_ch, len, false);

    // Configure and start RX drain (same count so FIFO never overflows).
    dma_channel_set_trans_count(_dma_rx_ch, len, false);

    // Trigger both simultaneously.
    dma_start_channel_mask((1u << _dma_tx_ch) | (1u << _dma_rx_ch));

    // Wait for TX DMA to complete, then for SPI to finish clocking out.
    dma_channel_wait_for_finish_blocking(_dma_tx_ch);
    while (spi_is_busy(spi1)) tight_loop_contents();

    // Drain any residual RX FIFO bytes.
    while (spi_is_readable(spi1)) (void)spi_get_hw(spi1)->dr;
}

// ─── Solid-pixel fill (in-transaction) ───────────────────────────────────────
// Sends `count` pixels of the given RGB888 colour.
// _txbuf must already be pre-filled for `count` pixels (or a divisor of count
// if called from fillRect, which pre-fills one full row).

void TFT_ILI9488::_fillPixels(uint32_t count, uint8_t r, uint8_t g, uint8_t b) {
    // Pre-fill TX buffer with one "stride" worth of solid colour.
    uint32_t stride = (count < (uint32_t)_TXBUF_PIX) ? count : (uint32_t)_TXBUF_PIX;
    uint8_t *p = _txbuf;
    for (uint32_t i = 0; i < stride; i++) { *p++ = r; *p++ = g; *p++ = b; }

    // Send in full-stride chunks, then remainder.
    uint32_t rem = count;
    while (rem >= stride) {
        _dmaWrite(_txbuf, stride * 3);
        rem -= stride;
    }
    if (rem) _dmaWrite(_txbuf, rem * 3);
}

// ─── Low-level SPI helpers ────────────────────────────────────────────────────

void TFT_ILI9488::_beginTransaction() {
    SPI1.beginTransaction(SPISettings(SPI_FREQ_DISPLAY, MSBFIRST, SPI_MODE0));
    gpio_put(PIN_TFT_CS, 0);
}

void TFT_ILI9488::_endTransaction() {
    gpio_put(PIN_TFT_CS, 1);
    SPI1.endTransaction();
}

void TFT_ILI9488::_writeCmd(uint8_t cmd) {
    gpio_put(PIN_TFT_DC, 0);
    SPI1.transfer(cmd);
    gpio_put(PIN_TFT_DC, 1);
}

void TFT_ILI9488::_writeData(uint8_t data) {
    SPI1.transfer(data);
}

// Optimised _setWindow: sends the 4 address bytes after each command as a
// block via spi_write_blocking(), cutting per-command overhead in half.
void TFT_ILI9488::_setWindow(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    uint8_t buf[4];

    gpio_put(PIN_TFT_DC, 0);
    SPI1.transfer(ILI_CASET);
    gpio_put(PIN_TFT_DC, 1);
    buf[0] = x0 >> 8; buf[1] = x0 & 0xFF;
    buf[2] = x1 >> 8; buf[3] = x1 & 0xFF;
    spi_write_blocking(spi1, buf, 4);

    gpio_put(PIN_TFT_DC, 0);
    SPI1.transfer(ILI_PASET);
    gpio_put(PIN_TFT_DC, 1);
    buf[0] = y0 >> 8; buf[1] = y0 & 0xFF;
    buf[2] = y1 >> 8; buf[3] = y1 & 0xFF;
    spi_write_blocking(spi1, buf, 4);

    gpio_put(PIN_TFT_DC, 0);
    SPI1.transfer(ILI_RAMWR);
    gpio_put(PIN_TFT_DC, 1);
}

// ─── Initialisation ───────────────────────────────────────────────────────────

void TFT_ILI9488::init() {
    pinMode(PIN_TFT_CS,   OUTPUT); gpio_put(PIN_TFT_CS,   1);
    pinMode(PIN_TFT_DC,   OUTPUT); gpio_put(PIN_TFT_DC,   1);
    pinMode(PIN_TFT_RST,  OUTPUT); gpio_put(PIN_TFT_RST,  1);
    pinMode(PIN_TOUCH_CS, OUTPUT); gpio_put(PIN_TOUCH_CS, 1);

    SPI1.setMOSI(11);
    SPI1.setMISO(12);
    SPI1.setSCK(10);
    SPI1.begin();

    gpio_put(PIN_TFT_RST, 0);
    delay(10);
    gpio_put(PIN_TFT_RST, 1);
    delay(120);

    _beginTransaction();

    _writeCmd(0xE0);
    const uint8_t pgamma[] = {0x00,0x03,0x09,0x08,0x16,0x0A,0x3F,0x78,
                               0x4C,0x09,0x0A,0x08,0x16,0x1A,0x0F};
    for (uint8_t i = 0; i < sizeof(pgamma); i++) _writeData(pgamma[i]);

    _writeCmd(0xE1);
    const uint8_t ngamma[] = {0x00,0x16,0x19,0x03,0x0F,0x05,0x32,0x45,
                               0x46,0x04,0x0E,0x0D,0x35,0x37,0x0F};
    for (uint8_t i = 0; i < sizeof(ngamma); i++) _writeData(ngamma[i]);

    _writeCmd(0xC0); _writeData(0x17); _writeData(0x15);
    _writeCmd(0xC1); _writeData(0x41);
    _writeCmd(0xC5); _writeData(0x00); _writeData(0x12); _writeData(0x80);

    _writeCmd(ILI_MADCTL); _writeData(MADCTL_ROT3);
    _writeCmd(ILI_COLMOD); _writeData(0x66); // 18-bit colour

    _writeCmd(0xB0); _writeData(0x00);
    _writeCmd(0xB1); _writeData(0xA0);
    _writeCmd(0xB4); _writeData(0x02);
    _writeCmd(0xB6); _writeData(0x02); _writeData(0x02); _writeData(0x3B);
    _writeCmd(0xB7); _writeData(0xC6);
    _writeCmd(0xF7); _writeData(0xA9); _writeData(0x51);
                     _writeData(0x2C); _writeData(0x82);

    _writeCmd(ILI_SLPOUT);
    _endTransaction();
    delay(120);

    _beginTransaction();
    _writeCmd(ILI_DISPON);
    _endTransaction();
    delay(25);

    // Claim DMA channels for bulk pixel output.
    _dmaInit();
}

void TFT_ILI9488::setRotation(uint8_t r) {
    uint8_t madval;
    switch (r % 4) {
        case 0: madval = MADCTL_MX | MADCTL_BGR; _width=320; _height=480; break;
        case 1: madval = MADCTL_MV | MADCTL_BGR; _width=480; _height=320; break;
        case 2: madval = MADCTL_MY | MADCTL_BGR; _width=320; _height=480; break;
        default:
            madval = MADCTL_MX | MADCTL_MY | MADCTL_MV | MADCTL_BGR;
            _width=480; _height=320; break;
    }
    _beginTransaction();
    _writeCmd(ILI_MADCTL);
    _writeData(madval);
    _endTransaction();
}

void TFT_ILI9488::setSwapBytes(bool swap) { _swapBytes = swap; }

// ─── fillScreen ──────────────────────────────────────────────────────────────

void TFT_ILI9488::fillScreen(uint16_t color) {
    fillRect(0, 0, _width, _height, color);
}

// ─── fillRect — DMA optimised ─────────────────────────────────────────────────
// Pre-fills one row of _txbuf with the RGB888 colour, then DMA-sends it once
// per row.  A full 480×320 fill goes from 1.4 M individual SPI calls to 320
// DMA bursts of 1440 bytes each.

void TFT_ILI9488::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= _width || y >= _height || w <= 0 || h <= 0) return;
    if (x + w > _width)  w = _width  - x;
    if (y + h > _height) h = _height - y;

    uint8_t r = (color >> 11) << 3;
    uint8_t g = ((color >> 5) & 0x3F) << 2;
    uint8_t b = (color & 0x1F) << 3;

    // Pre-fill exactly one row of the TX buffer.
    uint8_t *p = _txbuf;
    for (int32_t i = 0; i < w; i++) { *p++ = r; *p++ = g; *p++ = b; }
    const uint32_t row_bytes = (uint32_t)w * 3;

    _beginTransaction();
    _setWindow(x, y, x + w - 1, y + h - 1);
    for (int32_t row = 0; row < h; row++) {
        _dmaWrite(_txbuf, row_bytes);
    }
    _endTransaction();
}

// ─── drawRect ────────────────────────────────────────────────────────────────

void TFT_ILI9488::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
    drawLine(x,     y,     x+w-1, y,     color);
    drawLine(x,     y+h-1, x+w-1, y+h-1, color);
    drawLine(x,     y,     x,     y+h-1, color);
    drawLine(x+w-1, y,     x+w-1, y+h-1, color);
}

// ─── drawLine (Bresenham) ─────────────────────────────────────────────────────

void TFT_ILI9488::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color) {
    // Fast paths for axis-aligned lines.
    if (y0 == y1) {
        if (x0 > x1) { int32_t t=x0; x0=x1; x1=t; }
        fillRect(x0, y0, x1-x0+1, 1, color);
        return;
    }
    if (x0 == x1) {
        if (y0 > y1) { int32_t t=y0; y0=y1; y1=t; }
        fillRect(x0, y0, 1, y1-y0+1, color);
        return;
    }

    int32_t dx = abs(x1-x0), sx = x0<x1 ? 1 : -1;
    int32_t dy = abs(y1-y0), sy = y0<y1 ? 1 : -1;
    int32_t err = (dx>dy ? dx : -dy)/2, e2;

    for (;;) {
        drawPixel(x0, y0, color);
        if (x0==x1 && y0==y1) break;
        e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 <  dy) { err += dx; y0 += sy; }
    }
}

// ─── drawPixel ───────────────────────────────────────────────────────────────

void TFT_ILI9488::drawPixel(int32_t x, int32_t y, uint16_t color) {
    if (x < 0 || x >= _width || y < 0 || y >= _height) return;
    _beginTransaction();
    _setWindow(x, y, x, y);
    _writePixel(color);
    _endTransaction();
}

// ─── fillRoundRect / drawRoundRect ───────────────────────────────────────────

void TFT_ILI9488::fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h,
                                  int32_t r, uint16_t color) {
    if (r <= 0 || r > w/2 || r > h/2) { fillRect(x, y, w, h, color); return; }
    fillRect(x + r, y, w - 2*r, h, color);

    int32_t f = 1-r, ddF_x = 1, ddF_y = -2*r, px = 0, py = r;
    int32_t delta = h - 2*r - 1;

    while (px < py) {
        if (f >= 0) { py--; ddF_y += 2; f += ddF_y; }
        px++; ddF_x += 2; f += ddF_x;
        fillRect(x + w - r - 1 + px, y + r - py, 1, 2*py + 1 + delta, color);
        fillRect(x + w - r - 1 + py, y + r - px, 1, 2*px + 1 + delta, color);
        fillRect(x + r - px,         y + r - py, 1, 2*py + 1 + delta, color);
        fillRect(x + r - py,         y + r - px, 1, 2*px + 1 + delta, color);
    }
}

void TFT_ILI9488::drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h,
                                  int32_t r, uint16_t color) {
    if (r <= 0) { drawRect(x, y, w, h, color); return; }
    drawLine(x+r,     y,         x+w-r-1, y,         color);
    drawLine(x+r,     y+h-1,     x+w-r-1, y+h-1,     color);
    drawLine(x,       y+r,       x,       y+h-r-1,   color);
    drawLine(x+w-1,   y+r,       x+w-1,   y+h-r-1,   color);

    int32_t f = 1-r, ddF_x = 1, ddF_y = -2*r, px = 0, py = r;
    while (px <= py) {
        drawPixel(x+w-r+px-1, y+r-py-1,   color);
        drawPixel(x+r-px,     y+r-py-1,   color);
        drawPixel(x+w-r+px-1, y+h-r+py,   color);
        drawPixel(x+r-px,     y+h-r+py,   color);
        drawPixel(x+w-r+py-1, y+r-px-1,   color);
        drawPixel(x+r-py,     y+r-px-1,   color);
        drawPixel(x+w-r+py-1, y+h-r+px,   color);
        drawPixel(x+r-py,     y+h-r+px,   color);
        if (f >= 0) { py--; ddF_y += 2; f += ddF_y; }
        px++; ddF_x += 2; f += ddF_x;
    }
}

// ─── pushRect — DMA optimised ─────────────────────────────────────────────────
// Converts each row of RGB565 → RGB888 into _txbuf, then DMA-sends it.

void TFT_ILI9488::pushRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data) {
    if (w <= 0 || h <= 0) return;
    _beginTransaction();
    _setWindow(x, y, x + w - 1, y + h - 1);
    for (int32_t row = 0; row < h; row++) {
        uint16_t *src = data + row * w;
        uint8_t  *dst = _txbuf;
        for (int32_t i = 0; i < w; i++) {
            uint16_t px = *src++;
            *dst++ = (px >> 11) << 3;
            *dst++ = ((px >> 5) & 0x3F) << 2;
            *dst++ = (px & 0x1F) << 3;
        }
        _dmaWrite(_txbuf, (uint32_t)w * 3);
    }
    _endTransaction();
}

// ─── pushImage — DMA optimised ────────────────────────────────────────────────

void TFT_ILI9488::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data) {
    // Clip to screen bounds.
    int32_t dx = (x < 0) ? -x : 0;
    int32_t dy = (y < 0) ? -y : 0;
    int32_t dw = w - dx;  if (x + dw > _width)  dw = _width  - (x + dx);
    int32_t dh = h - dy;  if (y + dh > _height) dh = _height - (y + dy);
    if (dw <= 0 || dh <= 0) return;

    _beginTransaction();
    _setWindow(x + dx, y + dy, x + dx + dw - 1, y + dy + dh - 1);
    for (int32_t row = dy; row < dy + dh; row++) {
        uint16_t *src = data + row * w + dx;
        uint8_t  *dst = _txbuf;
        for (int32_t i = 0; i < dw; i++) {
            uint16_t px = *src++;
            *dst++ = (px >> 11) << 3;
            *dst++ = ((px >> 5) & 0x3F) << 2;
            *dst++ = (px & 0x1F) << 3;
        }
        _dmaWrite(_txbuf, (uint32_t)dw * 3);
    }
    _endTransaction();
}

// ─── Text rendering ──────────────────────────────────────────────────────────

void TFT_ILI9488::setTextFont(uint8_t f)   { _textFont  = f; }
void TFT_ILI9488::setTextColor(uint16_t c) { _textColor = c; }

int16_t TFT_ILI9488::fontHeight(uint8_t f) {
    if (f == 1) return 8;
    if (f >= 2 && f <= 4) return pgm_read_byte(&fontdata[f].height);
    return 8;
}

int16_t TFT_ILI9488::_charWidth(uint8_t c, uint8_t f) {
    if (c < 32 || c > 127) return 0;
    if (f == 1) return 6;
    if (f == 2 || f == 4) {
        const uint8_t *wtbl = (const uint8_t *)pgm_read_dword(&fontdata[f].widthtbl);
        return (int16_t)pgm_read_byte(wtbl + c - 32);
    }
    return 0;
}

int16_t TFT_ILI9488::textWidth(const char *str, uint8_t f) {
    int16_t total = 0;
    while (*str) { total += _charWidth((uint8_t)*str++, f); }
    return total;
}

// Draw one character with the SPI transaction kept open across the whole glyph.
// Foreground pixels are sent as horizontal DMA bursts; background is skipped
// (transparent rendering).  Reduces transactions from one-per-pixel to
// one-per-horizontal-run.

int16_t TFT_ILI9488::_drawChar(uint8_t c, int32_t x, int32_t y) {
    if (c < 32 || c > 127) return 0;
    uint8_t uc = c - 32;

    // Decompose fg colour to RGB888 once.
    uint8_t fr = (_textColor >> 11) << 3;
    uint8_t fg = ((_textColor >> 5) & 0x3F) << 2;
    uint8_t fb = (_textColor & 0x1F) << 3;

    if (_textFont == 1) {
        // GLCD 5×7 bitmap: 5 bytes per char, each byte is a column
        // (bit 0 = top pixel).  Rendered as 6 px wide (1 px right-side spacing)
        // and 8 px tall (row 7 is inter-line spacing, always blank).
        const uint8_t *glyph = font + (uint16_t)c * 5;
        const uint8_t  gw = 5;

        _beginTransaction();
        for (uint8_t row = 0; row < 7; row++) {
            uint8_t run_start = 0;
            bool    in_run    = false;

            for (uint8_t col = 0; col < gw; col++) {
                bool set = (pgm_read_byte(glyph + col) >> row) & 1;
                if (set && !in_run) {
                    run_start = col;
                    in_run    = true;
                } else if (!set && in_run) {
                    uint8_t run_len = col - run_start;
                    _setWindow(x + run_start, y + row, x + col - 1, y + row);
                    uint8_t *p = _txbuf;
                    for (uint8_t i = 0; i < run_len; i++) {
                        *p++ = fr; *p++ = fg; *p++ = fb;
                    }
                    _dmaWrite(_txbuf, (uint32_t)run_len * 3);
                    in_run = false;
                }
            }
            if (in_run) {
                uint8_t run_len = gw - run_start;
                _setWindow(x + run_start, y + row, x + gw - 1, y + row);
                uint8_t *p = _txbuf;
                for (uint8_t i = 0; i < run_len; i++) {
                    *p++ = fr; *p++ = fg; *p++ = fb;
                }
                _dmaWrite(_txbuf, (uint32_t)run_len * 3);
            }
        }
        _endTransaction();
        return 6;
    }

    if (_textFont == 2) {
        uint32_t flash_addr  = pgm_read_dword(&chrtbl_f16[uc]);
        uint8_t  width       = pgm_read_byte(widtbl_f16 + uc);
        uint8_t  bpr         = (width + 6) / 8;  // bytes per row

        _beginTransaction();
        for (uint8_t row = 0; row < chr_hgt_f16; row++) {
            const uint8_t *rowdata = (const uint8_t *)flash_addr + (uint16_t)bpr * row;
            uint8_t col = 0, run_start = 0;
            bool in_run = false;

            for (uint8_t k = 0; k < bpr; k++) {
                uint8_t line = pgm_read_byte(rowdata + k);
                for (uint8_t bit = 0; bit < 8 && col < width; bit++, col++) {
                    bool set = (line & (0x80u >> bit)) != 0;
                    if (set && !in_run) {
                        run_start = col;
                        in_run = true;
                    } else if (!set && in_run) {
                        // Emit run [run_start, col-1]
                        uint8_t run_len = col - run_start;
                        _setWindow(x + run_start, y + row, x + col - 1, y + row);
                        uint8_t *p = _txbuf;
                        for (uint8_t i = 0; i < run_len; i++) {
                            *p++ = fr; *p++ = fg; *p++ = fb;
                        }
                        _dmaWrite(_txbuf, (uint32_t)run_len * 3);
                        in_run = false;
                    }
                }
            }
            // Flush trailing run.
            if (in_run) {
                uint8_t run_len = width - run_start;
                _setWindow(x + run_start, y + row, x + (int32_t)width - 1, y + row);
                uint8_t *p = _txbuf;
                for (uint8_t i = 0; i < run_len; i++) {
                    *p++ = fr; *p++ = fg; *p++ = fb;
                }
                _dmaWrite(_txbuf, (uint32_t)run_len * 3);
            }
        }
        _endTransaction();
        return (int16_t)width;
    }

    if (_textFont == 4) {
        // 8-bit RLE: bit7=1 → (n&0x7F)+1 foreground pixels; bit7=0 → skip.
        uint32_t flash_addr = pgm_read_dword(&chrtbl_f32[uc]);
        uint8_t  width      = pgm_read_byte(widtbl_f32 + uc);
        uint32_t total      = (uint32_t)width * chr_hgt_f32;
        uint32_t pc         = 0;

        _beginTransaction();
        while (pc < total) {
            uint8_t b   = pgm_read_byte((const uint8_t *)flash_addr++);
            uint32_t run = (uint32_t)(b & 0x7F) + 1;
            bool     fgrun = (b & 0x80) != 0;

            while (run > 0 && pc < total) {
                int32_t col = (int32_t)(pc % width);
                int32_t row = (int32_t)(pc / width);
                // Limit to end of current row so windows stay 1px tall.
                uint32_t until_eol = (uint32_t)(width - col);
                uint32_t take = run < until_eol ? run : until_eol;
                if (take > total - pc) take = total - pc;

                if (fgrun) {
                    _setWindow(x + col, y + row, x + col + (int32_t)take - 1, y + row);
                    uint8_t *p = _txbuf;
                    for (uint32_t i = 0; i < take; i++) {
                        *p++ = fr; *p++ = fg; *p++ = fb;
                    }
                    _dmaWrite(_txbuf, take * 3);
                }
                pc   += take;
                run  -= take;
            }
        }
        _endTransaction();
        return (int16_t)width;
    }

    return 0;
}

int16_t TFT_ILI9488::drawString(const char *str, int32_t x, int32_t y) {
    int32_t cx = x;
    while (*str) cx += _drawChar((uint8_t)*str++, cx, y);
    return (int16_t)(cx - x);
}

// ─── Touch (XPT2046) ─────────────────────────────────────────────────────────

void TFT_ILI9488::_touchBegin() {
    gpio_put(PIN_TFT_CS, 1); // ensure display CS is deasserted
    SPI1.beginTransaction(SPISettings(SPI_FREQ_TOUCH, MSBFIRST, SPI_MODE0));
    gpio_put(PIN_TOUCH_CS, 0);
}

void TFT_ILI9488::_touchEnd() {
    gpio_put(PIN_TOUCH_CS, 1);
    SPI1.endTransaction();
}

bool TFT_ILI9488::_getTouchRaw(uint16_t *x, uint16_t *y) {
    uint16_t tmp;
    _touchBegin();

    SPI1.transfer(0xD0); SPI1.transfer(0);
    SPI1.transfer(0xD0); SPI1.transfer(0);
    SPI1.transfer(0xD0); SPI1.transfer(0);
    SPI1.transfer(0xD0);
    tmp  = (uint16_t)SPI1.transfer(0) << 5;
    tmp |= (uint8_t)SPI1.transfer(0x90) >> 3;
    *x = tmp;

    SPI1.transfer(0); SPI1.transfer(0x90);
    SPI1.transfer(0); SPI1.transfer(0x90);
    SPI1.transfer(0); SPI1.transfer(0x90);
    tmp  = (uint16_t)SPI1.transfer(0) << 5;
    tmp |= (uint8_t)SPI1.transfer(0) >> 3;
    *y = tmp;

    _touchEnd();
    return true;
}

uint16_t TFT_ILI9488::_getTouchRawZ() {
    _touchBegin();
    int16_t tz = 0xFFF;
    SPI1.transfer(0xB0);
    tz += (int16_t)(SPI1.transfer16(0xC0) >> 3);
    tz -= (int16_t)(SPI1.transfer16(0x00) >> 3);
    _touchEnd();
    if (tz == 4095) tz = 0;
    return (uint16_t)tz;
}

#define _RAWERR 20
bool TFT_ILI9488::_validTouch(uint16_t *x, uint16_t *y, uint16_t threshold) {
    uint16_t x1, y1, x2, y2;
    uint16_t z1 = 1, z2 = 0;
    while (z1 > z2) { z2 = z1; z1 = _getTouchRawZ(); delay(1); }
    if (z1 <= threshold){
        //Serial.printf("failed: %u\n", z1); 
        return false;
    }
    _getTouchRaw(&x1, &y1);
    delay(1);
    if (_getTouchRawZ() <= threshold) return false;
    delay(2);
    _getTouchRaw(&x2, &y2);
    if (abs((int16_t)x1 - (int16_t)x2) > _RAWERR) return false;
    if (abs((int16_t)y1 - (int16_t)y2) > _RAWERR) return false;
    *x = x1; *y = y1;
    return true;
}

void TFT_ILI9488::_convertRawXY(uint16_t *x, uint16_t *y) {
    uint16_t xt = *x, yt = *y, xx, yy;
    if (!_tcal_rotate) {
        xx = (uint16_t)((int32_t)(xt - _tcal_x0) * _width  / _tcal_x1);
        yy = (uint16_t)((int32_t)(yt - _tcal_y0) * _height / _tcal_y1);
    } else {
        xx = (uint16_t)((int32_t)(yt - _tcal_x0) * _width  / _tcal_x1);
        yy = (uint16_t)((int32_t)(xt - _tcal_y0) * _height / _tcal_y1);
    }
    if (_tcal_invert_x) xx = (uint16_t)(_width  - xx);
    if (_tcal_invert_y) yy = (uint16_t)(_height - yy);
    *x = xx; *y = yy;
}

uint8_t TFT_ILI9488::getTouch(uint16_t *x, uint16_t *y, uint16_t threshold) {
    uint16_t xt, yt;
    if (threshold < 20) threshold = 20;
    uint8_t n = 5, valid = 0;
    while (n--) { if (_validTouch(&xt, &yt, threshold)) valid++; }
    if (!valid) return 0;
    //Serial.printf("valid touch\n");
    _convertRawXY(&xt, &yt);
    //if (xt >= (uint16_t)_width || yt >= (uint16_t)_height) return 0;
    *x = xt; *y = yt;
    return valid;
}

void TFT_ILI9488::calibrateTouch(uint16_t *parameters, uint32_t colorFG,
                                   uint32_t colorBG, uint8_t size) {
    int16_t values[8] = {0,0,0,0,0,0,0,0};
    uint16_t xt, yt;

    for (uint8_t i = 0; i < 4; i++) {
        fillRect(0,            0,            size+1, size+1, colorBG);
        fillRect(0,            _height-size-1, size+1, size+1, colorBG);
        fillRect(_width-size-1, 0,            size+1, size+1, colorBG);
        fillRect(_width-size-1, _height-size-1, size+1, size+1, colorBG);

        switch (i) {
            case 0:
                drawLine(0, 0, 0, size, colorFG);
                drawLine(0, 0, size, 0, colorFG);
                drawLine(0, 0, size, size, colorFG);
                break;
            case 1:
                drawLine(0, _height-size-1, 0, _height-1, colorFG);
                drawLine(0, _height-1, size, _height-1, colorFG);
                drawLine(size, _height-size-1, 0, _height-1, colorFG);
                break;
            case 2:
                drawLine(_width-size-1, 0, _width-1, 0, colorFG);
                drawLine(_width-size-1, size, _width-1, 0, colorFG);
                drawLine(_width-1, size, _width-1, 0, colorFG);
                break;
            case 3:
                drawLine(_width-size-1, _height-size-1, _width-1, _height-1, colorFG);
                drawLine(_width-1, _height-1-size, _width-1, _height-1, colorFG);
                drawLine(_width-1-size, _height-1, _width-1, _height-1, colorFG);
                break;
        }

        if (i > 0) delay(1000);
        for (uint8_t j = 0; j < 8; j++) {
            while (!_validTouch(&xt, &yt, 150));
            values[i*2  ] += (int16_t)xt;
            values[i*2+1] += (int16_t)yt;
        }
        values[i*2  ] /= 8;
        values[i*2+1] /= 8;
    }

    _tcal_rotate = (abs(values[0]-values[2]) > abs(values[1]-values[3]));
    if (_tcal_rotate) {
        _tcal_x0 = (uint16_t)((values[1]+values[3])/2);
        _tcal_x1 = (uint16_t)((values[5]+values[7])/2);
        _tcal_y0 = (uint16_t)((values[0]+values[4])/2);
        _tcal_y1 = (uint16_t)((values[2]+values[6])/2);
    } else {
        _tcal_x0 = (uint16_t)((values[0]+values[2])/2);
        _tcal_x1 = (uint16_t)((values[4]+values[6])/2);
        _tcal_y0 = (uint16_t)((values[1]+values[5])/2);
        _tcal_y1 = (uint16_t)((values[3]+values[7])/2);
    }
    _tcal_invert_x = false;
    if (_tcal_x0 > _tcal_x1) {
        uint16_t t=_tcal_x0; _tcal_x0=_tcal_x1; _tcal_x1=t;
        _tcal_invert_x = true;
    }
    _tcal_invert_y = false;
    if (_tcal_y0 > _tcal_y1) {
        uint16_t t=_tcal_y0; _tcal_y0=_tcal_y1; _tcal_y1=t;
        _tcal_invert_y = true;
    }
    _tcal_x1 -= _tcal_x0;
    _tcal_y1 -= _tcal_y0;
    if (_tcal_x0 == 0) _tcal_x0 = 1;
    if (_tcal_x1 == 0) _tcal_x1 = 1;
    if (_tcal_y0 == 0) _tcal_y0 = 1;
    if (_tcal_y1 == 0) _tcal_y1 = 1;

    if (parameters) {
        parameters[0] = _tcal_x0;
        parameters[1] = _tcal_x1;
        parameters[2] = _tcal_y0;
        parameters[3] = _tcal_y1;
        parameters[4] = (uint16_t)(_tcal_rotate | (_tcal_invert_x << 1) | (_tcal_invert_y << 2));
    }
}

void TFT_ILI9488::setTouch(uint16_t *parameters) {
    _tcal_x0 = parameters[0]; if (_tcal_x0 == 0) _tcal_x0 = 1;
    _tcal_x1 = parameters[1]; if (_tcal_x1 == 0) _tcal_x1 = 1;
    _tcal_y0 = parameters[2]; if (_tcal_y0 == 0) _tcal_y0 = 1;
    _tcal_y1 = parameters[3]; if (_tcal_y1 == 0) _tcal_y1 = 1;
    _tcal_rotate   = (parameters[4] & 0x01) != 0;
    _tcal_invert_x = (parameters[4] & 0x02) != 0;
    _tcal_invert_y = (parameters[4] & 0x04) != 0;
}
