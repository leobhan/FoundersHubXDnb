/**
 * @file button.cpp
 * @brief Single Button Input Module Implementation
 */

#include "button.h"

ButtonModule::ButtonModule(int pin, int ledPin, bool activeLow)
    : _pin(pin)
    , _activeLow(activeLow)
    , _lastState(false)
    , _currentState(false)
    , _pressedFlag(false)
    , _releasedFlag(false)
    , _ledPin(ledPin)
    , _led(1, ledPin >= 0 ? ledPin : 0, NEO_GRB + NEO_KHZ800)
    , _r(255), _g(255), _b(255)  // default to white (matches V2.0's press color)
    , _lit(false)
    , _maxBrightness(DEFAULT_MAX_BRIGHTNESS)
{
}

void ButtonModule::begin() {
    pinMode(_pin, INPUT_PULLUP);
    _currentState = isPressed();
    _lastState = _currentState;
    _gestures.begin(_pin, _activeLow);
    Serial.print("[Button] Initialized on pin ");
    Serial.print(_pin);
    Serial.print(_activeLow ? " (active low)" : " (active high)");

    if (_ledPin >= 0) {
        _led.begin();
        _led.setBrightness(_maxBrightness);
        _led.clear();
        _led.show();
        Serial.print(", RGB light on pin ");
        Serial.print(_ledPin);
    }
    Serial.println();
}

bool ButtonModule::isPressed() {
    int raw = digitalRead(_pin);
    return _activeLow ? (raw == LOW) : (raw == HIGH);
}

bool ButtonModule::wasPressed() {
    update();
    if (_pressedFlag) {
        _pressedFlag = false;
        Serial.println("[Button] Pressed");
        return true;
    }
    return false;
}

bool ButtonModule::wasReleased() {
    update();
    if (_releasedFlag) {
        _releasedFlag = false;
        Serial.println("[Button] Released");
        return true;
    }
    return false;
}

void ButtonModule::update() {
    bool reading = isPressed();

    // Edge detection (no software debounce - hardware capacitor handles it)
    if (reading != _currentState) {
        _currentState = reading;
        if (_currentState) {
            _pressedFlag = true;  // Just pressed
        } else {
            _releasedFlag = true;  // Just released
        }
    }
}

int ButtonModule::getState() {
    return isPressed() ? 1 : 0;
}

// --- V2.0 RGB light ---------------------------------------------------------

void ButtonModule::applyLight() {
    if (_ledPin < 0) return;  // older button: no pixel, nothing to drive
    if (_lit) {
        _led.setPixelColor(0, _led.Color(_r, _g, _b));
    } else {
        _led.setPixelColor(0, 0);
    }
    _led.show();
}

void ButtonModule::setColor(uint8_t r, uint8_t g, uint8_t b) {
    _r = r;
    _g = g;
    _b = b;
    _lit = true;
    applyLight();
}

void ButtonModule::on() {
    _lit = true;
    applyLight();
}

void ButtonModule::off() {
    _lit = false;
    applyLight();
}

bool ButtonModule::isLit() const {
    return _lit;
}

void ButtonModule::setBrightness(uint8_t brightness) {
    if (_ledPin < 0) return;
    uint8_t clamped = brightness > _maxBrightness ? _maxBrightness : brightness;
    _led.setBrightness(clamped);
    applyLight();
}

void ButtonModule::setBrightnessCap(uint8_t cap) {
    _maxBrightness = cap;
    if (_ledPin < 0) return;
    // Pull the current brightness down if it now sits above the new ceiling.
    if (_led.getBrightness() > _maxBrightness) {
        _led.setBrightness(_maxBrightness);
        applyLight();
    }
}

// --- Gestures --------------------------------------------------------------

AtechGesture ButtonModule::getGesture() {
    AtechGesture g = _gestures.poll();
    if (g != GESTURE_NONE) {
        Serial.print("[Button] ");
        Serial.println(atechGestureName(g));
    }
    return g;
}

void ButtonModule::setGestureTiming(uint16_t multiTapMs, uint16_t longPressMs) {
    _gestures.setTiming(multiTapMs, longPressMs);
}
