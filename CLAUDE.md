# Adam77

Teensy 4.1 serial terminal (USB keyboard in, 40×24 text on an ILI9341 TFT) for the PAL-2 KIM-1 replica. See [README.md](README.md) for the design and [docs/breadboard.md](docs/breadboard.md) for wiring and the bring-up order.

## Build and run

```bash
pio run              # build
pio run -t upload    # build and flash; Teensy Loader opens
pio device monitor   # USB debug console, 115200
```

- PlatformIO is installed with Homebrew (`brew install platformio`). ILI9341_t3 and USBHost_t36 come with the Teensy framework, so `platformio.ini` has no `lib_deps`.
- **An open `pio device monitor` blocks uploads** (`[upload] Error -1`). Quit it with Ctrl+C first.
- **The monitor may pick the wrong port.** The PAL-2's USB-to-TTL adapter shows up as `/dev/cu.usbserial-…`, and other devices may show as `usbmodem`. Pass the Teensy's port explicitly: `pio device monitor -p /dev/cu.usbmodem…` (find it with `pio device list`).
- `pio device monitor` needs an interactive terminal. To capture console output from a script, run `cat` on the port in the background right after an upload, then stop it. At startup, the firmware waits up to 2 seconds for a console, then prints its setup steps and any `CrashReport`.
- If the Teensy keeps disconnecting and reconnecting, even in its bootloader, suspect power or wiring, not firmware.

## Hardware safety

- **Teensy 4.1 pins are not 5 V tolerant.** The PAL-2's serial output must go through a level shifter (divider or 74LVC245) before it reaches pin 0.
- **The PAL-2 header is labeled from the adapter's point of view.** Its **RXD** pin is the PAL-2's *output* (idle about 4.2 V) and goes to Teensy pin 0 through the shifter. Its **TXD** pin is the PAL-2's input and goes straight to pin 1.
- On the breadboard, the PAL-2 keeps its own power from its adapter (5 V and GND wires only). Connect only GND, TX and RX to Adam77, and never join the two boards' 5 V lines. Disconnect the adapter's TXD/RXD wires so they don't drive the same lines as the Teensy.
- **The Teensy's GND must be connected to the breadboard ground rail.** Without it the TFT backlight still lights dimly, but the display ignores every command.

## Code notes

- **ILI9341_t3 on Teensy 4: `tft.readcommand8()` corrupts the next command sent** when D/C is on pin 9 (not a hardware CS pin). A `setRotation()` after a register read silently didn't apply. If you need register reads for diagnosis, do them after all setup writes and remove them afterward.
- Rotation 3 is landscape (320×240). The ILI9341 can only scroll in hardware along its 320-pixel axis, so vertical scrolling redraws the screen (see README).
- `lib/TextBuffer` and `lib/Fonts` come from [Adam74](https://github.com/EngineersNeedArt/Adam74); keep changes to them minimal.

## Style

- Match `src/main.cpp`: 4-space indents, `snake_case` functions and variables, a space before the parentheses in calls and declarations (`draw_cell (c, r, false)`), and aligned `#define` constants in ALL_CAPS.
- Keep hardware decisions and measurements in README.md, and wiring in docs/breadboard.md.
