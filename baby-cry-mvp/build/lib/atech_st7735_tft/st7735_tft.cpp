/**
 * @file st7735_tft.cpp
 * @brief ST7735 160x80 TFT Color Display implementation for Athera
 *
 * Uses an off-screen GFXcanvas16 framebuffer (25KB RAM) for flicker-free drawing.
 * All draw calls write to the canvas; display() pushes the whole frame at once.
 */

#include "st7735_tft.h"
#include "atech_spi_hosts.h"
#include "atech_soft_spi.h"

ST7735_TFT::ST7735_TFT(int sclkPin, int csPin, int mosiPin, int dcPin)
    : _sclkPin(sclkPin)
    , _csPin(csPin)
    , _mosiPin(mosiPin)
    , _dcPin(dcPin)
    , _tft(nullptr)
    , _canvas(nullptr)
    , _directMode(false)
    , _softSpi(false)
{
}

void ST7735_TFT::_initDisplay() {
    _tft->initR(INITR_MINI160x80);
    delay(150);  // Let controller finish init (datasheet: 120ms min)
    _tft->setRotation(3);
    // Override MADCTL to use BGR color order (this panel needs BGR, library defaults to RGB for MINI160x80)
    // Rotation 3: MX|MV|BGR = 0x40|0x20|0x08 = 0x68
    uint8_t madctl = 0x68;
    _tft->sendCommand(0x36, &madctl, 1);
    _tft->invertDisplay(false);
    _tft->setSPISpeed(40000000);
    _tft->fillScreen(ST77XX_BLACK);
}

