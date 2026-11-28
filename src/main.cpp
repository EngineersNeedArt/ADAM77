// ==============================================================================
// Adam77 (Teensy 4.1)
// A 40x24 ASCII terminal for early 8-bit computers such as the KIM-1.
//
//   USB keyboard (USB host port) --> Serial1 TX --> KIM-1 TTY input
//   KIM-1 TTY output --> Serial1 RX --> 40x24 text on an ILI9341 320x240 TFT
//   F2: pick a file on the microSD card: send it to the KIM-1 (paper tapes, BASIC)
//       or read it on the screen (.txt)
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

// Colors: white for Adam77's own text (banners, selection bar, startup message), green
// for anything sent to the KIM-1 (the terminal's own color), dark green for files that
// are only read.
#define CHROME_COLOR            ILI9341_WHITE
#define RGB565(r, g, b)         ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))  // 0-255 each.
#define READ_ONLY_COLOR         RGB565 (0, 102, 0)  // Dark green, 40% of the terminal's.
#define CHROME_FLAG             0x80    // Set on text-buffer characters drawn in CHROME_COLOR.

#define ASCII_BEL               0x07
#define ASCII_BS                0x08
#define ASCII_LF                0x0A
#define ASCII_FF                0x0C
#define ASCII_CR                0x0D
#define ASCII_ESC               0x1B
#define ASCII_RUBOUT            0x7F

// --- Files on the microSD card -------------------------------------------------
#define FILE_MENU_KEY           KEYD_F2
#define MAX_ENTRIES             256     // Files and folders shown per folder.
#define MAX_ENTRY_NAME          128     // Longer names are skipped.
#define MAX_PATH_LENGTH         256
#define MENU_FIRST_ROW          2       // Rows 0-1: title. Rows 2-21: files. Row 23: help.
#define MENU_LIST_ROWS          20
#define ECHO_TIMEOUT_MS         50      // A character takes about 9 ms at 1200 baud 8N2.
#define MENU_TYPE_COLUMN        33      // Type label (FOLDER, TAPE, BASIC, TEXT) in the menu.
#define REPEAT_DELAY_MS         400     // Held arrow keys repeat in the menu and viewer.
#define REPEAT_INTERVAL_MS      60
#define VIEW_FIRST_ROW          1       // Row 0: file name. Rows 1-22: text. Row 23: help.
#define VIEW_ROWS               22
#define VIEW_BUFFER_SIZE        65536   // Longer text files are cut off.
#define VIEW_MAX_LINES          8192
#define TAB_WIDTH               8

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

enum Mode { MODE_TERMINAL, MODE_FILE_MENU, MODE_SENDING, MODE_VIEWING };
Mode mode = MODE_TERMINAL;

struct MenuEntry {
    char name[MAX_ENTRY_NAME];
    bool is_folder;
};
DMAMEM MenuEntry entries[MAX_ENTRIES];      // The current folder: folders first, then files.
int16_t entry_count = 0;
char current_folder[MAX_PATH_LENGTH] = "/";
int16_t menu_selection = 0;
int16_t menu_top = 0;
const char *menu_message = NULL;    // Shown instead of the list (no card, empty folder).
char pending_send[MAX_PATH_LENGTH] = "";    // File to send, set by the menu.
bool cancel_send = false;

int repeat_key = 0;                 // Navigation key being held down, or 0.
bool repeating = false;
elapsedMillis repeat_timer;

// The extension says what a file is. Other files are sent as typed, unless they start with ';'.
enum FileType { FILE_OTHER, FILE_TAPE, FILE_BASIC, FILE_TEXT };

// Text viewer: the file in RAM, split into screen lines (word-wrapped to 40 columns).
DMAMEM char view_buffer[VIEW_BUFFER_SIZE];
DMAMEM uint32_t view_line_start[VIEW_MAX_LINES];
DMAMEM uint8_t view_line_length[VIEW_MAX_LINES];
int16_t view_line_count = 0;
int16_t view_top = 0;

