/**
 * @file atech_gestures.h
 * @brief Tap / double tap / triple tap / long press for any push button.
 *
 * Used by ButtonModule and RotaryEncoder (its push switch). Edges are caught by
 * a pin-change interrupt with their timestamps, so a quick tap is never missed
 * even when the app's loop is busy drawing a screen or reading a card; a
 * polled button only sees taps longer than one loop iteration. poll() hands
 * the edges to AtechTapClassifier, which holds the timing rules.
 */

#pragma once
#include <Arduino.h>
#include "atech_tap_classifier.h"

class AtechGestureInput {
public:
    void begin(int pin, bool activeLow) {
        _pin = pin;
        _activeLow = activeLow;
        if (_pin < 0) return;
        attachInterruptArg(digitalPinToInterrupt(_pin), _isr, this, CHANGE);
    }

    void setTiming(uint16_t multiTapMs, uint16_t longPressMs) {
        _classifier.multiTapMs = multiTapMs;
        _classifier.longPressMs = longPressMs;
    }

    /** Drain captured edges and return a gesture if one just completed. */
    AtechGesture poll() {
        if (_pin < 0) return GESTURE_NONE;
        uint32_t times[kEdges];
        bool levels[kEdges];
        uint8_t n = 0;
        portENTER_CRITICAL(&_mux);
        while (_tail != _head && n < kEdges) {
            times[n] = _edgeMs[_tail];
            levels[n] = _edgeLevel[_tail];
            _tail = (_tail + 1) % kEdges;
            n++;
        }
        portEXIT_CRITICAL(&_mux);
        for (uint8_t i = 0; i < n; i++) _classifier.edge(levels[i], times[i]);
        return _classifier.tick(millis(), _pressedNow());
    }

private:
    static const uint8_t kEdges = 16;

    int _pin = -1;
    bool _activeLow = true;
    volatile uint32_t _edgeMs[kEdges];
    volatile bool _edgeLevel[kEdges];
    volatile uint8_t _head = 0;
    volatile uint8_t _tail = 0;
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    AtechTapClassifier _classifier;

    bool _pressedNow() const {
        int raw = digitalRead(_pin);
        return _activeLow ? (raw == LOW) : (raw == HIGH);
    }

    // Defined in atech_gestures.cpp: an IRAM_ATTR function defined inline in a
    // header fails to link on Xtensa ("dangerous relocation: l32r").
    static void IRAM_ATTR _isr(void* arg);
};