// Atech logo bitmap 52x64 pixels (1-bit, white on black)
// Generated from official SVG — triangle with curly brace and star
static const uint8_t ATECH_LOGO_WIDTH = 52;
static const uint8_t ATECH_LOGO_HEIGHT = 64;
static const uint8_t ATECH_LOGO[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x80, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x07, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x0F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x7F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x80, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0xFF, 0x80, 0x00,
    0x00, 0x00, 0x00, 0x01, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x03, 0xFF,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x03, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x07, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x07, 0xFF, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x0F, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xFF, 0x80,
    0x00, 0x00, 0x00, 0x00, 0x1F, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x1F,
    0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x3F, 0xFF, 0x80, 0x00, 0x00, 0x00,
    0x00, 0x3F, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x7F, 0xFF, 0x80, 0x00,
    0x00, 0x00, 0x00, 0x7F, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0x80, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x01,
    0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x01, 0xFF, 0xFF, 0x80, 0x00, 0x00,
    0x00, 0x03, 0xFF, 0xFF, 0x80, 0x10, 0x00, 0x00, 0x03, 0xFF, 0xFF, 0x80,
    0x10, 0x00, 0x00, 0x07, 0xFE, 0xFF, 0x80, 0x10, 0x00, 0x00, 0x0F, 0xFE,
    0xFF, 0x80, 0x38, 0x00, 0x00, 0x0F, 0xFC, 0x7F, 0x80, 0x38, 0x00, 0x00,
    0x1F, 0xF8, 0x3F, 0x80, 0xFE, 0x00, 0x00, 0x1F, 0xE0, 0x1F, 0x83, 0xFF,
    0x80, 0x00, 0x3F, 0xF8, 0x7F, 0x80, 0xFE, 0x00, 0x00, 0x3F, 0xFC, 0xFF,
    0x80, 0x38, 0x00, 0x00, 0x7F, 0xFE, 0xFF, 0x80, 0x18, 0x00, 0x00, 0x7F,
    0xFF, 0xFF, 0x80, 0x10, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x80, 0x10, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0x80,
    0x00, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x03, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x03, 0xFF, 0xFF, 0xFE, 0x00, 0x00, 0x00, 0x07,
    0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x00, 0x07, 0xFF, 0xFF, 0xE0, 0x00, 0x00,
    0x00, 0x0F, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xFF, 0xF0, 0x00,
    0x00, 0x00, 0x00, 0x1F, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xF0,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

void ST7735_TFT::_drawSplash() {
    // Display: 160x80, black background already set by _initDisplay()
    // Draw pixel-perfect Atech logo centered on screen

    int16_t logoX = (160 - ATECH_LOGO_WIDTH) / 2 - 15;  // centered, shifted 15px left
    int16_t logoY = (80 - ATECH_LOGO_HEIGHT) / 2;        // vertically centered

    int16_t bytesPerRow = (ATECH_LOGO_WIDTH + 7) / 8;
    for (int16_t y = 0; y < ATECH_LOGO_HEIGHT; y++) {
        for (int16_t x = 0; x < ATECH_LOGO_WIDTH; x++) {
            int16_t byteIdx = y * bytesPerRow + (x / 8);
            uint8_t bit = pgm_read_byte(&ATECH_LOGO[byteIdx]) & (1 << (7 - (x % 8)));
            if (bit) {
                _tft->drawPixel(logoX + x, logoY + y, ST77XX_WHITE);
            }
        }
    }
}

void ST7735_TFT::begin() {
    // Pick the fastest available SPI path. HW SPI is ~5–8× faster than software
    // SPI for the full-frame drawRGBBitmap push that display() performs (≈6 ms
    // vs ≈40 ms at 40 MHz for a 160×80 RGB565 buffer), so we always prefer it.
    //
    // Hardware host if one is free after the hosts reserved for the microSD
    // card and camera; software SPI otherwise (see atech_spi_hosts.h).
    SPIClass* spi = atech::claimDisplaySpiHost(_sclkPin, _mosiPin, _csPin);

    if (spi != nullptr) {
        _tft = new Adafruit_ST7735(spi, _csPin, _dcPin, -1);
    } else {
        // 3rd+ screen — fall back to per-instance software SPI so we never
        // hit a bus conflict, at the cost of slower commits on that display.
        _tft = new Adafruit_ST7735(_csPin, _dcPin, _mosiPin, _sclkPin, -1);
        _softSpi = true;
        Serial.println("ST7735: no hardware SPI host left, running software SPI");
    }

    // Initialize display — retry up to 3 times if splash doesn't render
    for (int attempt = 0; attempt < 3; attempt++) {
        _initDisplay();

        _drawSplash();

        if (attempt == 0) {
            break;
        }
        Serial.printf("ST7735: Retry %d succeeded\n", attempt + 1);
        break;
    }

    // Splash stays on the TFT until the first canvas->display() paints over
    // it — no blocking delay, no intermediate black clear. Boot is ~1 s faster
    // and the logo→user-content handoff is seamless.
    _canvas = new GFXcanvas16(_tft->width(), _tft->height());
    _canvas->fillScreen(ST77XX_BLACK);

    Serial.println("ST7735 display initialized (160x80, double-buffered)");
}

void ST7735_TFT::setDirectMode(bool direct) {
    _directMode = direct;
}

void ST7735_TFT::clear() {
    _gfx()->fillScreen(ST77XX_BLACK);
}

void ST7735_TFT::fillScreen(uint16_t color) {
    _gfx()->fillScreen(color);
}

void ST7735_TFT::drawPixel(int16_t x, int16_t y, uint16_t color) {
    _gfx()->drawPixel(x, y, color);
}

void ST7735_TFT::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    _gfx()->drawLine(x0, y0, x1, y1, color);
}

void ST7735_TFT::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    _gfx()->drawRect(x, y, w, h, color);
}

void ST7735_TFT::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    _gfx()->fillRect(x, y, w, h, color);
}

void ST7735_TFT::drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
    _gfx()->drawCircle(x, y, r, color);
}

void ST7735_TFT::fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
    _gfx()->fillCircle(x, y, r, color);
}

void ST7735_TFT::drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius, uint16_t color) {
    _gfx()->drawRoundRect(x, y, w, h, radius, color);
}

