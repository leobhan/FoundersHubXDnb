/**
 * @file atech_tap_classifier.h
 * @brief Turns button edges into tap / double tap / triple tap / long press.
 *
 * Pure logic with no Arduino dependency, so it can be unit-tested on a host
 * (tests/test_gestures.py compiles it with g++). The ESP32 side, which captures
 * the edges in an interrupt, is AtechGestureInput in atech_gestures.h.
 *
 * Rules:
 *   - A tap sequence is a run of presses where each new press comes within
 *     multiTapMs of the previous RELEASE. It ends once the button has been up
 *     for multiTapMs, and reports TAP, DOUBLE_TAP or TRIPLE_TAP (4+ taps count
 *     as a triple). The window runs from release to the next press, not to the
 *     next release, so a relaxed double tap is not split into two single taps.
 *   - Holding for longPressMs reports LONG_PRESS once, while still held, and
 *     cancels the sequence, so the release that follows reports nothing.
 *   - A single TAP is therefore reported multiTapMs after release, the cost of
 *     telling it apart from a double. Code that only needs presses should keep
 *     using wasPressed(), which is instant.
 *   - Edges closer than debounceMs to the last accepted edge are contact
 *     bounce and are ignored; tick() re-reads the real level, so an ignored
 *     edge cannot leave the state stuck.
 */

#pragma once
#include <stdint.h>

enum AtechGesture : uint8_t {
    GESTURE_NONE = 0,
    GESTURE_TAP,
    GESTURE_DOUBLE_TAP,
    GESTURE_TRIPLE_TAP,
    GESTURE_LONG_PRESS,
};

inline const char* atechGestureName(AtechGesture g) {
    switch (g) {
        case GESTURE_TAP:        return "tap";
        case GESTURE_DOUBLE_TAP: return "double_tap";
        case GESTURE_TRIPLE_TAP: return "triple_tap";
        case GESTURE_LONG_PRESS: return "long_press";
        default:                 return "none";
    }
}

class AtechTapClassifier {
public:
    uint16_t multiTapMs = 400;
    uint16_t longPressMs = 700;
    uint16_t debounceMs = 15;

    /** A raw edge: the button went down (pressed=true) or up at time ms. */
    void edge(bool pressed, uint32_t ms) {
        if (pressed == _down) return;
        if (_hasEdge && (uint32_t)(ms - _lastEdgeMs) < debounceMs) return;
        _hasEdge = true;
        _lastEdgeMs = ms;
        _down = pressed;
        if (pressed) {
            _pressMs = ms;
            if (_taps < 255) _taps++;
        } else {
            _releaseMs = ms;
            if (_longFired) {
                _longFired = false;
                _taps = 0;
            }
        }
    }

    /**
     * Advance the clock. levelNow is the button's actual state right now.
     * Returns a gesture the moment one completes, else GESTURE_NONE.
     */
    AtechGesture tick(uint32_t now, bool levelNow) {
        if (levelNow != _down && (!_hasEdge || (uint32_t)(now - _lastEdgeMs) >= debounceMs)) {
            edge(levelNow, now);
        }
        if (_down && !_longFired && (uint32_t)(now - _pressMs) >= longPressMs) {
            _longFired = true;
            _taps = 0;
            return GESTURE_LONG_PRESS;
        }
        if (!_down && _taps > 0 && (uint32_t)(now - _releaseMs) >= multiTapMs) {
            uint8_t n = _taps;
            _taps = 0;
            if (n == 1) return GESTURE_TAP;
            if (n == 2) return GESTURE_DOUBLE_TAP;
            return GESTURE_TRIPLE_TAP;
        }
        return GESTURE_NONE;
    }

private:
    bool _down = false;
    bool _hasEdge = false;
    bool _longFired = false;
    uint8_t _taps = 0;
    uint32_t _lastEdgeMs = 0;
    uint32_t _pressMs = 0;
    uint32_t _releaseMs = 0;
};
