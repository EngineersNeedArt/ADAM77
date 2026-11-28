# Adam77

A 40×24 ASCII serial terminal for early 8-bit computers such as the KIM-1. A USB keyboard plugs into the Teensy 4.1's USB host port. Text appears on a 320×240 ILI9341 TFT.

Adam77 follows [Adam74](https://github.com/EngineersNeedArt/Adam74). Adam74 took parallel ASCII in; Adam77 talks serial and adds a keyboard.

## Layout

| Path | What |
| --- | --- |
| `src/main.cpp` | Firmware |
| `lib/TextBuffer/` | Text grid class, from Adam74 |
| `lib/Fonts/` | Liberation Mono Bold, from Adam74 |
| `hardware/` | KiCad project (rough schematic; see below) |
| `platformio.ini` | Build configuration |

## Building

```bash
pio run              # build
pio run -t upload    # build and flash (Teensy Loader opens)
pio device monitor   # USB debug console
```

## Hardware (KiCad)

`hardware/ADAM77.kicad_pro` opens in KiCad 10. It's a rough first pass, generated rather than drawn by hand, so expect to tidy the layout.

- **The schematic** covers USB-C 5 V input (with CC pull-downs), the Teensy 4.1, the PAL-2 TTL header (74HC4050 on the PAL-2's output, polyfuse on its 5 V), the TFT header, the USB host jack and the LM386 audio section.
- **ERC:** no errors or warnings.
- **Through-hole parts** everywhere except the USB-C jack, whose six power-only pads are large enough to hand-solder. The level shifter is a **74HC4050** (DIP-16) instead of the 74LVC245, which isn't made in DIP. Powered at 3.3 V, its inputs accept 5 V and its outputs are 3.3 V. The polyfuse is the radial Bourns MF-RG1100.
- **The Teensy 4.1** symbol and footprint are in the project's own library (`ADAM77.kicad_sym`, `ADAM77.pretty`). The footprint covers the 48 outer pins only. The USB host pins come out on J4, a 5-pin header for a short cable from the Teensy, since their exact position on the Teensy isn't modeled.
- **To start the PCB:** open the PCB editor and choose Tools → Update PCB from Schematic (F8) to pull in all the footprints.
- **J2 (PAL-2):** 1 GND, 2 +5V, 3 `TTL (IN) KYBD`, 4 `TTL (OUT) PTR`. The PAL-2 has no standard cable, so make a 4-wire one to match.
- **J4 (USB host):** 5V, D−, D+, GND, GND, the same order as the Teensy 4.1's host pins.

## Loading files from the SD card

Put files in the top folder of a microSD card (FAT16, FAT32 or exFAT) in the Teensy 4.1's own card socket.

1. At the KIM-1 monitor prompt, press **F2**. A list of the card's files appears. Known types show their name without the extension, plus a type label. Files that are sent (TAPE, BASIC) are green; text files, which are only read, are dark green. The banners, the selection bar and the startup message are white.
2. Choose one with the arrow keys (Page Up/Down and Home/End also work), then press **Enter**. Press **Esc** to go back.
3. The file's extension decides what happens:

   | Extension | Type | What Enter does |
   | --- | --- | --- |
   | `.pap`, `.ptp` | TAPE | Sends it as a paper tape (MOS Technology format): types `L` first, putting the monitor in paper-tape load mode, then sends the tape with CR LF line ends. |
   | `.bas` | BASIC | Sends it as if typed, with CR line ends. |
   | `.txt` | TEXT | Shows it on the screen only; nothing goes to the KIM-1. |
   | anything else | | Sent as a paper tape if its first character is `;`, otherwise as if typed. |

4. Sending waits for the KIM-1's echo of each character before sending the next, so it runs as fast as the KIM-1 can take it. Press **Esc** to stop a send. A beep means it's finished.
5. A text file is word-wrapped to 40 columns. Scroll with the arrow keys, **Space** or Page Up/Down, and Home/End. **Esc** goes back to the file list; **F2** goes straight back to the terminal. Files over 64 KB are cut off.

A text file is a handy place for a program's instructions: put `slots.txt` (for example, "0200 G to start") next to `slots.pap`.

## Design notes

- **Serial:** Uses the Teensy's hardware UART (`Serial1`, pins 0/1) at 1200 baud 8N2. Nothing is bit-banged. The KIM-1 measures the baud rate from the first RUBOUT after reset, so any standard rate works.
- **Line interface (target: PAL-2):** The PAL-2 KIM-1 replica has a 5 V TTL serial port, the same kind its bundled USB-to-TTL adapter plugs into. Its idle line is high, as on a standard UART, so no inversion is needed. The Teensy 4.1's pins are **not 5 V tolerant**, so the PAL-2's TX line must be shifted down before it reaches Teensy RX1, for example by a 74HC4050 (DIP) or the 74LVC245 that Adam74 used. The Teensy's 3.3 V TX output is normally above a 5 V TTL input's 2.0 V high threshold, so that direction can connect directly. Adam77 takes the place of the PAL-2's USB-to-TTL adapter and uses the same four wires: TXD, RXD, 5 V and GND.
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
  - **PAL-2 fuse:** a polyfuse that holds 1.1 A and trips at 2.2 A, such as the Bourns MF-RG1100 (radial, through-hole) or MF-MSMF110 (1812 surface mount). The 476 mA peak is under half the hold rating. Its resistance drops only about 0.07 V at that current. A 0.75 A part would also work, but drops about twice as much voltage.
  - **Keyboard port:** the Teensy 4.1's USB host header has its own current-limit switch, powered from VIN, so the PCB only needs a USB-A jack wired to the header.
- **PAL-1:** Not the same interface. The PAL-1 adds a small discrete-transistor stage after the KIM-1 current loop to produce RS-232 levels on a DB-9 wired as DCE (data communications equipment). Supporting it would need an RS-232 transceiver such as the MAX3232 as an option.
- **Board (decided): Teensy 4.1.** It has the same 600 MHz chip as the 4.0. We chose it for two things: the USB host header, which has current limiting built in, and the microSD socket, which could hold paper-tape files for Adam77 to send to the PAL-2 with echo pacing.
- **Scrolling:** The ILI9341 scrolls in hardware only along its native 320-pixel axis, which is horizontal in landscape mode. That's why Adam74 couldn't scroll vertically. A RAM framebuffer with DMA updates (`ILI9341_t3n`) is the likely fix: 150 KB fits easily in the Teensy 4.1's RAM.

## Future ideas

None of these are decided yet.

- **Audio in: LM386 mixer and speaker.** A PAL-2 port pin and the Teensy's bell pin share one speaker and one volume pot. Then programs such as MUSICBOX from *The First Book of KIM* play without another board. The LM386 is an 8-pin audio amplifier that runs from 5 V and drives an 8 Ω speaker at about ½ W; a plain op-amp can't drive a speaker directly.

  ```
  Teensy pin 19 ──1 µF──47 kΩ──┐
                               ├──── pot top (10 kΩ log)
  PAL-2 port pin ─1 µF──47 kΩ──┘     pot wiper ──→ LM386 IN+ (pin 3)
                                     pot bottom ──→ GND
                                                   LM386 IN− (pin 2) → GND

  LM386 OUT (pin 5) ──220 µF (+ toward LM386)──→ speaker (8 Ω) ──→ GND
                    └─10 Ω──0.05 µF──→ GND       (keeps it from oscillating)
  LM386 pin 6 → 5 V, with 100 µF + 0.1 µF to GND close to the chip
  LM386 pin 7 → 10 µF to GND                     (cuts supply noise)
  LM386 pin 4 → GND
  ```

  - The 1 µF capacitors block DC, so it doesn't matter whether a pin idles high or low.
  - The 47 kΩ resistors mix the two sources, keep their outputs from fighting, and cut the full-volume square waves down for the LM386's gain of 20. Increase them if it's too loud at the bottom of the pot.
  - The PAL-2's 5 V signal meets only resistors and capacitors, never a Teensy pin. Its ground comes through the TTL cable.
  - The speaker replaces the piezo. Pin 19 and the firmware stay the same.
  - If digital noise from the shared 5 V rail comes through as a buzz, filter the LM386's supply: 10 Ω in series and another 100 µF to GND.
- **DB-9 RS-232 port.** A MAX3232 transceiver and a DB-9 wired as DTE, like a PC's serial port, so a straight cable reaches DCE machines such as the PAL-1. Keep the TTL header for the PAL-2: the TTL header on `Serial1`, the DB-9 on `Serial2`, and a setting picks the active one.
- **Settings screen.** Instead of DIP switches: a key such as F1 opens a settings screen, with the choices saved in the Teensy's EEPROM. Settings: baud rate, data bits / parity / stop bits, which port, local echo, adding LF after CR, upper-case only, text color. Holding a key at power-up restores the defaults. Fuller support for other machines would eventually need a subset of cursor-control codes, such as VT52 or ADM-3A.
- **More from the SD card.** The basic loader works (see "Loading files from the SD card"). Still to do: folders, Intel HEX converted to MOS paper-tape format as it's sent, and capturing a tape the PAL-2 punches with `Q` into a new file.
