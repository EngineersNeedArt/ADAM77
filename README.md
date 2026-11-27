# Adam77

A 40×24 ASCII serial terminal for early 8-bit computers such as the KIM-1. A USB keyboard plugs into the Teensy 4.1's USB host port. Text appears on a 320×240 ILI9341 TFT.

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

## Loading files from the SD card

Put files in the top folder of a microSD card (FAT16, FAT32 or exFAT) in the Teensy 4.1's own card socket.

1. At the KIM-1 monitor prompt, press **F2**. A list of the card's files appears.
2. Choose one with the arrow keys (Page Up/Down and Home/End also work), then press **Enter**. Press **Esc** to go back without sending.
3. Adam77 sends the file:
   - **Paper tapes** (files whose first character is `;`, in MOS Technology format): it types `L` first, putting the monitor in paper-tape load mode.
   - **Any other file** is sent as if typed, with CR line ends. Use this for BASIC listings and the like.
4. Each character waits for the KIM-1's echo before the next is sent, so the load runs as fast as the KIM-1 can take it. Press **Esc** to stop a send. A beep means it's finished.

## Design notes

- **Serial:** Uses the Teensy's hardware UART (`Serial1`, pins 0/1) at 1200 baud 8N2. Nothing is bit-banged. The KIM-1 measures the baud rate from the first RUBOUT after reset, so any standard rate works.
- **Line interface (target: PAL-2):** The PAL-2 KIM-1 replica has a 5 V TTL serial port, the same kind its bundled USB-to-TTL adapter plugs into. Its idle line is high, as on a standard UART, so no inversion is needed. The Teensy 4.1's pins are **not 5 V tolerant**, so the PAL-2's TX line must be shifted down before it reaches Teensy RX1, for example by the 74LVC245 that Adam74 used. The Teensy's 3.3 V TX output is normally above a 5 V TTL input's 2.0 V high threshold, so that direction can connect directly. Adam77 takes the place of the PAL-2's USB-to-TTL adapter and uses the same four wires: TXD, RXD, 5 V and GND.
- **Power (decided):** Adam77 powers the PAL-2 through that 5 V wire, the same way the adapter does. One 5 V USB supply feeds Adam77's power jack and becomes the board's 5 V rail. That rail supplies:
  - the Teensy's VIN pin
  - the USB host port, for the keyboard
  - the PAL-2, through a resettable polyfuse

  Cut the VUSB–VIN trace on the Teensy so that a programming cable can't feed power back into the rail.

  **Measured:** the PAL-2's peak draw was 476 mA, including its USB-to-TTL adapter. That was measured while it loaded a paper tape over serial and ran a game with LED animation.

  | Load | Typical | Worst case |
  | --- | --- | --- |
  | PAL-2 | 480 mA | 480 mA |
  | Teensy 4.1 (600 MHz) | 100 mA | 120 mA |
  | ILI9341 TFT and backlight | 80 mA | 100 mA |
  | USB keyboard | 20–100 mA | 500 mA (USB limit; backlit keyboards) |
  | **Total** | **~0.75 A** | **~1.2 A** |

  - **Supply:** use a USB-C 5 V supply rated at least 2 A; 3 A is preferred. The board's USB-C jack needs 5.1 kΩ pull-downs on CC1 and CC2, or USB-C chargers won't turn on their 5 V output.
  - **PAL-2 fuse:** a polyfuse that holds 1.1 A and trips at 2.2 A, such as the Bourns MF-MSMF110 (1812 size). The 476 mA peak is under half the hold rating. Its resistance drops only about 0.07 V at that current. A 0.75 A part would also work, but drops about twice as much voltage.
  - **Keyboard port:** the Teensy 4.1's USB host header has its own current-limit switch, powered from VIN, so the PCB only needs a USB-A jack wired to the header.
- **PAL-1:** Not the same interface. The PAL-1 adds a small discrete-transistor stage after the KIM-1 current loop to produce RS-232 levels on a DB-9 wired as DCE (data communications equipment). Supporting it would need an RS-232 transceiver such as the MAX3232 as an option.
- **Board (decided): Teensy 4.1.** It has the same 600 MHz chip as the 4.0. We chose it for two things: the USB host header, which has current limiting built in, and the microSD socket, which could hold paper-tape files for Adam77 to send to the PAL-2 with echo pacing.
- **Scrolling:** The ILI9341 scrolls in hardware only along its native 320-pixel axis, which is horizontal in landscape mode. That's why Adam74 couldn't scroll vertically. A RAM framebuffer with DMA updates (`ILI9341_t3n`) is the likely fix: 150 KB fits easily in the Teensy 4.1's RAM.

## Future ideas

None of these are decided yet.

- **Audio in.** A one-transistor amplifier and a small speaker, like the circuit in the KIM-1 manual, driven by a PAL-2 port pin. Then programs such as MUSICBOX from *The First Book of KIM* play without another board. The signal never touches a Teensy pin, so 5 V is fine. The Teensy's bell output could feed the same amplifier through its own resistor, so one speaker and one volume pot serve both.
- **DB-9 RS-232 port.** A MAX3232 transceiver and a DB-9 wired as DTE, like a PC's serial port, so a straight cable reaches DCE machines such as the PAL-1. Keep the TTL header for the PAL-2: the TTL header on `Serial1`, the DB-9 on `Serial2`, and a setting picks the active one.
- **Settings screen.** Instead of DIP switches: a key such as F1 opens a settings screen, with the choices saved in the Teensy's EEPROM. Settings: baud rate, data bits / parity / stop bits, which port, local echo, adding LF after CR, upper-case only, text color. Holding a key at power-up restores the defaults. Fuller support for other machines would eventually need a subset of cursor-control codes, such as VT52 or ADM-3A.
- **More from the SD card.** The basic loader works (see "Loading files from the SD card"). Still to do: folders, Intel HEX converted to MOS paper-tape format as it's sent, and capturing a tape the PAL-2 punches with `Q` into a new file.
