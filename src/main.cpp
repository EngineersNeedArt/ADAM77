// ==============================================================================
// Adam77 (Teensy 4.0)
// A 40x24 ASCII terminal for early 8-bit computers such as the KIM-1.
//
//   USB keyboard (USB host port) --> Serial1 TX --> KIM-1 TTY input
//   KIM-1 TTY output --> Serial1 RX --> 40x24 text on an ILI9341 320x240 TFT
//
// The display code descends from Adam74 (github.com/EngineersNeedArt/Adam74).
// ==============================================================================

#include <Arduino.h>
#include <ILI9341_t3.h>
#include <USBHost_t36.h>
#include "TextBuffer.h"
#include "font_LiberationMonoBold.h"

// --- Pins (TFT and piezo match Adam74) -----------------------------------------
// Serial1 uses pin 0 (RX1) and pin 1 (TX1).
#define TFT_DC                  9
#define TFT_CS                  10
#define PIEZO                   19

// --- Serial line to the host computer ---------------------------------------
// The KIM-1 measures the baud rate from the first RUBOUT it receives, so any
// standard rate works. The Teensy UART does the timing in hardware: no bit-banging.
#define HOST_BAUD               1200
#define HOST_FORMAT             SERIAL_8N2      // Two stop bits suit older machines.
#define FORCE_UPPERCASE         true            // KIM-1 monitor expects upper case.

// --- Text grid ------------------------------------------------------------------
#define NUM_COLUMNS             40
#define NUM_ROWS                24
#define X_ADVANCE               8
#define Y_ADVANCE               10
#define CHAR_X_MARGIN           1
#define CHAR_Y_MARGIN           1
#define CURSOR_FLASH_INTERVAL   500

#define ASCII_BEL               0x07
#define ASCII_BS                0x08
#define ASCII_LF                0x0A
#define ASCII_FF                0x0C
#define ASCII_CR                0x0D
#define ASCII_RUBOUT            0x7F

ILI9341_t3 tft = ILI9341_t3 (TFT_CS, TFT_DC);
TextBuffer text = TextBuffer ();

USBHost usb_host;
USBHub usb_hub (usb_host);
KeyboardController keyboard (usb_host);

uint16_t foreground_color = ILI9341_GREEN;
uint16_t background_color = ILI9341_BLACK;
int16_t cursor_x = 0;
int16_t cursor_y = 0;
bool cursor_visible = false;
elapsedMillis cursor_timer;

// ------------------------------------------------------------------------------
// Display
// ------------------------------------------------------------------------------

void draw_cell (int16_t col, int16_t row, bool inverted) {
    char character = text.getCharRowColumn (row, col) & 0x7F;
    int16_t x = col * X_ADVANCE;
    int16_t y = row * Y_ADVANCE;
    tft.fillRect (x, y, X_ADVANCE, Y_ADVANCE, inverted ? foreground_color : background_color);
    if (character > ' ') {
        tft.setCursor (x + CHAR_X_MARGIN, y + CHAR_Y_MARGIN);
        tft.setTextColor (inverted ? background_color : foreground_color);
        tft.drawFontChar (character);
    }
}

void refresh_display () {
    for (int16_t r = 0; r < NUM_ROWS; r++) {
        for (int16_t c = 0; c < NUM_COLUMNS; c++) {
            draw_cell (c, r, false);
        }
    }
}

void hide_cursor () {
    if (cursor_visible) {
        draw_cell (cursor_x, cursor_y, false);
        cursor_visible = false;
    }
}

void flash_cursor () {
    cursor_visible = !cursor_visible;
    draw_cell (cursor_x, cursor_y, cursor_visible);
    cursor_timer = 0;
}

void line_feed () {
    cursor_y++;
    if (cursor_y >= NUM_ROWS) {
        cursor_y = NUM_ROWS - 1;
        text.scrollUp ();
        refresh_display ();     // TODO: faster scrolling (see README).
    }
}

void beep () {
    tone (PIEZO, 1760, 60);
}

void handle_host_char (char c) {
    c &= 0x7F;      // The KIM-1 does not drive bit 7 meaningfully.
    hide_cursor ();
    switch (c) {
        case ASCII_CR:  cursor_x = 0;                       break;
        case ASCII_LF:  line_feed ();                       break;
        case ASCII_BS:  if (cursor_x > 0) cursor_x--;       break;
        case ASCII_BEL: beep ();                            break;
        case ASCII_FF:
            text.clear ();
            tft.fillScreen (background_color);
            cursor_x = cursor_y = 0;
            break;
        default:
            if (c >= ' ' && c < ASCII_RUBOUT) {
                text.setCharRowColumn (c, cursor_y, cursor_x);
                draw_cell (cursor_x, cursor_y, false);
                cursor_x++;
                if (cursor_x >= NUM_COLUMNS) {
                    cursor_x = 0;
                    line_feed ();
                }
            }
            break;
    }
    flash_cursor ();    // Show the cursor at its new spot right away.
}

// ------------------------------------------------------------------------------
// Keyboard
// ------------------------------------------------------------------------------

void on_key_press (int key) {
    // USBHost_t36 hands over ASCII for ordinary keys (with Ctrl applied).
    if (key <= 0 || key > 0x7F) {
        return;     // Function, arrow keys, etc.: not mapped yet.
    }
    char c = (char) key;
    if (FORCE_UPPERCASE && c >= 'a' && c <= 'z') {
        c -= 'a' - 'A';
    }
    if (c == ASCII_LF) {
        c = ASCII_CR;   // Enter key.
    }
    Serial1.write (c);  // No local echo: the KIM-1 echoes what it receives.
}

// ------------------------------------------------------------------------------

void setup () {
    Serial.begin (115200);      // USB debug console.
    Serial1.begin (HOST_BAUD, HOST_FORMAT);
    pinMode (PIEZO, OUTPUT);

    tft.begin ();
    tft.setRotation (3);
    tft.setFont (LiberationMono_8_Bold);
    tft.fillScreen (background_color);
    text.begin (NUM_ROWS, NUM_COLUMNS);

    usb_host.begin ();
    keyboard.attachPress (on_key_press);

    const char *banner = "ADAM77 READY\r\n";
    while (*banner) {
        handle_host_char (*banner++);
    }
    beep ();
}

void loop () {
    usb_host.Task ();

    while (Serial1.available ()) {
        handle_host_char ((char) Serial1.read ());
    }

    if (cursor_timer >= CURSOR_FLASH_INTERVAL) {
        flash_cursor ();
    }
}