// ------------------------------------------------------------------------------
// Display
// ------------------------------------------------------------------------------

void draw_char_colored (int16_t col, int16_t row, char character, bool inverted, uint16_t color) {
    int16_t x = col * X_ADVANCE;
    int16_t y = row * Y_ADVANCE;
    tft.fillRect (x, y, X_ADVANCE, Y_ADVANCE, inverted ? color : background_color);
    if (character > ' ') {
        tft.setCursor (x + CHAR_X_MARGIN, y + CHAR_Y_MARGIN);
        tft.setTextColor (inverted ? background_color : color);
        tft.drawFontChar (character);
    }
}

void draw_char_at (int16_t col, int16_t row, char character, bool inverted) {
    draw_char_colored (col, row, character, inverted, foreground_color);
}

void draw_cell (int16_t col, int16_t row, bool inverted) {
    char character = text.getCharRowColumn (row, col);
    uint16_t color = (character & CHROME_FLAG) ? CHROME_COLOR : foreground_color;
    draw_char_colored (col, row, character & 0x7F, inverted, color);
}

// Draws 'string' starting at 'col', padded with spaces (or cut off) to 'width' cells.
void draw_string_colored (int16_t col, int16_t row, const char *string, bool inverted, int16_t width, uint16_t color) {
    for (int16_t i = 0; i < width && col + i < NUM_COLUMNS; i++) {
        char character = *string ? *string++ : ' ';
        draw_char_colored (col + i, row, character, inverted, color);
    }
}

