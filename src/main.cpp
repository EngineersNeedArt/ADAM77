// ==============================================================================
// Adam77 (Teensy 4.1)
// A 40x24 ASCII terminal for early 8-bit computers such as the KIM-1.
//
//   USB keyboard (USB host port) --> Serial1 TX --> KIM-1 TTY input
//   KIM-1 TTY output --> Serial1 RX --> 40x24 text on an ILI9341 320x240 TFT
//   F2: pick a file on the microSD card and send it to the KIM-1 (paper tapes)
//
// The display code descends from Adam74 (github.com/EngineersNeedArt/Adam74).
// ==============================================================================

#include <Arduino.h>
#include <ILI9341_t3.h>
#include <USBHost_t36.h>
#include <SD.h>
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
#define ASCII_ESC               0x1B
#define ASCII_RUBOUT            0x7F

// --- Paper tapes from the microSD card ---------------------------------------
#define FILE_MENU_KEY           KEYD_F2
#define MAX_TAPE_FILES          128
#define MAX_TAPE_NAME           64
#define MENU_FIRST_ROW          2       // Rows 0-1: title. Rows 2-21: files. Row 23: help.
#define MENU_LIST_ROWS          20
#define ECHO_TIMEOUT_MS         50      // A character takes about 9 ms at 1200 baud 8N2.

ILI9341_t3 tft = ILI9341_t3 (TFT_CS, TFT_DC);
TextBuffer text = TextBuffer ();

USBHost usb_host;
USBHub usb_hub (usb_host);
KeyboardController keyboard (usb_host);
// Many keyboards also expose extra HID interfaces (media keys, etc.) that need parsers.
USBHIDParser hid1 (usb_host);
USBHIDParser hid2 (usb_host);
USBHIDParser hid3 (usb_host);

uint16_t foreground_color = ILI9341_GREEN;
uint16_t background_color = ILI9341_BLACK;
int16_t cursor_x = 0;
int16_t cursor_y = 0;
bool cursor_visible = false;
elapsedMillis cursor_timer;

enum Mode { MODE_TERMINAL, MODE_FILE_MENU, MODE_SENDING };
Mode mode = MODE_TERMINAL;

char tape_names[MAX_TAPE_FILES][MAX_TAPE_NAME];
int16_t tape_count = 0;
int16_t menu_selection = 0;
int16_t menu_top = 0;
const char *menu_message = NULL;    // Shown instead of the list (no card, no files).
int16_t pending_send = -1;          // Index of the file to send, set by the menu.
bool cancel_send = false;

// ------------------------------------------------------------------------------
// Display
// ------------------------------------------------------------------------------

void draw_char_at (int16_t col, int16_t row, char character, bool inverted) {
    int16_t x = col * X_ADVANCE;
    int16_t y = row * Y_ADVANCE;
    tft.fillRect (x, y, X_ADVANCE, Y_ADVANCE, inverted ? foreground_color : background_color);
    if (character > ' ') {
        tft.setCursor (x + CHAR_X_MARGIN, y + CHAR_Y_MARGIN);
        tft.setTextColor (inverted ? background_color : foreground_color);
        tft.drawFontChar (character);
    }
}

void draw_cell (int16_t col, int16_t row, bool inverted) {
    draw_char_at (col, row, text.getCharRowColumn (row, col) & 0x7F, inverted);
}

