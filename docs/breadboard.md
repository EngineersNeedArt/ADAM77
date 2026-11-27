# Adam77 breadboard bring-up (Teensy 4.1)

There's no polyfuse on the breadboard. Each board keeps its own power, and only **GND, TX and RX** connect between Adam77 and the PAL-2.

## Power

- **Teensy:** power it from its micro-USB cable to the Mac, the same cable used for programming. Leave the Teensy's VUSB–VIN trace intact. **VIN** is then the 5 V rail, and **3.3V** is the Teensy's regulator output.
- **PAL-2:** keep powering it from its USB-to-TTL adapter, but use only the adapter's 5 V and GND wires. Its TXD and RXD wires come off the PAL-2 and go to the Teensy instead (see below).
- **Never connect the two boards' 5 V lines together.** Join their grounds only.
- **Connect a Teensy GND pin to the breadboard's ground rail.** Without it, the TFT's backlight still lights (dimly) through the signal lines, but the display ignores every command and the screen stays blank.

## ILI9341 TFT (SPI module)

|  TFT pin  | Teensy 4.1 |
|-----------|------------|
|    VCC    | VIN(5V)[1] |
|    GND    |    GND     |
|    CS     |     10     |
|   RESET   |    3.3V    |
|    D/C    |     9      |
| SDI(MOSI) |     11     |
|    SCK    |     13     |
|    LED    |   VIN [2]  |
| SDO(MISO) |  12 (opt.) |

[1] If the module has a 3.3 V regulator, as most red 2.8" boards do. Otherwise use 3.3V.
[2] Through a 100 Ω resistor.

## Serial to the PAL-2

Adam77 takes the adapter's place: each Teensy pin goes where the matching adapter wire went.

| PAL-2 Application Connector pin | Teensy 4.1 |
| --- | --- |
| **`TTL (IN) KYBD`**, the PAL-2's input (the adapter's TXD wire went here) | **pin 1 (TX1)**, connected directly. 3.3 V is enough for a 5 V TTL input. |
| **`TTL (OUT) PTR`**, the PAL-2's output (the adapter's RXD wire went here) | **pin 0 (RX1)**, through the level shifter below. |
| GND | GND |

**Check first:** measure the PAL-2's TX line with a multimeter while it's idle. It idles high. On the PAL-2's Application Connector, this is **`TTL (OUT) PTR`**. Ours measured **4.18 V**.
- About **3.3 V**: wire it straight to pin 0.
- Above that: it must be shifted down. The Teensy 4.1 is **not** 5 V tolerant. Use either:
  - **Resistor divider:** PAL-2 TX → 10 kΩ → pin 0, then pin 0 → 20 kΩ → GND (two 10 kΩ in series). That gives about 2.8 V from 4.18 V, and still only 3.3 V if the line ever reaches a full 5 V. It's plenty fast for 1200 baud. (A 10 kΩ / 15 kΩ divider gives only about 2.5 V from 4.18 V, too close to the Teensy's 2.3 V minimum for a reliable high.)
  - **74LVC245, as on Adam74:**
    - Power: VCC (pin 20) to 3.3V, GND (pin 10) to GND
    - Direction: DIR (pin 1) to 3.3V, so data flows A → B
    - Enable: OE (pin 19) to GND
    - Signal: PAL-2 TX into A1 (pin 2), and B1 (pin 18) out to Teensy pin 0
    - Unused A inputs (pins 3–9): tie to GND

## Piezo

Teensy **pin 19** → 100 Ω → piezo → GND.

## USB keyboard

Plug the keyboard into the Teensy 4.1's 5-pin USB host header. The simplest way is PJRC's USB host cable, which runs from that header to a USB-A jack. The header has its own current limiting, powered from VIN, so no extra fuse or capacitor is needed.

You can test everything before the keyboard is wired: `pio device monitor` forwards whatever you type to the PAL-2 as if it came from the keyboard.

## Bring-up order

1. **Display only:** `pio run -t upload`. Expect a beep, "ADAM77 READY" in green, and a blinking cursor.
2. **Serial loopback:** jumper pin 1 to pin 0, run `pio device monitor`, and type. The characters should appear on the TFT, in upper case. Then remove the jumper.
3. **PAL-2:** connect it as described above and reset it.
   - Send a RUBOUT so the PAL-2 can set its baud rate. With a keyboard, press **Delete**. In the monitor, the Delete key may send 0x7F; if not, look for a send-control-character option in its menu.
   - The KIM monitor prompt should appear on the TFT.
4. **Keyboard:** plug it into the host cable.
