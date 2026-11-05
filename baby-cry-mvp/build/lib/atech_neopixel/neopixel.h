/**
 * @file neopixel.h
 * @brief NeoPixel 3x3 LED Grid Module for Athera
 *
 * Controls a 3x3 grid of NeoPixel LEDs (9 LEDs in series). Drives both
 * module revisions transparently: the original RGB (WS2812B) module wired
 * on Line A, and the newer RGBW (SK6812) module wired on Line B. Color
 * data is written to both lines every frame; whichever module is plugged
 * in latches the signal on its own line and ignores the other. The RGBW
 * line drives its dedicated white LED with the shared achromatic component
 * (W = min(r,g,b)) rather than mixing white from R+G+B.
 *
 * Brightness defaults to a comfortable ceiling of DEFAULT_MAX_BRIGHTNESS
 * (20% of the raw 0-255 range) so the newer RGBW module is not painfully
 * bright out of the box. Callers that genuinely need more can raise the
 * ceiling with setBrightnessCap() — up to the full hardware max of 255.
 *
 * Grid layout (index):
 *   0  1  2
 *   3  4  5
 *   6  7  8
 *
 * Athera Connector:
 * - Pin 1: 5V (LED power)
 * - Pin 4: GND
 * - Pin 5: Data (Line A) — RGB module
 * - Pin 6: Data (Line B) — RGBW module
 */

#ifndef NEOPIXEL_MODULE_H
#define NEOPIXEL_MODULE_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

class NeoPixelGrid {
public:
    static const uint8_t GRID_SIZE = 3;
    static const uint8_t NUM_LEDS = 9;
    static const uint8_t DEFAULT_MAX_BRIGHTNESS = 51;  // 20% of 255 — comfortable default ceiling

    /**
     * @brief Construct NeoPixel grid controller
     * @param dataPinA GPIO for Line A (RGB module)
     * @param dataPinB GPIO for Line B (RGBW module)
     * @param numLeds Number of LEDs (default 9 for 3x3)
     */
    NeoPixelGrid(int dataPinA, int dataPinB, uint8_t numLeds = NUM_LEDS);

    /**
     * @brief Initialize the LED strip
     */
    void begin();

    /**
     * @brief Set a single pixel by index (0-8)
     * @param index Pixel index
     * @param r Red (0-255)
     * @param g Green (0-255)
     * @param b Blue (0-255)
     */
    void setPixel(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

    /**
     * @brief Set a single pixel with an explicit white channel (RGBW module only)
     *
     * Use this only if you have the newer RGBW (v2) module and want to drive
     * its dedicated white LED yourself instead of the automatic white the
     * plain RGB methods derive. On the older RGB module `w` is ignored and the
     * pixel simply shows r,g,b, so code using this stays safe on either module.
     * @param index Pixel index (0-8)
     * @param r Red (0-255)
     * @param g Green (0-255)
     * @param b Blue (0-255)
     * @param w White (0-255) — rendered only by the RGBW module
     */
    void setPixelRGBW(uint8_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t w);

    /**
     * @brief Set a single pixel by grid position
     * @param row Row (0-2)
     * @param col Column (0-2)
     * @param r Red (0-255)
     * @param g Green (0-255)
     * @param b Blue (0-255)
     */
    void setPixelXY(uint8_t row, uint8_t col, uint8_t r, uint8_t g, uint8_t b);

    /**
     * @brief Set all pixels to the same color
     * @param r Red (0-255)
     * @param g Green (0-255)
     * @param b Blue (0-255)
     */
    void setAll(uint8_t r, uint8_t g, uint8_t b);

    /**
     * @brief Set all pixels with an explicit white channel (RGBW module only)
     *
     * Like setAll(), but you supply the white value directly rather than
     * letting the driver derive it. Only the RGBW (v2) module renders `w`;
     * the older RGB module ignores it and shows r,g,b.
     * @param r Red (0-255)
     * @param g Green (0-255)
     * @param b Blue (0-255)
     * @param w White (0-255) — rendered only by the RGBW module
     */
    void setAllRGBW(uint8_t r, uint8_t g, uint8_t b, uint8_t w);

    /**
     * @brief Set an entire row to a color
     * @param row Row index (0-2)
     * @param r Red (0-255)
     * @param g Green (0-255)
     * @param b Blue (0-255)
     */
    void setRow(uint8_t row, uint8_t r, uint8_t g, uint8_t b);

    /**
     * @brief Set an entire column to a color
     * @param col Column index (0-2)
     * @param r Red (0-255)
     * @param g Green (0-255)
     * @param b Blue (0-255)
     */
    void setColumn(uint8_t col, uint8_t r, uint8_t g, uint8_t b);

    /**
     * @brief Turn off all pixels
     */
    void clear();

    /**
     * @brief Update the physical LEDs with current buffer
     */
    void show();

    /**
     * @brief Set global brightness. Values above the current cap (default
     *        DEFAULT_MAX_BRIGHTNESS = 51 = 20% of 255) are clamped to protect
     *        against the RGBW module's high output. Raise the cap first with
     *        setBrightnessCap() if you need the full range.
     * @param brightness 0-255 (clamped to the current brightness cap)
     */
    void setBrightness(uint8_t brightness);

    /**
     * @brief Raise or lower the brightness ceiling.
     *
     * Defaults to DEFAULT_MAX_BRIGHTNESS (51, 20%). Pass a higher value — up
     * to 255 — to unlock more of the hardware range when the user explicitly
     * wants a brighter display. If the current brightness is above the new
     * cap it is pulled down to match.
     * @param cap Maximum brightness setBrightness() will allow (0-255)
     */
    void setBrightnessCap(uint8_t cap);

    /**
     * @brief Get current brightness
     */
    uint8_t getBrightness() const;

    /**
     * @brief Get a pixel's color as packed 32-bit (0x00RRGGBB)
     * @param index Pixel index
     * @return Packed color, or 0 if index out of range
     */
    uint32_t getPixelColor(uint8_t index) const;

    /**
     * @brief Convert row/col to linear index
     */
    uint8_t xyToIndex(uint8_t row, uint8_t col) const {
        return row * GRID_SIZE + col;
    }

private:
    /**
     * @brief Pack a color for the RGBW strip, moving the shared achromatic
     *        component onto the dedicated white channel (W = min(r,g,b)).
     */
    uint32_t rgbwColor(uint8_t r, uint8_t g, uint8_t b);

    Adafruit_NeoPixel _stripA;  // RGB, Line A (old module)
    Adafruit_NeoPixel _stripB;  // RGBW, Line B (new module)
    int _dataPinA;
    int _dataPinB;
    uint8_t _numLeds;
    uint8_t _maxBrightness;  // current brightness ceiling (starts at DEFAULT_MAX_BRIGHTNESS)
};

#endif // NEOPIXEL_MODULE_H
