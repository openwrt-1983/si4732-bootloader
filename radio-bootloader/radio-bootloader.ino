/*
 * Dual-firmware boot selector for ESP32-S3 SI4732 radio (sunnygold board)
 * Screen : ST7789 170x320 8-bit parallel
 * Input  : EC11 encoder  A=GPIO2  B=GPIO1  SW=GPIO21
 *
 * Behaviour:
 *   - two cards shown side by side: OTA_0 (4MB) / OTA_1 (6MB)
 *   - turn CW  -> select the right card; turn CCW -> select the left card
 *   - no wrap-around at either end
 *   - short press encoder (SW, <2s) -> boot the selected firmware immediately
 *   - long press (>=2s) -> IGNORED (does nothing), never reboots
 *   - no input for 5 seconds -> auto boot the currently selected firmware
 */
#include <Arduino.h>
#include "LGFX_Config.h"
#include "FontBig.h"       // custom 1bpp GFXfont, 44px native, ASCII 0x20-0x7e
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include <Preferences.h>

LGFX lcd;

#define PIN_ENC_A     2
#define PIN_ENC_B     1
#define PIN_ENC_SW    21
#define EC11_REVERSE  0        // if rotation feels mirrored, set to 1

#define AUTO_START_MS 5000UL
#define LONG_PRESS_IGNORE_MS 2000UL   // 长按 >=2s 直接忽略，不触发启动/重启

// display labels on the two cards (partition slot names)
// index 0 = LEFT card (default highlight / auto-boot), index 1 = RIGHT card
static const char *FW_NAME[2] = {"OTA_0", "OTA_1"};
static const esp_partition_subtype_t FW_SUB[2] = {
    ESP_PARTITION_SUBTYPE_APP_OTA_0,   // zhang-chong  -> ota_0 @ 0x110000 (4MB, front)
    ESP_PARTITION_SUBTYPE_APP_OTA_1    // Bright-Heart -> ota_1 @ 0x510000 (6MB)
};

static int selected = 0;   // start on the left card (overridden by saved choice)
static unsigned long lastActionMs = 0;
static bool swHeld = false;
static bool launched = false;
static int lastShownSec = -1;

// Remember the last chosen firmware across power cycles.
static Preferences prefs;
static int loadLastChoice()
{
    prefs.begin("bootsel", true);
    // key bumped to choice2 when card order was swapped, so a stale
    // 0/1 from the old order cannot highlight the wrong firmware
    int v = prefs.getInt("choice2", 0);
    prefs.end();
    return (v == 0 || v == 1) ? v : 0;
}
static void saveLastChoice(int v)
{
    prefs.begin("bootsel", false);
    prefs.putInt("choice2", v);
    prefs.end();
}

// ---------- colours (RGB565) ----------
#define C_BG        0x0000
#define C_TITLE     0xFFE0
#define C_CARD      0x1082
#define C_CARD_SEL  0x05CF
#define C_BORDER    0x4208
#define C_BORDER_SEL 0x07FF
#define C_TEXT      0xFFFF
#define C_TEXT_DIM  0x8410
#define C_HINT      0x041F

// ---------- EC11 quadrature decoder ----------
static const int8_t ENC_TABLE[16] = {
    0, -1, 1, 0,
    1,  0, 0, -1,
    -1, 0, 0, 1,
    0,  1, -1, 0
};
static uint8_t encAB = 0xFF;

static int pollEncoder()
{
    uint8_t a = digitalRead(PIN_ENC_A) ? 1 : 0;
    uint8_t b = digitalRead(PIN_ENC_B) ? 1 : 0;
    uint8_t s = (a << 1) | b;
    if (encAB == 0xFF) { encAB = s; return 0; }
    uint8_t idx = (encAB << 2) | s;
    encAB = s;
    int d = ENC_TABLE[idx];
#if EC11_REVERSE
    d = -d;
#endif
    return d;
}

// ---------- encoder push button ----------
// Short press (<2s) boots the highlighted firmware.
// Long press (>=2s) is explicitly ignored: no boot, no restart, no menu.
static bool buttonPressed()
{
    static unsigned long lastEdge = 0;
    static unsigned long pressStart = 0;
    static bool latched = false;

    bool down = (digitalRead(PIN_ENC_SW) == LOW);
    unsigned long now = millis();

    if (down != latched && (now - lastEdge > 40)) {
        lastEdge = now;
        latched = down;
        swHeld = down;

        if (down) {
            pressStart = now;
            lastActionMs = now;   // hold pauses the auto-boot countdown
            return false;
        }

        // released: only a short press is accepted
        if ((now - pressStart) < LONG_PRESS_IGNORE_MS) return true;
        lastActionMs = now;   // long press ignored: restart the 5s countdown
    }
    return false;
}

