# baby-cry-mvp

Generated firmware for **Atech 14-Port Board** (`14port`).

## Modules

- **NeoPixel 3x3 Grid** (`neopixel`) as `greenLight` on `port_1`
- **ST7735 TFT Color Display (160x80)** (`st7735_tft`) as `screen` on `port_3, port_4`
- **Button** (`button`) as `button` on `port_5`
- **NeoPixel 3x3 Grid** (`neopixel`) as `redLight` on `port_6`
- **I2S MEMS Microphone (ICS-43434)** (`microphone`) as `mic` on `port_13, port_14`

## Build & flash

This folder is generated. Change `project.yaml` one level up and run, from there:

```bash
atech build .
atech upload .
```

It is also a plain PlatformIO project (`pio run`, `pio run -t upload`), but
anything edited here is overwritten by the next `atech build`.
