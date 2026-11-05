/**
 * @file atech_gestures.cpp
 * @brief The pin-change interrupt for AtechGestureInput (see atech_gestures.h).
 */

#include "atech_gestures.h"

void IRAM_ATTR AtechGestureInput::_isr(void* arg) {
    AtechGestureInput* self = (AtechGestureInput*)arg;
    int raw = digitalRead(self->_pin);
    bool pressed = self->_activeLow ? (raw == LOW) : (raw == HIGH);
    portENTER_CRITICAL_ISR(&self->_mux);
    uint8_t next = (self->_head + 1) % kEdges;
    if (next != self->_tail) {  // full: drop; poll() resyncs from the real level
        self->_edgeMs[self->_head] = millis();
        self->_edgeLevel[self->_head] = pressed;
        self->_head = next;
    }
    portEXIT_CRITICAL_ISR(&self->_mux);
}