// Draws 'string' starting at 'col', padded with spaces (or cut off) to 'width' cells.
void draw_string_at (int16_t col, int16_t row, const char *string, bool inverted, int16_t width) {
    for (int16_t i = 0; i < width && col + i < NUM_COLUMNS; i++) {
        char character = *string ? *string++ : ' ';
        draw_char_at (col + i, row, character, inverted);
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

void restore_terminal () {
    refresh_display ();
    cursor_visible = false;
    flash_cursor ();
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
    if (key == KEYD_DELETE) {
        key = ASCII_RUBOUT;     // The KIM-1 sets its baud rate from a RUBOUT after reset.
    }
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
// File menu (F2): paper tapes and text files on the microSD card
// ------------------------------------------------------------------------------

int compare_names (const void *a, const void *b) {
    return strcasecmp ((const char *) a, (const char *) b);
}

// Fills tape_names with the files in the card's top folder, sorted by name.
void read_tape_directory () {
    tape_count = 0;
    menu_message = NULL;
    if (!SD.begin (BUILTIN_SDCARD)) {      // Called each time, so a card can be swapped.
        menu_message = "NO SD CARD";
        return;
    }
    File root = SD.open ("/");
    while (tape_count < MAX_TAPE_FILES) {
        File entry = root.openNextFile ();
        if (!entry) {
            break;
        }
        // Skip folders, and hidden files such as the "._" files macOS leaves behind.
        if (!entry.isDirectory () && entry.name ()[0] != '.') {
            strlcpy (tape_names[tape_count], entry.name (), MAX_TAPE_NAME);
            tape_count++;
        }
        entry.close ();
    }
    root.close ();
    qsort (tape_names, tape_count, MAX_TAPE_NAME, compare_names);
    if (tape_count == 0) {
        menu_message = "NO FILES ON SD CARD";
    }
}

void draw_file_menu () {
    draw_string_at (0, 0, " SEND FILE FROM SD CARD", true, NUM_COLUMNS);
    draw_string_at (0, 1, "", false, NUM_COLUMNS);
    for (int16_t i = 0; i < MENU_LIST_ROWS; i++) {
        int16_t row = MENU_FIRST_ROW + i;
        int16_t index = menu_top + i;
        if (menu_message && i == 0) {
            draw_string_at (1, row, menu_message, false, NUM_COLUMNS - 1);
        } else if (!menu_message && index < tape_count) {
            draw_string_at (0, row, "", false, 1);
            draw_string_at (1, row, tape_names[index], index == menu_selection, NUM_COLUMNS - 2);
            draw_string_at (NUM_COLUMNS - 1, row, "", false, 1);
        } else {
            draw_string_at (0, row, "", false, NUM_COLUMNS);
        }
    }
    draw_string_at (0, NUM_ROWS - 2, "", false, NUM_COLUMNS);
    draw_string_at (0, NUM_ROWS - 1, menu_message ? " ESC: BACK" : " ENTER: SEND   ESC: BACK", true, NUM_COLUMNS);
}

void open_file_menu () {
    hide_cursor ();
    mode = MODE_FILE_MENU;
    read_tape_directory ();
    menu_selection = 0;
    menu_top = 0;
    draw_file_menu ();
}

void close_file_menu () {
    mode = MODE_TERMINAL;
    restore_terminal ();
}

void file_menu_key (int key) {
    if (key == ASCII_ESC || key == FILE_MENU_KEY) {
        close_file_menu ();
        return;
    }
    if (menu_message) {
        return;     // Nothing to choose.
    }
    int16_t selection = menu_selection;
    switch (key) {
        case KEYD_UP:           selection--;                    break;
        case KEYD_DOWN:         selection++;                    break;
        case KEYD_PAGE_UP:      selection -= MENU_LIST_ROWS;    break;
        case KEYD_PAGE_DOWN:    selection += MENU_LIST_ROWS;    break;
        case KEYD_HOME:         selection = 0;                  break;
        case KEYD_END:          selection = tape_count - 1;     break;
        case ASCII_LF:
        case ASCII_CR:
            pending_send = menu_selection;      // loop () sends it once the menu is closed.
            close_file_menu ();
            return;
        default:
            return;
    }
    menu_selection = constrain (selection, 0, tape_count - 1);
    if (menu_selection < menu_top) {
        menu_top = menu_selection;
    } else if (menu_selection >= menu_top + MENU_LIST_ROWS) {
        menu_top = menu_selection - MENU_LIST_ROWS + 1;
    }
    draw_file_menu ();
}

// Sends one character, then waits for the KIM-1 to echo it before going on.
// The KIM-1 receives in software, so sending faster than it echoes loses characters.
void send_paced (char c) {
    Serial1.write (c);
    elapsedMillis waited;
    while (waited < ECHO_TIMEOUT_MS) {
        usb_host.Task ();       // Keeps Esc working during a send.
        if (Serial1.available ()) {
            handle_host_char ((char) Serial1.read ());
            return;
        }
    }
}

// Paper-tape files (first character ';') are sent after an "L", which puts the KIM-1
// monitor in paper-tape load mode, with CR LF line ends. Any other file is sent as if
// typed, with CR line ends (BASIC listings and the like).
void send_file (const char *name) {
    File file = SD.open (name);
    if (!file) {
        beep ();
        return;
    }
    int first = file.peek ();
    while (first == ' ' || first == '\t' || first == ASCII_CR || first == ASCII_LF) {
        file.read ();
        first = file.peek ();
    }
    bool is_tape = (first == ';');
    Serial.printf ("send: %s (%s)\n", name, is_tape ? "paper tape" : "text");

    mode = MODE_SENDING;
    cancel_send = false;
    if (is_tape) {
        send_paced ('L');
    }
    char previous = 0;
    while (file.available () && !cancel_send) {
        char c = file.read ();
        if (c == ASCII_CR || c == ASCII_LF) {
            if (c == ASCII_LF && previous == ASCII_CR) {
                previous = c;
                continue;       // Second half of a CR LF already sent.
            }
            send_paced (ASCII_CR);
            if (is_tape) {
                send_paced (ASCII_LF);
            }
        } else if (c >= ' ' && c < ASCII_RUBOUT) {
            send_paced (c);
        }
        previous = c;
    }
    file.close ();
    mode = MODE_TERMINAL;
    Serial.println (cancel_send ? "send: cancelled" : "send: done");
    beep ();
}

// ------------------------------------------------------------------------------

void on_keyboard_press (int key) {
    Serial.printf ("key: 0x%02X\n", key);     // Bring-up aid: log what the USB keyboard sends.
    switch (mode) {
        case MODE_TERMINAL:
            if (key == FILE_MENU_KEY) {
                open_file_menu ();
            } else {
                on_key_press (key);
            }
            break;
        case MODE_FILE_MENU:
            file_menu_key (key);
            break;
        case MODE_SENDING:
            if (key == ASCII_ESC) {
                cancel_send = true;
            }
            break;
    }
}

void check_keyboard_connection () {
    static bool was_connected = false;
    bool connected = keyboard;
    if (connected != was_connected) {
        if (connected) {
            Serial.printf ("keyboard: connected (%04X:%04X)\n", keyboard.idVendor (), keyboard.idProduct ());
        } else {
            Serial.println ("keyboard: disconnected");
        }
        was_connected = connected;
    }
}

// ------------------------------------------------------------------------------

void setup () {
    Serial.begin (115200);      // USB debug console.
    while (!Serial && millis () < 2000) {
        // Give a debug console a moment to connect.
    }
    Serial.println ("Adam77 starting");
    if (CrashReport) {
        Serial.print (CrashReport);     // Why the previous run restarted, if it crashed.
    }

    Serial1.begin (HOST_BAUD, HOST_FORMAT);
    pinMode (PIEZO, OUTPUT);

    Serial.println ("setup: display");
    tft.begin ();
    tft.setRotation (3);
    tft.setFont (LiberationMono_8_Bold);
    tft.fillScreen (background_color);
    text.begin (NUM_ROWS, NUM_COLUMNS);

    Serial.println ("setup: USB host");
    usb_host.begin ();
    keyboard.attachPress (on_keyboard_press);
    Serial.println ("setup: done");

    const char *banner = "ADAM77 READY\r\n";
    while (*banner) {
        handle_host_char (*banner++);
    }
    beep ();
}

void loop () {
    usb_host.Task ();
    check_keyboard_connection ();

    // Bring-up aid: text typed in `pio device monitor` is sent as if it came from the keyboard.
    while (Serial.available ()) {
        on_key_press (Serial.read ());
    }

    if (pending_send >= 0) {
        int16_t index = pending_send;
        pending_send = -1;
        send_file (tape_names[index]);
    }

    // While the file menu is up, host output waits in the serial buffer.
    while (mode == MODE_TERMINAL && Serial1.available ()) {
        handle_host_char ((char) Serial1.read ());
    }

    if (mode == MODE_TERMINAL && cursor_timer >= CURSOR_FLASH_INTERVAL) {
        flash_cursor ();
    }
}
