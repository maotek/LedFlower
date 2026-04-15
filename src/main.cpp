#include <lvgl.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include "RTClib.h"
#include "ui/ui.h"   // EEZ Studio generated UI

/* Screen resolution */
static const uint16_t screenWidth  = 240;
static const uint16_t screenHeight = 240;

/* LVGL buffer
   ~1/10 screen is recommended for PARTIAL mode */
static uint16_t lvgl_buf[screenWidth * 10];

TFT_eSPI tft = TFT_eSPI();
RTC_DS3231 rtc;
static const int LED_PIN = 3;
static const uint16_t LED_COUNT = 12;
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

static const int BUTTON_PIN_1 = 1;
static const int BUTTON_PIN_2 = 2;
static const uint8_t LED_BRIGHTNESS = 255;
static const uint8_t STATIC_MODE_COUNT = 7;
static const uint8_t EFFECT_MODE_COUNT = 5;
static const uint8_t MODE_COUNT = STATIC_MODE_COUNT + EFFECT_MODE_COUNT;

const char *daysOfTheWeek[7] = {
    "Zondag", "Maandag", "Dinsdag", "Woensdag",
    "Donderdag", "Vrijdag", "Zaterdag"
};

// Set to 1 when the RTC module is connected.
#define USE_RTC 1

#if LV_USE_LOG != 0
void my_print(lv_log_level_t level, const char *buf)
{
    LV_UNUSED(level);
    Serial.print(buf);
}
#endif

/* LVGL 9 flush callback */
void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);

    // RGB565 buffer
    tft.pushColors((uint16_t *)px_map, w * h, true);

    tft.endWrite();

    lv_display_flush_ready(disp);
}

static uint32_t color_wheel(uint8_t pos)
{
    pos = 255 - pos;
    if (pos < 85) {
        return strip.Color(255 - pos * 3, 0, pos * 3);
    }
    if (pos < 170) {
        pos -= 85;
        return strip.Color(0, pos * 3, 255 - pos * 3);
    }
    pos -= 170;
    return strip.Color(pos * 3, 255 - pos * 3, 0);
}

static uint32_t scale_color(uint32_t color, uint8_t scale)
{
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    r = (uint16_t)r * scale / 255;
    g = (uint16_t)g * scale / 255;
    b = (uint16_t)b * scale / 255;
    return strip.Color(r, g, b);
}

static void fill_all(uint32_t color)
{
    for (uint16_t i = 0; i < strip.numPixels(); i++) {
        strip.setPixelColor(i, color);
    }
}

