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

const char *daysOfTheWeek[7] = {
    "Zondag", "Maandag", "Dinsdag", "Woensdag",
    "Donderdag", "Vrijdag", "Zaterdag"
};

// Set to 1 when the RTC module is connected.
#define USE_RTC 0

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

void setup()
{
    Serial.begin(115200);
    Wire.begin(8, 9);
    pinMode(BUTTON_PIN_1, INPUT_PULLUP);
    pinMode(BUTTON_PIN_2, INPUT_PULLUP);

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

    // Button 1 toggles LEDs on/off; Button 2 cycles color on press
    static bool leds_on = false;
    static uint8_t color_index = 0;
    static bool last_leds_on = false;
    static uint8_t last_color_index = 0;
    static int last_btn1 = HIGH;
    static int last_btn2 = HIGH;
    static uint32_t last_btn1_ms = 0;
    static uint32_t last_btn2_ms = 0;
    const uint32_t debounce_ms = 200;

    int btn1 = digitalRead(BUTTON_PIN_1);
    int btn2 = digitalRead(BUTTON_PIN_2);

    if (last_btn1 == HIGH && btn1 == LOW && (nowMs - last_btn1_ms) > debounce_ms) {
        leds_on = !leds_on;
        last_btn1_ms = nowMs;
    }
    if (last_btn2 == HIGH && btn2 == LOW && (nowMs - last_btn2_ms) > debounce_ms) {
        color_index = (color_index + 1) % 7;
        last_btn2_ms = nowMs;
    }
    last_btn1 = btn1;
    last_btn2 = btn2;

    uint32_t color = 0;
    switch (color_index) {
        case 0: color = strip.Color(255, 0, 0); break;     // red
        case 1: color = strip.Color(0, 255, 0); break;     // green
        case 2: color = strip.Color(0, 0, 255); break;     // blue
        case 3: color = strip.Color(255, 255, 255); break; // white
        case 4: color = strip.Color(255, 255, 0); break;   // yellow
        case 5: color = strip.Color(0, 255, 255); break;   // cyan
        default: color = strip.Color(255, 0, 255); break;  // magenta
    }

    bool need_update = (leds_on != last_leds_on) || (color_index != last_color_index);
    if (need_update) {
        if (leds_on) {
            for (uint16_t i = 0; i < strip.numPixels(); i++) {
                strip.setPixelColor(i, color);
            }
        } else {
            strip.clear();
        }
        strip.show();
        last_leds_on = leds_on;
        last_color_index = color_index;
    }

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