void draw_string_at (int16_t col, int16_t row, const char *string, bool inverted, int16_t width) {
    draw_string_colored (col, row, string, inverted, width, foreground_color);
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

// 'chrome' marks Adam77's own messages, drawn in CHROME_COLOR.
void handle_host_char (char c, bool chrome = false) {
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
                text.setCharRowColumn (chrome ? (c | CHROME_FLAG) : c, cursor_y, cursor_x);
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

// Folders first, then files, each sorted by name.
int compare_entries (const void *a, const void *b) {
    const MenuEntry *entry_a = (const MenuEntry *) a;
    const MenuEntry *entry_b = (const MenuEntry *) b;
    if (entry_a->is_folder != entry_b->is_folder) {
        return entry_a->is_folder ? -1 : 1;
    }
    return strcasecmp (entry_a->name, entry_b->name);
}

FileType file_type (const char *name) {
    const char *dot = strrchr (name, '.');
    if (!dot) {
        return FILE_OTHER;
    }
    if (!strcasecmp (dot, ".pap") || !strcasecmp (dot, ".ptp")) {
        return FILE_TAPE;
    }
    if (!strcasecmp (dot, ".bas")) {
        return FILE_BASIC;
    }
    if (!strcasecmp (dot, ".txt")) {
        return FILE_TEXT;
    }
    return FILE_OTHER;
}

const char *file_type_label (FileType type) {
    switch (type) {
        case FILE_TAPE:     return "TAPE";
        case FILE_BASIC:    return "BASIC";
        case FILE_TEXT:     return "TEXT";
        default:            return "";
    }
}

// Menu color for each type: green for files that are sent, gray for files that are read.
uint16_t file_type_color (FileType type) {
    return (type == FILE_TEXT) ? READ_ONLY_COLOR : foreground_color;
}

bool at_top_folder () {
    return strcmp (current_folder, "/") == 0;
}

// Puts the full path of 'name' in the current folder into 'path'.
void entry_path (const char *name, char *path, size_t size) {
    strlcpy (path, current_folder, size);
    if (!at_top_folder ()) {
        strlcat (path, "/", size);
    }
    strlcat (path, name, size);
}

// Fills entries with the current folder's files and folders. Returns false if the
// folder can't be opened.
bool read_folder () {
    entry_count = 0;
    menu_message = NULL;
    File folder = SD.open (current_folder);
    if (!folder) {
        return false;
    }
    if (!folder.isDirectory ()) {
        folder.close ();
        return false;
    }
    while (entry_count < MAX_ENTRIES) {
        File entry = folder.openNextFile ();
        if (!entry) {
            break;
        }
        // Skip hidden files and folders (such as the "._" files macOS leaves behind), the
        // folder Windows adds, and names too long to keep.
        const char *name = entry.name ();
        if (name[0] != '.' && strcmp (name, "System Volume Information") != 0 && strlen (name) < MAX_ENTRY_NAME) {
            strcpy (entries[entry_count].name, name);
            entries[entry_count].is_folder = entry.isDirectory ();
            entry_count++;
        }
        entry.close ();
    }
    folder.close ();
    qsort (entries, entry_count, sizeof (MenuEntry), compare_entries);
    if (entry_count == 0) {
        menu_message = "EMPTY FOLDER";
    }
    return true;
}

void draw_menu_row (int16_t i) {
    int16_t row = MENU_FIRST_ROW + i;
    int16_t index = menu_top + i;
    if (menu_message && i == 0) {
        draw_string_at (0, row, "", false, 1);
        draw_string_at (1, row, menu_message, false, NUM_COLUMNS - 1);
    } else if (!menu_message && index < entry_count) {
        MenuEntry *entry = &entries[index];
        bool selected = (index == menu_selection);
        FileType type = entry->is_folder ? FILE_OTHER : file_type (entry->name);
        uint16_t color = (selected || entry->is_folder) ? CHROME_COLOR : file_type_color (type);
        char name[MAX_ENTRY_NAME];
        strlcpy (name, entry->name, sizeof (name));
        if (type != FILE_OTHER) {
            *strrchr (name, '.') = 0;   // The type label stands in for the extension.
        }
        const char *label = entry->is_folder ? "FOLDER" : file_type_label (type);
        draw_string_at (0, row, "", false, 1);
        draw_string_colored (1, row, name, selected, MENU_TYPE_COLUMN - 1, color);
        draw_string_colored (MENU_TYPE_COLUMN, row, label, selected, NUM_COLUMNS - 1 - MENU_TYPE_COLUMN, color);
        draw_string_at (NUM_COLUMNS - 1, row, "", false, 1);
    } else {
        draw_string_at (0, row, "", false, NUM_COLUMNS);
    }
}

void draw_file_menu () {
    // Title: the folder's path, cut from the left if it's too long.
    char title[NUM_COLUMNS + 1];
    int16_t length = strlen (current_folder);
    if (length <= NUM_COLUMNS - 4) {
        snprintf (title, sizeof (title), " SD:%s", current_folder);
    } else {
        snprintf (title, sizeof (title), " SD:...%s", current_folder + length - (NUM_COLUMNS - 7));
    }
    draw_string_colored (0, 0, title, true, NUM_COLUMNS, CHROME_COLOR);
    draw_string_at (0, 1, "", false, NUM_COLUMNS);
    for (int16_t i = 0; i < MENU_LIST_ROWS; i++) {
        draw_menu_row (i);
    }
    draw_string_at (0, NUM_ROWS - 2, "", false, NUM_COLUMNS);
    draw_string_colored (0, NUM_ROWS - 1, menu_message ? " ESC: BACK" : " ENTER: OPEN   ESC: BACK   A-Z: JUMP", true, NUM_COLUMNS, CHROME_COLOR);
}

// Reads and shows the current folder, with the entry named 'select' selected if it's there.
void show_folder (const char *select) {
    if (!read_folder ()) {
        strcpy (current_folder, "/");   // Gone, perhaps a different card: start at the top.
        if (!read_folder ()) {
            menu_message = "CAN'T READ SD CARD";
        }
    }
    menu_selection = 0;
    for (int16_t i = 0; select && i < entry_count; i++) {
        if (strcmp (entries[i].name, select) == 0) {
            menu_selection = i;
            break;
        }
    }
    menu_top = max (0, menu_selection - MENU_LIST_ROWS + 1);
    draw_file_menu ();
}

// Opens the menu where it was last left: same folder, same entry selected.
void open_file_menu () {
    hide_cursor ();
    mode = MODE_FILE_MENU;
    char select[MAX_ENTRY_NAME] = "";
    if (menu_selection < entry_count) {
        strcpy (select, entries[menu_selection].name);
    }
    if (!SD.begin (BUILTIN_SDCARD)) {   // Called each time, so a card can be swapped.
        entry_count = 0;
        menu_message = "NO SD CARD";
        menu_selection = menu_top = 0;
        draw_file_menu ();
        return;
    }
    show_folder (select);
}

void open_folder (const char *name) {
    char path[MAX_PATH_LENGTH];
    entry_path (name, path, sizeof (path));
    strlcpy (current_folder, path, sizeof (current_folder));
    show_folder (NULL);
}

// Goes up one folder, selecting the folder just left.
void open_parent_folder () {
    char *slash = strrchr (current_folder, '/');
    char came_from[MAX_ENTRY_NAME];
    strlcpy (came_from, slash + 1, sizeof (came_from));
    if (slash == current_folder) {
        slash[1] = 0;
    } else {
        *slash = 0;
    }
    show_folder (came_from);
}

void open_text_viewer (int16_t index);

void close_file_menu () {
    mode = MODE_TERMINAL;
    restore_terminal ();
}

// The next entry after the selection whose name starts with 'c' (wrapping around), or -1.
int16_t next_entry_starting_with (char c) {
    for (int16_t i = 1; i <= entry_count; i++) {
        int16_t index = (menu_selection + i) % entry_count;
        if (tolower (entries[index].name[0]) == tolower (c)) {
            return index;
        }
    }
    return -1;
}

void file_menu_key (int key) {
    if (key == FILE_MENU_KEY || (key == ASCII_ESC && at_top_folder ())) {
        close_file_menu ();
        return;
    }
    if (key == ASCII_ESC || key == ASCII_BS || key == ASCII_RUBOUT || key == KEYD_LEFT) {
        if (!at_top_folder ()) {
            open_parent_folder ();
        }
        return;
    }
    if (menu_message) {
        return;     // Nothing to choose.
    }
    MenuEntry *entry = &entries[menu_selection];
    int16_t selection = menu_selection;
    switch (key) {
        case KEYD_UP:           selection--;                    break;
        case KEYD_DOWN:         selection++;                    break;
        case KEYD_PAGE_UP:      selection -= MENU_LIST_ROWS;    break;
        case KEYD_PAGE_DOWN:    selection += MENU_LIST_ROWS;    break;
        case KEYD_HOME:         selection = 0;                  break;
        case KEYD_END:          selection = entry_count - 1;    break;
        case KEYD_RIGHT:
            if (entry->is_folder) {
                open_folder (entry->name);
            }
            return;
        case ASCII_LF:
        case ASCII_CR:
            if (entry->is_folder) {
                open_folder (entry->name);
            } else if (file_type (entry->name) == FILE_TEXT) {
                open_text_viewer (menu_selection);
            } else {
                entry_path (entry->name, pending_send, sizeof (pending_send));
                close_file_menu ();     // loop () sends it once the menu is closed.
            }
            return;
        default:
            if (key < 0x80 && isalnum (key)) {
                selection = next_entry_starting_with ((char) key);
                if (selection >= 0) {
                    break;
                }
            }
            return;
    }
    int16_t old_selection = menu_selection;
    int16_t old_top = menu_top;
    menu_selection = constrain (selection, 0, entry_count - 1);
    if (menu_selection < menu_top) {
        menu_top = menu_selection;
    } else if (menu_selection >= menu_top + MENU_LIST_ROWS) {
        menu_top = menu_selection - MENU_LIST_ROWS + 1;
    }
    if (menu_top == old_top) {      // Redraw only the two rows that changed.
        draw_menu_row (old_selection - menu_top);
        draw_menu_row (menu_selection - menu_top);
    } else {
        for (int16_t i = 0; i < MENU_LIST_ROWS; i++) {
            draw_menu_row (i);
        }
    }
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

// Paper tapes (.pap, .ptp, or any other file whose first character is ';') are sent after
// an "L", which puts the KIM-1 monitor in paper-tape load mode, with CR LF line ends.
// Other files (.bas listings and the like) are sent as if typed, with CR line ends.
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
    FileType type = file_type (name);
    bool is_tape = (type == FILE_TAPE || (type == FILE_OTHER && first == ';'));
    Serial.printf ("send: %s (%s)\n", name, is_tape ? "paper tape" : "typed");

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
// Text viewer: .txt files are shown on the screen and not sent (instructions, notes)
// ------------------------------------------------------------------------------

// Reads the file into view_buffer: tabs expanded, CR LF and lone CR turned into LF,
// other control characters dropped. Returns false if the file can't be opened.
bool load_text_file (const char *name) {
    File file = SD.open (name);
    if (!file) {
        return false;
    }
    uint32_t length = 0;
    int16_t column = 0;
    char previous = 0;
    while (file.available () && length < VIEW_BUFFER_SIZE) {
        char c = file.read ();
        if (c == ASCII_LF && previous == ASCII_CR) {
            previous = c;
            continue;       // Second half of a CR LF.
        }
        previous = c;
        if (c == ASCII_CR || c == ASCII_LF) {
            view_buffer[length++] = ASCII_LF;
            column = 0;
        } else if (c == '\t') {
            do {
                view_buffer[length++] = ' ';
                column++;
            } while (column % TAB_WIDTH && length < VIEW_BUFFER_SIZE);
        } else if (c >= ' ' && c < ASCII_RUBOUT) {
            view_buffer[length++] = c;
            column++;
        }
    }
    if (file.available ()) {
        Serial.printf ("view: %s cut off at %d bytes\n", name, VIEW_BUFFER_SIZE);
    }
    file.close ();

    // Split into screen lines, breaking long lines at the last space that fits.
    view_line_count = 0;
    uint32_t start = 0;
    while (start < length && view_line_count < VIEW_MAX_LINES) {
        uint32_t end = start;
        while (end < length && view_buffer[end] != ASCII_LF && end - start < NUM_COLUMNS) {
            end++;
        }
        uint32_t next = end + 1;        // Skip the LF.
        if (end < length && view_buffer[end] != ASCII_LF) {
            next = end;                 // Hit the right edge: wrap.
            uint32_t space = end;
            while (space > start && view_buffer[space] != ' ') {
                space--;
            }
            if (space > start) {
                end = space;            // Break at the space and drop it.
                next = space + 1;
            }
        }
        view_line_start[view_line_count] = start;
        view_line_length[view_line_count] = end - start;
        view_line_count++;
        start = next;
    }
    return true;
}

void draw_text_viewer () {
    for (int16_t i = 0; i < VIEW_ROWS; i++) {
        int16_t line = view_top + i;
        int16_t length = (line < view_line_count) ? view_line_length[line] : 0;
        const char *chars = view_buffer + ((line < view_line_count) ? view_line_start[line] : 0);
        for (int16_t col = 0; col < NUM_COLUMNS; col++) {
            draw_char_at (col, VIEW_FIRST_ROW + i, col < length ? chars[col] : ' ', false);
        }
    }
    char help[64];      // Cut to NUM_COLUMNS when drawn.
    int16_t last = min (view_top + VIEW_ROWS, (int) view_line_count);
    snprintf (help, sizeof (help), " ESC: BACK  SPACE: MORE  %d-%d/%d", view_line_count ? view_top + 1 : 0, last, view_line_count);
    draw_string_colored (0, NUM_ROWS - 1, help, true, NUM_COLUMNS, CHROME_COLOR);
}

void open_text_viewer (int16_t index) {
    char path[MAX_PATH_LENGTH];
    entry_path (entries[index].name, path, sizeof (path));
    if (!load_text_file (path)) {
        beep ();
        return;
    }
    Serial.printf ("view: %s (%d lines)\n", path, view_line_count);
    mode = MODE_VIEWING;
    view_top = 0;
    char title[NUM_COLUMNS + 1];
    snprintf (title, sizeof (title), " %s", entries[index].name);
    draw_string_colored (0, 0, title, true, NUM_COLUMNS, CHROME_COLOR);
    draw_text_viewer ();
}

void text_viewer_key (int key) {
    if (key == FILE_MENU_KEY) {
        close_file_menu ();     // Straight back to the terminal.
        return;
    }
    if (key == ASCII_ESC) {
        mode = MODE_FILE_MENU;  // Back to the list, with the same file selected.
        draw_file_menu ();
        return;
    }
    int16_t top = view_top;
    switch (key) {
        case KEYD_UP:           top--;                                  break;
        case KEYD_DOWN:
        case ASCII_LF:
        case ASCII_CR:          top++;                                  break;
        case KEYD_PAGE_UP:      top -= VIEW_ROWS - 1;                   break;
        case ' ':
        case KEYD_PAGE_DOWN:    top += VIEW_ROWS - 1;                   break;
        case KEYD_HOME:         top = 0;                                break;
        case KEYD_END:          top = view_line_count - VIEW_ROWS;      break;
        default:                return;
    }
    top = constrain (top, 0, max (0, view_line_count - VIEW_ROWS));
    if (top != view_top) {
        view_top = top;
        draw_text_viewer ();
    }
}

// ------------------------------------------------------------------------------

void handle_key (int key) {
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
        case MODE_VIEWING:
            text_viewer_key (key);
            break;
        case MODE_SENDING:
            if (key == ASCII_ESC) {
                cancel_send = true;
            }
            break;
    }
}

bool is_repeating_key (int key) {
    return key == KEYD_UP || key == KEYD_DOWN || key == KEYD_PAGE_UP || key == KEYD_PAGE_DOWN;
}

void on_keyboard_press (int key) {
    Serial.printf ("key: 0x%02X\n", key);     // Bring-up aid: log what the USB keyboard sends.
    repeat_key = is_repeating_key (key) ? key : 0;
    repeating = false;
    repeat_timer = 0;
    handle_key (key);
}

void on_keyboard_release (int key) {
    if (key == repeat_key) {
        repeat_key = 0;
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
            repeat_key = 0;
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
    keyboard.attachRelease (on_keyboard_release);
    Serial.println ("setup: done");

    const char *banner = "ADAM77 READY\r\n";
    while (*banner) {
        handle_host_char (*banner++, true);
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

    if (pending_send[0]) {
        char path[MAX_PATH_LENGTH];
        strcpy (path, pending_send);
        pending_send[0] = 0;
        send_file (path);
    }

    // Held navigation keys repeat (USBHost_t36 doesn't repeat keys itself).
    if (repeat_key && (mode == MODE_FILE_MENU || mode == MODE_VIEWING)
            && repeat_timer >= (repeating ? REPEAT_INTERVAL_MS : REPEAT_DELAY_MS)) {
        repeat_timer = 0;
        repeating = true;
        handle_key (repeat_key);
    }

    // While the file menu is up, host output waits in the serial buffer.
    while (mode == MODE_TERMINAL && Serial1.available ()) {
        handle_host_char ((char) Serial1.read ());
    }

    if (mode == MODE_TERMINAL && cursor_timer >= CURSOR_FLASH_INTERVAL) {
        flash_cursor ();
    }
}
