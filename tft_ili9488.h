#pragma once
#include <Arduino.h>
#include <SPI.h>

// ─── RGB565 color constants ───────────────────────────────────────────────────
#define TFT_BLACK       0x0000
#define TFT_WHITE       0xFFFF
#define TFT_RED         0xF800
#define TFT_GREEN       0x07E0
#define TFT_BLUE        0x001F
#define TFT_CYAN        0x07FF
#define TFT_MAGENTA     0xF81F
#define TFT_YELLOW      0xFFE0
#define TFT_ORANGE      0xFD20
#define TFT_DARKGREEN   0x03E0
#define TFT_DARKGREY    0x7BEF
#define TFT_LIGHTGREY   0xC618
#define TFT_SKYBLUE     0x867D

// pgm_read shims (RP2040 flash is memory-mapped; these are plain dereferences)
#ifndef pgm_read_byte
  #define pgm_read_byte(addr)   (*(const uint8_t *)(addr))
#endif
#ifndef pgm_read_dword
  #define pgm_read_dword(addr)  (*(const uint32_t *)(addr))
#endif
#ifndef PROGMEM
  #define PROGMEM
#endif

// ─── Font metadata (mirrors TFT_eSPI's fontinfo) ─────────────────────────────
typedef struct {
    const uint8_t *chartbl;
    const uint8_t *widthtbl;
    uint8_t        height;
    uint8_t        baseline;
} fontinfo;

// fontdata[n] — same indexing as TFT_eSPI: 2=Font16 (16px), 4=Font32rle (26px)
extern const fontinfo fontdata[];

// GLCD 5×7 bitmap font (used by screen_draw_mono in screen_gx.cpp)
extern const unsigned char font[];

// ─── ILI9488 driver class ─────────────────────────────────────────────────────
class TFT_ILI9488 {
public:
    TFT_ILI9488();

    // Initialisation
    void init();
    void fillScreen(uint16_t color);
    void setRotation(uint8_t r);
    void setSwapBytes(bool swap);   // stored; affects pushImage byte order

    // Basic drawing
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
    void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color);
    void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color);
    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color);
    void drawPixel(int32_t x, int32_t y, uint16_t color);

    // Pixel-buffer blits
    // pushRect: swap bytes forced OFF (standard RGB565 in buffer)
    void pushRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data);
    // pushImage: obeys current _swapBytes setting
    void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data);

    // Text
    void    setTextFont(uint8_t f);
    void    setTextColor(uint16_t c);
    int16_t drawString(const char *str, int32_t x, int32_t y);
    int16_t textWidth(const char *str, uint8_t font);
    int16_t fontHeight(uint8_t font);

    // Touch (XPT2046)
    void    calibrateTouch(uint16_t *parameters, uint32_t colorFG, uint32_t colorBG, uint8_t size);
    void    setTouch(uint16_t *parameters);
    uint8_t getTouch(uint16_t *x, uint16_t *y, uint16_t threshold = 350);

private:
    // ── DMA helpers ──────────────────────────────────────────────────────────
    void _dmaInit();
    // Send `len` bytes from `buf` to SPI via DMA (or spi_write_blocking for
    // small transfers).  Must be called inside an active SPI transaction.
    void _dmaWrite(const uint8_t *buf, uint32_t len);
    // Send `count` solid-colour pixels (RGB888) using the internal TX buffer.
    void _fillPixels(uint32_t count, uint8_t r, uint8_t g, uint8_t b);

    // ── SPI helpers ──────────────────────────────────────────────────────────
    void _beginTransaction();
    void _endTransaction();
    void _writeCmd(uint8_t cmd);
    void _writeData(uint8_t data);
    void _setWindow(int32_t x0, int32_t y0, int32_t x1, int32_t y1);

    // Single-pixel write used only by drawPixel (diagonal lines).
    inline void _writePixel(uint16_t color) {
        uint8_t r = (color >> 11) << 3;
        uint8_t g = ((color >> 5) & 0x3F) << 2;
        uint8_t b = (color & 0x1F) << 3;
        SPI1.transfer(r);
        SPI1.transfer(g);
        SPI1.transfer(b);
    }

    // ── Font helpers ─────────────────────────────────────────────────────────
    int16_t _drawChar(uint8_t uniCode, int32_t x, int32_t y);
    int16_t _charWidth(uint8_t uniCode, uint8_t font);

    // ── Touch helpers ────────────────────────────────────────────────────────
    void     _touchBegin();
    void     _touchEnd();
    bool     _getTouchRaw(uint16_t *x, uint16_t *y);
    uint16_t _getTouchRawZ();
    bool     _validTouch(uint16_t *x, uint16_t *y, uint16_t threshold);
    void     _convertRawXY(uint16_t *x, uint16_t *y);

    // ── State ────────────────────────────────────────────────────────────────
    uint8_t  _textFont;
    uint16_t _textColor;
    bool     _swapBytes;
    int32_t  _width, _height;

    // DMA channel handles (-1 until _dmaInit() succeeds).
    int _dma_tx_ch, _dma_rx_ch;

    // Touch calibration (same 5-value layout as TFT_eSPI)
    uint16_t _tcal_x0, _tcal_x1;   // min and range
    uint16_t _tcal_y0, _tcal_y1;
    bool     _tcal_rotate;
    bool     _tcal_invert_x, _tcal_invert_y;
};