void setup()
{
    Serial.begin(115200);
    Wire.begin(8, 9);
    pinMode(BUTTON_PIN_1, INPUT_PULLUP);
    pinMode(BUTTON_PIN_2, INPUT_PULLUP);
    randomSeed((uint32_t)micros());

#if USE_RTC
    if (!rtc.begin()) {
        Serial.println("Couldn't find RTC");
        while (1) delay(1000);
    }

    if (rtc.lostPower()) {
        Serial.println("RTC lost power, setting compile time");
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
#endif

    lv_init();

#if LV_USE_LOG != 0
    lv_log_register_print_cb(my_print);
#endif

    tft.begin();
    tft.setRotation(3);
    tft.fillScreen(TFT_BLACK);
    tft.invertDisplay(true);
    strip.begin();
    strip.setBrightness(LED_BRIGHTNESS);
    strip.clear();
    strip.show();
    delay(200);

    /* Create LVGL display */
    lv_display_t *disp = lv_display_create(screenWidth, screenHeight);
    lv_display_set_flush_cb(disp, my_disp_flush);
    lv_display_set_buffers(
        disp,
        lvgl_buf,
        NULL,
        sizeof(lvgl_buf),
        LV_DISPLAY_RENDER_MODE_PARTIAL
    );

    /* Optional but recommended for TFT_eSPI RGB565 setups */
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    /* Make this display the default one */
    lv_display_set_default(disp);

    /* Init EEZ Studio UI */
    ui_init();
}

void loop()
{
    static uint32_t lastTick = millis();
    uint32_t nowMs = millis();

    /* Feed LVGL tick */
    lv_tick_inc(nowMs - lastTick);
    lastTick = nowMs;

    /* Let LVGL run */
    lv_timer_handler();

    /* Let EEZ Flow / EEZ runtime run */
    ui_tick();

    // Button 1 toggles LEDs on/off; Button 2 cycles mode on press
    static bool leds_on = false;
    static uint8_t mode_index = 0;
    static bool last_leds_on = false;
    static uint8_t last_mode_index = 0;
    static uint32_t last_anim_ms = 0;
    static uint16_t anim_step = 0;
    static int last_btn1 = HIGH;
    static int last_btn2 = HIGH;
    static uint32_t last_btn1_ms = 0;
    static uint32_t last_btn2_ms = 0;
    const uint32_t debounce_ms = 200;
    const uint32_t anim_interval_ms = 20;

    int btn1 = digitalRead(BUTTON_PIN_1);
    int btn2 = digitalRead(BUTTON_PIN_2);

    if (last_btn1 == HIGH && btn1 == LOW && (nowMs - last_btn1_ms) > debounce_ms) {
        leds_on = !leds_on;
        last_btn1_ms = nowMs;
    }
    if (last_btn2 == HIGH && btn2 == LOW && (nowMs - last_btn2_ms) > debounce_ms) {
        mode_index = (mode_index + 1) % MODE_COUNT;
        last_btn2_ms = nowMs;
    }
    last_btn1 = btn1;
    last_btn2 = btn2;

    if (mode_index != last_mode_index) {
        anim_step = 0;
        last_anim_ms = 0;
    }

    if (!leds_on) {
        if (last_leds_on) {
            strip.clear();
            strip.show();
        }
    } else if (mode_index < STATIC_MODE_COUNT) {
        if (!last_leds_on || (mode_index != last_mode_index)) {
            uint32_t color = 0;
            switch (mode_index) {
                case 0: color = strip.Color(255, 0, 0); break;     // red
                case 1: color = strip.Color(0, 255, 0); break;     // green
                case 2: color = strip.Color(0, 0, 255); break;     // blue
                case 3: color = strip.Color(255, 255, 255); break; // white
                case 4: color = strip.Color(255, 255, 0); break;   // yellow
                case 5: color = strip.Color(0, 255, 255); break;   // cyan
                default: color = strip.Color(255, 0, 255); break;  // magenta
            }
            fill_all(color);
            strip.show();
        }
    } else {
        bool anim_due = (nowMs - last_anim_ms) >= anim_interval_ms;
        if (anim_due || !last_leds_on || (mode_index != last_mode_index)) {
            if (anim_due) {
                last_anim_ms = nowMs;
                anim_step++;
            }

            uint8_t effect_index = mode_index - STATIC_MODE_COUNT;
            switch (effect_index) {
                case 0: { // RGB breathing
                    uint16_t t = (nowMs / 10) % 512;
                    uint8_t brightness = (t < 256) ? t : 511 - t;
                    uint8_t hue = (nowMs / 20) & 0xFF;
                    uint32_t base = color_wheel(hue);
                    fill_all(scale_color(base, brightness));
                } break;
                case 1: { // Rainbow circle
                    uint8_t offset = anim_step & 0xFF;
                    for (uint16_t i = 0; i < strip.numPixels(); i++) {
                        uint8_t hue = (uint8_t)((i * 256 / strip.numPixels() + offset) & 0xFF);
                        strip.setPixelColor(i, color_wheel(hue));
                    }
                } break;
                case 2: { // Theater chase
                    uint8_t phase = anim_step % 3;
                    uint32_t chase = color_wheel((anim_step * 5) & 0xFF);
                    for (uint16_t i = 0; i < strip.numPixels(); i++) {
                        if (((i + phase) % 3) == 0) {
                            strip.setPixelColor(i, chase);
                        } else {
                            strip.setPixelColor(i, 0);
                        }
                    }
                } break;
                case 3: { // Color wipe
                    uint16_t pos = anim_step % (strip.numPixels() + 1);
                    uint32_t wipe = color_wheel((anim_step * 3) & 0xFF);
                    for (uint16_t i = 0; i < strip.numPixels(); i++) {
                        strip.setPixelColor(i, (i < pos) ? wipe : 0);
                    }
                } break;
                default: { // Twinkle
                    for (uint16_t i = 0; i < strip.numPixels(); i++) {
                        uint32_t c = strip.getPixelColor(i);
                        strip.setPixelColor(i, scale_color(c, 200));
                    }
                    for (uint8_t k = 0; k < 2; k++) {
                        if (random(0, 4) == 0) {
                            uint16_t idx = random(strip.numPixels());
                            strip.setPixelColor(idx, color_wheel(random(0, 256)));
                        }
                    }
                } break;
            }
            strip.show();
        }
    }

    last_leds_on = leds_on;
    last_mode_index = mode_index;

    char time_str[9];
    const char *weekday_str = daysOfTheWeek[0];

#if USE_RTC
    // RTC read
    DateTime now = rtc.now();
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d",
             now.hour(), now.minute(), now.second());
    weekday_str = daysOfTheWeek[now.dayOfTheWeek()];
#else
    // Fallback: derive a running time from millis()
    uint32_t seconds = nowMs / 1000;
    uint8_t sec = seconds % 60;
    uint8_t min = (seconds / 60) % 60;
    uint8_t hour = (seconds / 3600) % 24;
    uint8_t day = (seconds / 86400) % 7;
    snprintf(time_str, sizeof(time_str), "%02u:%02u:%02u",
             hour, min, sec);
    weekday_str = daysOfTheWeek[day];
#endif

    lv_label_set_text(objects.time, time_str);
    lv_label_set_text(objects.weekday, weekday_str);


    delay(5);
}
