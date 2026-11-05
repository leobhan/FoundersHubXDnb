/**
 * @file atech_soft_spi.h
 * @brief Fast bit-banged SPI writes for screens without a hardware host.
 *
 * A screen that finds no hardware SPI host free (see atech_spi_hosts.h) runs
 * Adafruit's software SPI, which toggles every bit with digitalWrite(): a full
 * 320x240 frame takes over a second. This writes the frame's pixel bytes
 * through the GPIO set/clear registers instead, many times faster, while
 * Adafruit still sends the few command bytes around it.
 *
 * SPI mode 0, MSB first: data is set while the clock is low and sampled on the
 * rising edge. The caller holds CS low and DC high (startWrite() +
 * setAddrWindow()) around the call.
 */

#pragma once
#include <Arduino.h>
#include "soc/gpio_struct.h"

namespace atech {

struct SoftSpiPins {
    volatile uint32_t* sclkSet;
    volatile uint32_t* sclkClr;
    volatile uint32_t* mosiSet;
    volatile uint32_t* mosiClr;
    uint32_t sclkMask;
    uint32_t mosiMask;

    SoftSpiPins(int sclk, int mosi) {
        sclkSet = sclk < 32 ? &GPIO.out_w1ts : &GPIO.out1_w1ts.val;
        sclkClr = sclk < 32 ? &GPIO.out_w1tc : &GPIO.out1_w1tc.val;
        mosiSet = mosi < 32 ? &GPIO.out_w1ts : &GPIO.out1_w1ts.val;
        mosiClr = mosi < 32 ? &GPIO.out_w1tc : &GPIO.out1_w1tc.val;
        sclkMask = 1UL << (sclk & 31);
        mosiMask = 1UL << (mosi & 31);
    }

    inline void writeByte(uint8_t b) const {
        for (uint8_t bit = 0x80; bit; bit >>= 1) {
            if (b & bit) *mosiSet = mosiMask; else *mosiClr = mosiMask;
            *sclkSet = sclkMask;
            *sclkClr = sclkMask;
        }
    }
};

/** Write RGB565 pixels high byte first, as the ST77xx panels expect. */
inline void softSpiWritePixels(int sclkPin, int mosiPin, const uint16_t* pixels, size_t count) {
    SoftSpiPins pins(sclkPin, mosiPin);
    *pins.sclkClr = pins.sclkMask;
    for (size_t i = 0; i < count; i++) {
        uint16_t p = pixels[i];
        pins.writeByte(p >> 8);
        pins.writeByte(p & 0xFF);
    }
}

}  // namespace atech