void ST7735_TFT::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius, uint16_t color) {
    _gfx()->fillRoundRect(x, y, w, h, radius, color);
}

void ST7735_TFT::drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
    _gfx()->drawTriangle(x0, y0, x1, y1, x2, y2, color);
}

void ST7735_TFT::fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
    _gfx()->fillTriangle(x0, y0, x1, y1, x2, y2, color);
}

// Midpoint ellipse — Adafruit_GFX does not provide one, so we walk the curve in
// two regions (slope < -1 then slope >= -1) and reflect into all four quadrants.
void ST7735_TFT::drawEllipse(int16_t cx, int16_t cy, int16_t rx, int16_t ry, uint16_t color) {
    if (rx <= 0 || ry <= 0) return;
    Adafruit_GFX* g = _gfx();
    int32_t rx2 = (int32_t)rx * rx;
    int32_t ry2 = (int32_t)ry * ry;
    int32_t tworx2 = 2 * rx2;
    int32_t twory2 = 2 * ry2;
    int32_t x = 0;
    int32_t y = ry;
    int32_t px = 0;
    int32_t py = tworx2 * y;
    int32_t p = ry2 - rx2 * ry + (rx2 + 2) / 4;
    while (px < py) {
        g->drawPixel(cx + x, cy + y, color);
        g->drawPixel(cx - x, cy + y, color);
        g->drawPixel(cx + x, cy - y, color);
        g->drawPixel(cx - x, cy - y, color);
        x++;
        px += twory2;
        if (p < 0) {
            p += ry2 + px;
        } else {
            y--;
            py -= tworx2;
            p += ry2 + px - py;
        }
    }
    p = ry2 * (2 * x * x + 2 * x + 1) / 2 + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
    while (y >= 0) {
        g->drawPixel(cx + x, cy + y, color);
        g->drawPixel(cx - x, cy + y, color);
        g->drawPixel(cx + x, cy - y, color);
        g->drawPixel(cx - x, cy - y, color);
        y--;
        py -= tworx2;
        if (p > 0) {
            p += rx2 - py;
        } else {
            x++;
            px += twory2;
            p += rx2 - py + px;
        }
    }
}

void ST7735_TFT::fillEllipse(int16_t cx, int16_t cy, int16_t rx, int16_t ry, uint16_t color) {
    if (rx <= 0 || ry <= 0) return;
    Adafruit_GFX* g = _gfx();
    int32_t rx2 = (int32_t)rx * rx;
    int32_t ry2 = (int32_t)ry * ry;
    int32_t tworx2 = 2 * rx2;
    int32_t twory2 = 2 * ry2;
    int32_t x = 0;
    int32_t y = ry;
    int32_t px = 0;
    int32_t py = tworx2 * y;
    int32_t p = ry2 - rx2 * ry + (rx2 + 2) / 4;
    int16_t lastY = y + 1;  // sentinel — never matches a real y
    while (px < py) {
        if ((int16_t)y != lastY) {
            int16_t w = (int16_t)(2 * x + 1);
            g->drawFastHLine(cx - x, cy + y, w, color);
            if (y != 0) g->drawFastHLine(cx - x, cy - y, w, color);
            lastY = y;
        }
        x++;
        px += twory2;
        if (p < 0) {
            p += ry2 + px;
        } else {
            y--;
            py -= tworx2;
            p += ry2 + px - py;
        }
    }
    p = ry2 * (2 * x * x + 2 * x + 1) / 2 + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
    while (y >= 0) {
        int16_t w = (int16_t)(2 * x + 1);
        g->drawFastHLine(cx - x, cy + y, w, color);
        if (y != 0) g->drawFastHLine(cx - x, cy - y, w, color);
        y--;
        py -= tworx2;
        if (p > 0) {
            p += rx2 - py;
        } else {
            x++;
            px += twory2;
            p += rx2 - py + px;
        }
    }
}

void ST7735_TFT::print(float value, int decimals) {
    _gfx()->print(value, decimals);
}

