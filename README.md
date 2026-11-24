# Adam77

A 40×24 ASCII serial terminal for early 8-bit computers such as the KIM-1. A USB keyboard plugs into the Teensy 4.0's USB host port. Text appears on a 320×240 ILI9341 TFT.

Adam77 follows [Adam74](https://github.com/EngineersNeedArt/Adam74). Adam74 took parallel ASCII in; Adam77 talks serial and adds a keyboard.

## Layout

| Path | What |
| --- | --- |
| `src/main.cpp` | Firmware |
| `lib/TextBuffer/` | Text grid class, from Adam74 |
| `lib/Fonts/` | Liberation Mono Bold, from Adam74 |
| `hardware/` | KiCad project (to come) |
| `platformio.ini` | Build configuration |

## Building

```bash
pio run              # build
pio run -t upload    # build and flash (Teensy Loader opens)
pio device monitor   # USB debug console
```

## Design notes

- **Serial:** Uses the Teensy's hardware UART (`Serial1`, pins 0/1) at 1200 baud 8N2. Nothing is bit-banged. The KIM-1 measures the baud rate from the first RUBOUT after reset, so any standard rate works.
- **Line interface (target: PAL-2):** The PAL-2 KIM-1 replica has a 5 V TTL serial port, the same kind its bundled USB-to-TTL adapter plugs into. Its idle line is high, as on a standard UART, so no inversion is needed. The Teensy 4.0's pins are **not 5 V tolerant**, so the PAL-2's TX line must be shifted down before it reaches Teensy RX1, for example by the 74LVC245 that Adam74 used. The Teensy's 3.3 V TX output is normally above a 5 V TTL input's 2.0 V high threshold, so that direction can connect directly. Adam77 takes the place of the PAL-2's USB-to-TTL adapter and uses the same four wires: TXD, RXD, 5 V and GND.
- **Power (decided):** Adam77 powers the PAL-2 through that 5 V wire, the same way the adapter does. One 5 V USB supply feeds Adam77's power jack and becomes the board's 5 V rail. That rail supplies:
  - the Teensy's VIN pin
  - the USB host port, for the keyboard
  - the PAL-2, through a resettable polyfuse

  Cut the VUSB–VIN trace on the Teensy so that a programming cable can't feed power back into the rail.

  **Measured:** the PAL-2's peak draw was 476 mA, including its USB-to-TTL adapter. That was measured while it loaded a paper tape over serial and ran a game with LED animation.

  | Load | Typical | Worst case |
  | --- | --- | --- |
  | PAL-2 | 480 mA | 480 mA |
  | Teensy 4.0 (600 MHz) | 100 mA | 120 mA |
  | ILI9341 TFT and backlight | 80 mA | 100 mA |
  | USB keyboard | 20–100 mA | 500 mA (USB limit; backlit keyboards) |
  | **Total** | **~0.75 A** | **~1.2 A** |

  - **Supply:** use a USB-C 5 V supply rated at least 2 A; 3 A is preferred. The board's USB-C jack needs 5.1 kΩ pull-downs on CC1 and CC2, or USB-C chargers won't turn on their 5 V output.
  - **PAL-2 fuse:** a polyfuse that holds 1.1 A and trips at 2.2 A, such as the Bourns MF-MSMF110 (1812 size). The 476 mA peak is under half the hold rating. Its resistance drops only about 0.07 V at that current. A 0.75 A part would also work, but drops about twice as much voltage.
  - **Keyboard port:** the Teensy 4.0 only provides the USB host's D+ and D− pads, so the keyboard's 5 V comes from our rail. It needs its own 500 mA polyfuse, or a current-limit switch such as the TPS2051 or TPD3S014, plus a 100 µF capacitor near the jack to handle the surge when a keyboard is plugged in. The Teensy 4.1 already has this circuit for its host port.
- **PAL-1:** Not the same interface. The PAL-1 adds a small discrete-transistor stage after the KIM-1 current loop to produce RS-232 levels on a DB-9 wired as DCE (data communications equipment). Supporting it would need an RS-232 transceiver such as the MAX3232 as an option.
- **USB host:** On the Teensy 4.0 the host D+/D− are pads on the underside. On the Teensy 4.1 they come out on a 5-pin header.
- **Scrolling:** The ILI9341 scrolls in hardware only along its native 320-pixel axis, which is horizontal in landscape mode. That's why Adam74 couldn't scroll vertically. A RAM framebuffer with DMA updates (`ILI9341_t3n`) is the likely fix: 150 KB fits easily in the Teensy 4.0's RAM.
