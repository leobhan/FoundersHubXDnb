/**
 * @file button.h
 * @brief Single Button Input Module for Athera
 *
 * Simple digital button input with debouncing. Active-low (pressed = LOW)
 * with internal pull-up.
 *
 * Drives both button revisions transparently:
 *   - Older buttons (pre-V2.0): just the switch. A fixed blue LED lights while
 *     the button is electrically held down — this is hardwired and not under
 *     firmware control.
 *   - V2.0 buttons: add a single addressable RGB NeoPixel on Line B. The light
 *     API below drives it for on/off and RGB effects. On an older button Line B
 *     is unconnected, so every light call is a harmless no-op — generated code
 *     is identical regardless of which revision is plugged in.
 *
 * (You can tell the two apart physically: press a V2.0 and the pixel is white,
 * press an older one and its fixed indicator is blue.)
 *
 * Athera Connector:
 * - Pin 5: Button (Data Line A)
 * - Pin 6: RGB NeoPixel data (Data Line B) — V2.0 only
 * - Pin 4: GND
 */

#ifndef BUTTON_MODULE_H
#define BUTTON_MODULE_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "atech_gestures.h"

class ButtonModule {
public:
    // Default brightness ceiling (20% of 255), matching the NeoPixel grid, so
    // the V2.0 light never blinds at close range. Raise it with
    // setBrightnessCap() (up to 255) when a brighter light is wanted.
    static const uint8_t DEFAULT_MAX_BRIGHTNESS = 51;

    /**
     * @brief Construct button module
     * @param pin GPIO for the button (Line A)
     * @param ledPin GPIO for the V2.0 RGB light (Line B). Pass -1 to disable
     *        the light entirely. Safe to pass Line B's GPIO even for an older
     *        button — the light calls just no-op on hardware without a pixel.
     * @param activeLow true if pressed = LOW (default)
     */
    ButtonModule(int pin, int ledPin = -1, bool activeLow = true);

    /**
     * @brief Initialize the button pin (internal pull-up) and, when present,
     *        the V2.0 RGB light (starts off).
     */
    void begin();

    /**
     * @brief Check if button is currently pressed
     */
    bool isPressed();

    /**
     * @brief Check if button was just pressed (edge detection)
     * Call this in loop() - returns true once per press
     */
    bool wasPressed();

    /**
     * @brief Check if button was just released (edge detection)
     */
    bool wasReleased();

    /**
     * @brief Update button state (call in loop for edge detection)
     */
    void update();

    /**
     * @brief Get raw button state (0 or 1)
     */
    int getState();

    /**
     * @brief Tap, double tap, triple tap or long press, once each, else GESTURE_NONE.
     *
     * Call every loop. Returns GESTURE_TAP / GESTURE_DOUBLE_TAP / GESTURE_TRIPLE_TAP
     * when a tap sequence ends (the button has been up for the multi-tap window,
     * 400 ms by default) and GESTURE_LONG_PRESS while still held (700 ms default).
     * Presses are caught by an interrupt, so quick taps are not missed while the
     * loop is busy. A single TAP arrives 400 ms after release; for instant
     * single-press reactions use wasPressed() instead.
     */
    AtechGesture getGesture();

    /** @brief Change the multi-tap window and the long-press time (ms). */
    void setGestureTiming(uint16_t multiTapMs = 400, uint16_t longPressMs = 700);


    // --- V2.0 RGB light --------------------------------------------------
    // All light calls apply immediately (no separate show() needed) and are
    // no-ops on older buttons that have no pixel.

    /**
     * @brief Set the RGB light to a color and show it immediately.
     *        Remembers the color so a later on() can restore it.
     * @param r Red (0-255)
     * @param g Green (0-255)
     * @param b Blue (0-255)
     */
    void setColor(uint8_t r, uint8_t g, uint8_t b);

    /**
     * @brief Turn the light on. Restores the last setColor(), or white if the
     *        color has never been set.
     */
    void on();

    /**
     * @brief Turn the light off (does not forget the last color).
     */
    void off();

    /**
     * @brief Whether the light is currently lit.
     */
    bool isLit() const;

    /**
     * @brief Set the light brightness. Values above the current cap (default
     *        DEFAULT_MAX_BRIGHTNESS = 51 = 20% of 255) are clamped. Applies to
     *        the current color immediately. Raise the cap with
     *        setBrightnessCap() first if you need the full range.
     */
    void setBrightness(uint8_t brightness);

    /**
     * @brief Raise or lower the brightness ceiling (default 51 = 20%; up to
     *        255 for full output). Use only when the user explicitly wants a
     *        brighter-than-default light. Re-applies the current brightness if
     *        it now exceeds the new cap.
     * @param cap Maximum brightness setBrightness() will allow (0-255)
     */
    void setBrightnessCap(uint8_t cap);

    /**
     * @brief Whether this button has a controllable V2.0 RGB light.
     */
    bool hasLight() const { return _ledPin >= 0; }

private:
    void applyLight();

    int _pin;
    bool _activeLow;
    bool _lastState;
    bool _currentState;
    bool _pressedFlag;
    bool _releasedFlag;

    // V2.0 RGB light (single NeoPixel on Line B)
    int _ledPin;
    Adafruit_NeoPixel _led;
    uint8_t _r, _g, _b;   // last requested color
    bool _lit;            // whether the light is currently on
    uint8_t _maxBrightness;  // current brightness ceiling (starts at DEFAULT_MAX_BRIGHTNESS)

    AtechGestureInput _gestures;
};

#endif // BUTTON_MODULE_H