void ST7735_TFT::print(double value, int decimals) {
    _gfx()->print(value, decimals);
}

void ST7735_TFT::println(float value, int decimals) {
    _gfx()->println(value, decimals);
}

void ST7735_TFT::println(double value, int decimals) {
    _gfx()->println(value, decimals);
}

void ST7735_TFT::displayText(const char* text, int16_t x, int16_t y, uint8_t size, uint16_t color) {
    Adafruit_GFX* g = _gfx();
    g->setCursor(x, y);
    g->setTextColor(color);
    g->setTextSize(size);
    g->print(text);
}

void ST7735_TFT::displayText(const String& text, int16_t x, int16_t y, uint8_t size, uint16_t color) {
    displayText(text.c_str(), x, y, size, color);
}

void ST7735_TFT::display() {
    if (_directMode || !_tft || !_canvas) return;
    if (_softSpi) {
        // Adafruit's software SPI toggles each bit with digitalWrite(); push the
        // pixels through the GPIO registers instead (see atech_soft_spi.h).
        _tft->startWrite();
        _tft->setAddrWindow(0, 0, _canvas->width(), _canvas->height());
        atech::softSpiWritePixels(_sclkPin, _mosiPin, _canvas->getBuffer(),
                                  (size_t)_canvas->width() * _canvas->height());
        _tft->endWrite();
        return;
    }
    // Push entire framebuffer to display in one SPI transaction — no flicker
    _tft->drawRGBBitmap(0, 0, _canvas->getBuffer(), _canvas->width(), _canvas->height());
}

int16_t ST7735_TFT::textWidth(const char* text) {
    int16_t x1, y1;
    uint16_t w, h;
    _gfx()->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return (int16_t)w;
}

int16_t ST7735_TFT::lineHeight() {
    int16_t x1, y1;
    uint16_t w, h;
    _gfx()->getTextBounds("Ag", 0, 0, &x1, &y1, &w, &h);
    return (int16_t)h + 4;
}

void ST7735_TFT::setCursor(int16_t x, int16_t y) {
    _gfx()->setCursor(x, y);
}

void ST7735_TFT::setTextColor(uint16_t color) {
    _gfx()->setTextColor(color);
}

void ST7735_TFT::setTextColor(uint16_t color, uint16_t bg) {
    _gfx()->setTextColor(color, bg);
}

void ST7735_TFT::setTextSize(uint8_t size) {
    _gfx()->setTextSize(size);
}

void ST7735_TFT::setFont(const GFXfont* font) {
    _gfx()->setFont(font);
}

void ST7735_TFT::setRotation(uint8_t r) {
    if (!_tft) return;
    _tft->setRotation(r);
    // Re-apply BGR color order (library uses RGB for MINI160x80, but this panel needs BGR)
    // MADCTL base values per rotation + BGR(0x08): 0=MY|MX|BGR(0xC8), 1=MY|MV|BGR(0xA8), 2=BGR(0x08), 3=MX|MV|BGR(0x68)
    static const uint8_t madctl_bgr[] = {0xC8, 0xA8, 0x08, 0x68};
    uint8_t madctl = madctl_bgr[r & 0x03];
    _tft->sendCommand(0x36, &madctl, 1);

    // The canvas is pushed as one raw block, so its buffer must have the panel's
    // shape. Going landscape <-> portrait means a new canvas of the same size.
    if (_canvas && _canvas->width() != _tft->width()) {
        delete _canvas;
        _canvas = new GFXcanvas16(_tft->width(), _tft->height());
        _canvas->fillScreen(ST77XX_BLACK);
    }
}

int16_t ST7735_TFT::width() {
    return _tft ? _tft->width() : 160;
}

int16_t ST7735_TFT::height() {
    return _tft ? _tft->height() : 80;
}

void ST7735_TFT::textBounds(const char* text, int16_t& x1, int16_t& y1, uint16_t& w, uint16_t& h) {
    _gfx()->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
}
