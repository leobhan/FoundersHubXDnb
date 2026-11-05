/**
 * @file atech_spi_hosts.h
 * @brief Shares the ESP32-S3's two hardware SPI hosts between SPI modules.
 *
 * The chip has two hardware SPI hosts, FSPI (the global SPI object) and HSPI.
 * More SPI modules than that still work, because a screen can bit-bang its bus
 * in software (slower pushes, same drawing). The microSD card and the camera
 * cannot: the SD library and the camera link both need a hardware host.
 *
 * So hardware hosts go to the modules that need them:
 *   - Generated setup() calls atech::reserveSpiHosts(n) before any begin(),
 *     where n is the number of microSD + camera modules in the project.
 *   - A screen takes a hardware host only if one is left over after those
 *     reservations; otherwise it runs software SPI.
 *   - The card and the camera claim a host in begin(), with a preference that
 *     is honoured when that host is free.
 *
 * With no reservation (hand-written sketches) the first two claimants get the
 * hosts in begin() order, which is what the screen drivers always did.
 *
 * One registry, not a counter per driver: when each display driver counted on
 * its own, a small screen plus a big screen both took FSPI and the second
 * stayed dark; and the screen, card and camera between them put two devices
 * on HSPI.
 */

#pragma once
#include <Arduino.h>
#include <SPI.h>

namespace atech {

struct SpiHostState {
    uint8_t used = 0;      // bit 0 = FSPI, bit 1 = HSPI
    uint8_t reserved = 0;  // hosts promised to modules that need hardware
};

inline SpiHostState& spiHostState() {
    // Function-local static in an inline function: one instance per program.
    static SpiHostState state;
    return state;
}

inline uint8_t spiHostBit(int host) { return host == FSPI ? 1 : 2; }

inline int freeSpiHosts() {
    uint8_t used = spiHostState().used;
    return !(used & 1) + !(used & 2);
}

/** Keep `count` hardware hosts for the microSD card / camera. Call before any begin(). */
inline void reserveSpiHosts(uint8_t count) {
    spiHostState().reserved = count > 2 ? 2 : count;
}

/**
 * Claim a hardware host for a module that cannot run without one (microSD,
 * camera). `preferred` (FSPI or HSPI) is used when free, otherwise the other
 * host. Returns the host, or -1 when both are taken.
 */
inline int claimSpiHost(int preferred) {
    SpiHostState& s = spiHostState();
    int order[2] = {preferred == HSPI ? HSPI : FSPI, preferred == HSPI ? FSPI : HSPI};
    for (int host : order) {
        if (!(s.used & spiHostBit(host))) {
            s.used |= spiHostBit(host);
            if (s.reserved) s.reserved--;
            return host;
        }
    }
    return -1;
}

/**
 * For a screen: begin a hardware host on these pins if one is free beyond the
 * reservations, else return nullptr and the driver bit-bangs.
 */
inline SPIClass* claimDisplaySpiHost(int sclkPin, int mosiPin, int csPin) {
    SpiHostState& s = spiHostState();
    if (freeSpiHosts() - s.reserved <= 0) return nullptr;
    static SPIClass hspi(HSPI);
    if (!(s.used & spiHostBit(FSPI))) {
        s.used |= spiHostBit(FSPI);
        SPI.begin(sclkPin, -1, mosiPin, csPin);
        return &SPI;
    }
    s.used |= spiHostBit(HSPI);
    hspi.begin(sclkPin, -1, mosiPin, csPin);
    return &hspi;
}

}  // namespace atech
