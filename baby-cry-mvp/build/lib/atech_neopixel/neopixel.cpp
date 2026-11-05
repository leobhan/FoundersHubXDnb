/**
 * @file neopixel.cpp
 * @brief NeoPixel 3x3 LED Grid Implementation
 *
 * Writes every color update to both the RGB strip on Line A and the RGBW
 * strip on Line B. The module that is physically plugged in latches data
 * from its own line; the unused line is a no-op. This makes the driver
 * transparent to both revisions at the cost of ~5ms per show() (Adafruit's
 * legacy-IDF path installs and uninstalls an RMT channel on every call,
 * twice here — once per strip). If you hammer show() from a hot loop you
 * will see catch-up lag; coalesce interactive updates at ~20-33ms instead.
 *
 * Advantage over a persistent-RMT-channel driver: this scales to N module
 * instances because Adafruit's transient channel allocation means we are
 * not bound by the 4-TX-channel hardware limit on ESP32-S3.
 */

#include "neopixel.h"

NeoPixelGrid::NeoPixelGrid(int dataPinA, int dataPinB, uint8_t numLeds)
    : _stripA(numLeds, dataPinA, NEO_GRB + NEO_KHZ800)
    , _stripB(numLeds, dataPinB, NEO_GRBW + NEO_KHZ800)
    , _dataPinA(dataPinA)
    , _dataPinB(dataPinB)
    , _numLeds(numLeds)
    , _maxBrightness(DEFAULT_MAX_BRIGHTNESS)
{
}

void NeoPixelGrid::begin() {
    _stripA.begin();
    _stripB.begin();
    _stripA.setBrightness(_maxBrightness);
    _stripB.setBrightness(_maxBrightness);
    clear();
    show();
    Serial.print("[NeoPixel] Initialized ");
    Serial.print(_numLeds);
    Serial.print(" LEDs on pins A=");
    Serial.print(_dataPinA);
    Serial.print(" B=");
    Serial.println(_dataPinB);
}

uint32_t NeoPixelGrid::rgbwColor(uint8_t r, uint8_t g, uint8_t b) {
    // Extract the achromatic (white) component so the RGBW module's dedicated
    // white LED renders the shared portion instead of mixing it from R+G+B.
    // This gives truer, more efficient whites; the RGB module on Line A is
    // driven from the full r,g,b and is unaffected by this split.
    uint8_t w = r < g ? r : g;
    w = w < b ? w : b;
    return _stripB.Color(r - w, g - w, b - w, w);
}

void NeoPixelGrid::setPixel(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (index < _numLeds) {
        _stripA.setPixelColor(index, _stripA.Color(r, g, b));
        _stripB.setPixelColor(index, rgbwColor(r, g, b));
    }
}

void NeoPixelGrid::setPixelRGBW(uint8_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (index < _numLeds) {
        // Explicit white: skip the automatic min(r,g,b) extraction and drive
        // the RGBW module's white LED with exactly `w`. The older RGB module
        // has no white LED, so it just shows r,g,b and ignores `w`.
        _stripA.setPixelColor(index, _stripA.Color(r, g, b));
        _stripB.setPixelColor(index, _stripB.Color(r, g, b, w));
    }
}

void NeoPixelGrid::setPixelXY(uint8_t row, uint8_t col, uint8_t r, uint8_t g, uint8_t b) {
    if (row < GRID_SIZE && col < GRID_SIZE) {
        setPixel(xyToIndex(row, col), r, g, b);
    }
}

void NeoPixelGrid::setAllRGBW(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    uint32_t colorA = _stripA.Color(r, g, b);        // RGB module: no white LED, shows r,g,b
    uint32_t colorB = _stripB.Color(r, g, b, w);     // RGBW module: explicit white channel
    for (uint8_t i = 0; i < _numLeds; i++) {
        _stripA.setPixelColor(i, colorA);
        _stripB.setPixelColor(i, colorB);
    }
}

void NeoPixelGrid::setAll(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t colorA = _stripA.Color(r, g, b);
    uint32_t colorB = rgbwColor(r, g, b);
    for (uint8_t i = 0; i < _numLeds; i++) {
        _stripA.setPixelColor(i, colorA);
        _stripB.setPixelColor(i, colorB);
    }
}

void NeoPixelGrid::setRow(uint8_t row, uint8_t r, uint8_t g, uint8_t b) {
    if (row < GRID_SIZE) {
        uint32_t colorA = _stripA.Color(r, g, b);
        uint32_t colorB = rgbwColor(r, g, b);
        for (uint8_t col = 0; col < GRID_SIZE; col++) {
            uint8_t i = xyToIndex(row, col);
            _stripA.setPixelColor(i, colorA);
            _stripB.setPixelColor(i, colorB);
        }
    }
}

void NeoPixelGrid::setColumn(uint8_t col, uint8_t r, uint8_t g, uint8_t b) {
    if (col < GRID_SIZE) {
        uint32_t colorA = _stripA.Color(r, g, b);
        uint32_t colorB = rgbwColor(r, g, b);
        for (uint8_t row = 0; row < GRID_SIZE; row++) {
            uint8_t i = xyToIndex(row, col);
            _stripA.setPixelColor(i, colorA);
            _stripB.setPixelColor(i, colorB);
        }
    }
}

void NeoPixelGrid::clear() {
    _stripA.clear();
    _stripB.clear();
}

void NeoPixelGrid::show() {
    _stripA.show();
    _stripB.show();
}

void NeoPixelGrid::setBrightness(uint8_t brightness) {
    uint8_t clamped = brightness > _maxBrightness ? _maxBrightness : brightness;
    if (_stripA.getBrightness() == clamped) return;  // no-op: avoid flooding Serial when callers tick this every frame
    _stripA.setBrightness(clamped);
    _stripB.setBrightness(clamped);
    Serial.print("[NeoPixel] Brightness set to ");
    Serial.print(clamped);
    if (clamped != brightness) {
        Serial.print(" (requested ");
        Serial.print(brightness);
        Serial.print(", clamped to cap ");
        Serial.print(_maxBrightness);
        Serial.print(")");
    }
    Serial.println();
}

void NeoPixelGrid::setBrightnessCap(uint8_t cap) {
    _maxBrightness = cap;
    // Pull the current brightness down if it now sits above the new ceiling
    // (setBrightness re-clamps and logs the change).
    if (_stripA.getBrightness() > _maxBrightness) {
        setBrightness(_maxBrightness);
    }
}

uint8_t NeoPixelGrid::getBrightness() const {
    return _stripA.getBrightness();
}

uint32_t NeoPixelGrid::getPixelColor(uint8_t index) const {
    if (index >= _numLeds) return 0;
    return _stripA.getPixelColor(index);
}