// ---------- drawing ----------
// Card name uses a native 44px 1bpp GFXfont (FontBig), not a scaled-up bitmap,
// so it stays crisp. "OTA_0"/"OTA_1" measure 122px in a 144px card -> 11px
// margins on BOTH sides, drawn with MC_DATUM so the text is truly centred.
static void drawCard(int idx, int x, int y, int w, int h)
{
    bool sel = (idx == selected);
    uint16_t fill = sel ? C_CARD_SEL : C_CARD;
    uint16_t edge = sel ? C_BORDER_SEL : C_BORDER;

    lcd.fillRoundRect(x, y, w, h, 10, edge);
    lcd.fillRoundRect(x + 3, y + 3, w - 6, h - 6, 8, fill);

    // simple radio icon, kept small at the top so the big label has room
    int icx = x + w / 2;
    int icy = y + 26;
    lcd.fillRect(icx - 18, icy - 10, 36, 20, sel ? 0x07FF : 0x4208);
    lcd.fillRect(icx - 15, icy - 8, 21, 16, C_BG);
    lcd.fillCircle(icx + 11, icy, 6, sel ? C_TITLE : C_TEXT_DIM);
    lcd.drawLine(icx, icy - 8, icx, icy + 8, sel ? C_TITLE : C_TEXT_DIM);
    lcd.drawLine(icx + 16, icy - 10, icx + 22, icy - 18, sel ? C_TITLE : C_TEXT_DIM);

    // big, centred slot name: 122px wide, 11px padding on each side
    lcd.setFont(&FontBig);
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextSize(1);
    lcd.setTextColor(sel ? C_TEXT : C_TEXT_DIM);
    lcd.drawString(FW_NAME[idx], icx, y + 64);

    // partition size label below the name
    lcd.setFont(&fonts::DejaVu12);
    lcd.setTextColor(sel ? C_TITLE : C_TEXT_DIM);
    lcd.drawString(idx == 0 ? "4MB" : "6MB", icx, y + h - 16);

    // selection marker
    if (sel) {
        lcd.fillTriangle(x + 8, y + 8, x + 22, y + 8, x + 15, y + 18, C_TITLE);
    }
}

static void drawCountdown(int sec)
{
    lcd.fillRect(0, 155, 320, 15, C_BG);
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextDatum(ML_DATUM);
    lcd.setTextSize(1);
    lcd.setTextColor(C_HINT);
    char buf[48];
    snprintf(buf, sizeof(buf), "Auto boot: %ds   turn=select  short press=boot", sec);
    lcd.drawString(buf, 10, 163);
}

static void drawAll()
{
    lcd.fillScreen(C_BG);

    lcd.setFont(&fonts::DejaVu18);
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextSize(1);
    lcd.setTextColor(C_TITLE);
    lcd.drawString("Select firmware", 160, 16);

    // wider gap between the two cards (12px in the middle) and margins at edges
    drawCard(0, 10, 38, 144, 112);
    drawCard(1, 166, 38, 144, 112);

    lcd.setFont(&fonts::DejaVu12);
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextColor(C_TEXT_DIM);
    lcd.drawString("turn encoder to select", 160, 153);

    lastShownSec = -1;
}

static void launch()
{
    if (launched) return;
    launched = true;

    lcd.fillScreen(C_BG);
    lcd.setFont(&fonts::DejaVu18);
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextSize(1);
    lcd.setTextColor(C_TITLE);
    lcd.drawString("Booting", 160, 62);
    lcd.setFont(&FontBig);
    lcd.setTextColor(C_TEXT);
    lcd.drawString(FW_NAME[selected], 160, 110);

    // Remember this choice; on the next cold boot the menu highlights it again.
    saveLastChoice(selected);

    const esp_partition_t *target = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, FW_SUB[selected], NULL);
    if (target) {
        esp_ota_set_boot_partition(target);
    }
    delay(400);
    // esp_restart() = software reset; the custom bootloader will follow
    // otadata and boot the chosen firmware. A later power-off/on is a cold
    // boot and returns here to the menu.
    esp_restart();
}

void setup()
{
    pinMode(PIN_ENC_A, INPUT_PULLUP);
    pinMode(PIN_ENC_B, INPUT_PULLUP);
    pinMode(PIN_ENC_SW, INPUT_PULLUP);

    lcd.init();
    lcd.setRotation(3);              // 320 x 170 landscape, rotated 180 deg (panel mounted upside down)
    lcd.setBrightness(140);
    lcd.fillScreen(C_BG);

    // Default-highlight the firmware chosen last time.
    selected = loadLastChoice();

    lastActionMs = millis();
    drawAll();
}

void loop()
{
    if (launched) { delay(10); return; }

    int d = pollEncoder();
    if (d != 0) {
        int next = selected + (d > 0 ? 1 : -1);
        if (next < 0) next = 0;       // no wrap
        if (next > 1) next = 1;       // no wrap
        if (next != selected) {
            selected = next;
            lastActionMs = millis();
            drawAll();
        }
    }

    if (buttonPressed()) {
        lastActionMs = millis();
        launch();
        return;
    }

    unsigned long elapsed = millis() - lastActionMs;
    int sec = (int)((AUTO_START_MS - elapsed) / 1000UL);
    if (elapsed >= AUTO_START_MS) sec = 0;
    if (sec < 0) sec = 0;
    if (sec != lastShownSec) {
        lastShownSec = sec;
        drawCountdown(sec);
    }
    if (!swHeld && elapsed >= AUTO_START_MS) {
        launch();
        return;
    }

    delay(2);
}
