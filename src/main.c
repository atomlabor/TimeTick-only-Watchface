#include <pebble.h>

#define TIMER_END_KEY     1
#define MSG_KEY_TEMP      2   // AppMessage key: temperature in °C (int32)
#define MSG_KEY_REQUEST   3   // AppMessage key: phone sends weather request trigger

static Window *s_main_window, *s_timer_window;
static BitmapLayer *s_bg_layer, *s_timer_bg_layer;
static GBitmap *s_bg_bitmap = NULL, *s_timer_bg_bitmap = NULL;

static BitmapLayer *s_digit_layers[4];
static GBitmap *s_digit_bitmaps[4] = {NULL, NULL, NULL, NULL};
static BitmapLayer *s_timer_digit_layers[4];
static GBitmap *s_timer_digit_bitmaps[4] = {NULL, NULL, NULL, NULL};

static TextLayer *s_date_layer, *s_steps_layer, *s_timer_hint_layer;
static TextLayer *s_weather_layer;           // NEW: Wetter-Label
static Layer *s_battery_layer;
static int s_battery_level = 100;
static bool s_timer_running = false;
static bool s_alarm_ringing = false;
static int s_timer_seconds = 480;

// Wetter-State
static bool s_weather_visible = false;
static char s_weather_buf[16] = "--°C";        // aktueller Anzeigetext
static AppTimer *s_weather_hide_timer = NULL;  // Auto-Hide nach 4 s

static const uint32_t DIGIT_RES_IDS[10] = {
   RESOURCE_ID_IMG_0, RESOURCE_ID_IMG_1, RESOURCE_ID_IMG_2, RESOURCE_ID_IMG_3, RESOURCE_ID_IMG_4,
   RESOURCE_ID_IMG_5, RESOURCE_ID_IMG_6, RESOURCE_ID_IMG_7, RESOURCE_ID_IMG_8, RESOURCE_ID_IMG_9
};

static const GRect DIGIT_FRAMES[4] = {
   {{10, 85}, {50, 60}}, {{45, 85}, {50, 60}}, {{105, 85}, {50, 60}}, {{140, 85}, {50, 60}}
};
static const GRect TIMER_DIGIT_FRAMES[4] = {
   {{10, 85}, {50, 60}}, {{45, 85}, {50, 60}}, {{105, 85}, {50, 60}}, {{140, 85}, {50, 60}}
};

/* ── Vorwärts-Deklarationen ─────────────────────────────────────── */
static void update_timer_digits(void);
static void update_clock(void);
static void battery_layer_update_proc(Layer *layer, GContext *ctx);
static void battery_handler(BatteryChargeState charge);
static void hal_play_alarm(bool en);
static void health_handler(HealthEventType event, void *context);
static void request_weather(void);

/* ── Batterie ────────────────────────────────────────────────────── */
static void battery_layer_update_proc(Layer *layer, GContext *ctx) {
   int num_segments = 10, gap = 2, segment_w = 14, offset_x = 1;
   int filled_segments = (s_battery_level * num_segments) / 100;
   for (int i = 0; i < num_segments; i++) {
       GRect seg_rect = GRect(offset_x + (i * (segment_w + gap)), 0, segment_w, 8);
       graphics_context_set_fill_color(ctx, (i < filled_segments) ? GColorDarkGray : GColorWhite);
       graphics_fill_rect(ctx, seg_rect, 0, GCornerNone);
       graphics_context_set_stroke_color(ctx, GColorBlack);
       graphics_draw_rect(ctx, seg_rect);
   }
}

static void battery_handler(BatteryChargeState charge) {
   s_battery_level = charge.charge_percent;
   if (s_battery_layer) layer_mark_dirty(s_battery_layer);
}

/* ── Health ──────────────────────────────────────────────────────── */
static void health_handler(HealthEventType event, void *context) {
   if (event == HealthEventSignificantUpdate || event == HealthEventMovementUpdate) {
       update_clock();
   }
}

/* ── Alarm ───────────────────────────────────────────────────────── */
static void hal_play_alarm(bool en) {
   if (en) {
       #if defined(PBL_SPEAKER)
       speaker_play_tone(440, 5000, 100, SpeakerWaveformSquare);
       #endif
       vibes_enqueue_custom_pattern((VibePattern){.durations=(uint32_t[]){400,200,1000}, .num_segments=3});
   } else {
       vibes_cancel();
       #if defined(PBL_SPEAKER)
       speaker_stop();
       #endif
   }
}

/* ── Wetter: AppMessage ──────────────────────────────────────────── */

// Wetter-Layer nach 4 s wieder ausblenden
static void weather_hide_callback(void *ctx) {
   s_weather_hide_timer = NULL;
   s_weather_visible = false;
   if (s_weather_layer) layer_set_hidden(text_layer_get_layer(s_weather_layer), true);
}

// Wetter anzeigen (und Auto-Hide-Timer neu starten)
static void show_weather(void) {
   if (!s_weather_layer) return;
   text_layer_set_text(s_weather_layer, s_weather_buf);
   layer_set_hidden(text_layer_get_layer(s_weather_layer), false);
   s_weather_visible = true;

   if (s_weather_hide_timer) app_timer_cancel(s_weather_hide_timer);
   s_weather_hide_timer = app_timer_register(4000, weather_hide_callback, NULL);
}

// Eingehende Nachricht vom Phone (Temperatur in °C)
static void inbox_received_handler(DictionaryIterator *iter, void *ctx) {
   Tuple *t = dict_find(iter, MESSAGE_KEY_MSG_KEY_TEMP);
   if (t) {
       int temp = (int)t->value->int32;
       snprintf(s_weather_buf, sizeof(s_weather_buf), "%d\xc2\xb0C", temp); // UTF-8 °
       show_weather();
   }
}

// Wetteranfrage ans Phone schicken
static void request_weather(void) {
   DictionaryIterator *iter;
   if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
   dict_write_uint8(iter, MESSAGE_KEY_MSG_KEY_REQUEST, 1);
   app_message_outbox_send();
}

/* ── Timer-Digits ────────────────────────────────────────────────── */
static void update_timer_digits(void) {
   int d[4] = { (s_timer_seconds/60)/10, (s_timer_seconds/60)%10,
                (s_timer_seconds%60)/10,  (s_timer_seconds%60)%10 };
   for (int i = 0; i < 4; i++) {
       if (s_timer_digit_bitmaps[i]) gbitmap_destroy(s_timer_digit_bitmaps[i]);
       s_timer_digit_bitmaps[i] = gbitmap_create_with_resource(DIGIT_RES_IDS[d[i]]);
       if (s_timer_digit_bitmaps[i]) bitmap_layer_set_bitmap(s_timer_digit_layers[i], s_timer_digit_bitmaps[i]);
   }
}

/* ── Clock ───────────────────────────────────────────────────────── */
static void update_clock(void) {
   time_t now = time(NULL);
   struct tm *t = localtime(&now);
   int h = t->tm_hour;
   if (!clock_is_24h_style()) { h = (h % 12 == 0) ? 12 : h % 12; }
   int d[4] = { h/10, h%10, t->tm_min/10, t->tm_min%10 };
   for (int i = 0; i < 4; i++) {
       if (s_digit_bitmaps[i]) gbitmap_destroy(s_digit_bitmaps[i]);
       s_digit_bitmaps[i] = gbitmap_create_with_resource(DIGIT_RES_IDS[d[i]]);
       if (s_digit_bitmaps[i]) bitmap_layer_set_bitmap(s_digit_layers[i], s_digit_bitmaps[i]);
   }
   static char date_buf[16], steps_buf[16];
   strftime(date_buf, sizeof(date_buf), "%d.%m", t);
   text_layer_set_text(s_date_layer, date_buf);
   HealthValue val = health_service_sum_today(HealthMetricStepCount);
   snprintf(steps_buf, sizeof(steps_buf), "%d", (int)val);
   text_layer_set_text(s_steps_layer, steps_buf);
}

/* ── Main Window ─────────────────────────────────────────────────── */
static void main_window_load(Window *window) {
   Layer *root = window_get_root_layer(window);
   window_set_background_color(window, GColorBlack);

   s_bg_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_BG);
   s_bg_layer = bitmap_layer_create(GRect(0, 0, 200, 228));
   bitmap_layer_set_bitmap(s_bg_layer, s_bg_bitmap);
   layer_add_child(root, bitmap_layer_get_layer(s_bg_layer));

   for (int i = 0; i < 4; i++) {
       s_digit_layers[i] = bitmap_layer_create(DIGIT_FRAMES[i]);
       bitmap_layer_set_compositing_mode(s_digit_layers[i], GCompOpSet);
       bitmap_layer_set_background_color(s_digit_layers[i], GColorClear);
       layer_add_child(root, bitmap_layer_get_layer(s_digit_layers[i]));
   }

   s_date_layer = text_layer_create(GRect(45, 50, 70, 20));
   text_layer_set_background_color(s_date_layer, GColorClear);
   text_layer_set_text_color(s_date_layer, GColorBlack);
   text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
   layer_add_child(root, text_layer_get_layer(s_date_layer));

   s_steps_layer = text_layer_create(GRect(125, 50, 70, 20));
   text_layer_set_background_color(s_steps_layer, GColorClear);
   text_layer_set_text_color(s_steps_layer, GColorBlack);
   text_layer_set_font(s_steps_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
   layer_add_child(root, text_layer_get_layer(s_steps_layer));

   // Wetter-Label: direkt unter den Ziffern (Ziffern enden bei y=145)
   s_weather_layer = text_layer_create(GRect(0, 148, 200, 18));
   text_layer_set_background_color(s_weather_layer, GColorClear);
   text_layer_set_text_color(s_weather_layer, GColorBlack);
   text_layer_set_font(s_weather_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
   text_layer_set_text_alignment(s_weather_layer, GTextAlignmentCenter);
   text_layer_set_text(s_weather_layer, s_weather_buf);
   layer_set_hidden(text_layer_get_layer(s_weather_layer), true); // erst unsichtbar
   layer_add_child(root, text_layer_get_layer(s_weather_layer));

   s_battery_layer = layer_create(GRect(20, 165, 160, 8));
   layer_set_update_proc(s_battery_layer, battery_layer_update_proc);
   layer_add_child(root, s_battery_layer);

   update_clock();
}

static void main_window_unload(Window *window) {
   if (s_weather_hide_timer) { app_timer_cancel(s_weather_hide_timer); s_weather_hide_timer = NULL; }
   if (s_bg_bitmap) gbitmap_destroy(s_bg_bitmap);
   bitmap_layer_destroy(s_bg_layer);
   for (int i = 0; i < 4; i++) {
       bitmap_layer_destroy(s_digit_layers[i]);
       if (s_digit_bitmaps[i]) gbitmap_destroy(s_digit_bitmaps[i]);
   }
   text_layer_destroy(s_date_layer);
   text_layer_destroy(s_steps_layer);
   text_layer_destroy(s_weather_layer);
   layer_destroy(s_battery_layer);
}

/* ── Timer Window ────────────────────────────────────────────────── */
static void timer_window_load(Window *window) {
   Layer *root = window_get_root_layer(window);
   window_set_background_color(window, GColorWhite);
   s_timer_bg_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMG_BG2);
   s_timer_bg_layer = bitmap_layer_create(GRect(0, 0, 200, 228));
   bitmap_layer_set_bitmap(s_timer_bg_layer, s_timer_bg_bitmap);
   layer_add_child(root, bitmap_layer_get_layer(s_timer_bg_layer));
   for (int i = 0; i < 4; i++) {
       s_timer_digit_layers[i] = bitmap_layer_create(TIMER_DIGIT_FRAMES[i]);
       bitmap_layer_set_compositing_mode(s_timer_digit_layers[i], GCompOpSet);
       bitmap_layer_set_background_color(s_timer_digit_layers[i], GColorClear);
       layer_add_child(root, bitmap_layer_get_layer(s_timer_digit_layers[i]));
   }
   s_timer_hint_layer = text_layer_create(GRect(0, 40, 200, 40));
   text_layer_set_background_color(s_timer_hint_layer, GColorClear);
   text_layer_set_text_color(s_timer_hint_layer, GColorBlack);
   text_layer_set_font(s_timer_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
   text_layer_set_text_alignment(s_timer_hint_layer, GTextAlignmentCenter);
   text_layer_set_text(s_timer_hint_layer, "Touch: +/- 1m | Mid: Start");
   layer_add_child(root, text_layer_get_layer(s_timer_hint_layer));
   update_timer_digits();
}

static void timer_window_unload(Window *window) {
   if (s_timer_bg_bitmap) gbitmap_destroy(s_timer_bg_bitmap);
   bitmap_layer_destroy(s_timer_bg_layer);
   for (int i = 0; i < 4; i++) {
       bitmap_layer_destroy(s_timer_digit_layers[i]);
       if (s_timer_digit_bitmaps[i]) gbitmap_destroy(s_timer_digit_bitmaps[i]);
   }
   text_layer_destroy(s_timer_hint_layer);
}

/* ── Touch ───────────────────────────────────────────────────────── */
static void touch_handler(const TouchEvent *ev, void *ctx) {
   if (ev->type == TouchEvent_Touchdown) {
       Window *top = window_stack_get_top_window();
       if (top == s_main_window) {
           // Wetter anfordern & anzeigen; Timer-Window erst bei zweitem Tap?
           // Hier: Wetter anzeigen, kein Window-Wechsel beim ersten Tap
           request_weather();
           // Falls bereits Daten vorhanden → sofort zeigen
           show_weather();
           // Wechsel zum Timer nur wenn Wetter gerade sichtbar war (zweiter Tap)
           // oder direkt: uncomment die nächste Zeile und lösche die obigen drei
           // window_stack_push(s_timer_window, true);
       } else if (top == s_timer_window) {
           if (s_timer_hint_layer) layer_set_hidden(text_layer_get_layer(s_timer_hint_layer), true);
           if (s_alarm_ringing) { hal_play_alarm(false); s_alarm_ringing = false; }
           else if (ev->y < 70) { s_timer_seconds += 60; update_timer_digits(); }
           else if (ev->y > 140) { if (s_timer_seconds >= 60) s_timer_seconds -= 60; update_timer_digits(); }
           else { s_timer_running = !s_timer_running; }
       }
   }
}

/* ── Tick ────────────────────────────────────────────────────────── */
static void tick_handler(struct tm *t, TimeUnits u) {
   update_clock();
   if (s_timer_running) {
       s_timer_seconds--;
       if (s_timer_seconds <= 0) {
           s_timer_seconds = 0; s_timer_running = false;
           s_alarm_ringing = true; hal_play_alarm(true);
       }
       update_timer_digits();
   }
}

/* ── Init / Main ─────────────────────────────────────────────────── */
static void init() {
   // AppMessage initialisieren (Wetter)
   app_message_register_inbox_received(inbox_received_handler);
   app_message_open(64, 64);

   s_main_window = window_create();
   window_set_window_handlers(s_main_window, (WindowHandlers){.load=main_window_load, .unload=main_window_unload});
   s_timer_window = window_create();
   window_set_window_handlers(s_timer_window, (WindowHandlers){.load=timer_window_load, .unload=timer_window_unload});

   battery_state_service_subscribe(battery_handler);
   health_service_events_subscribe(health_handler, NULL);

   #if defined(PBL_TOUCH)
   touch_service_subscribe(touch_handler, NULL);
   #endif
   tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
   window_stack_push(s_main_window, true);
   s_battery_level = battery_state_service_peek().charge_percent;

   // Beim Start einmal Wetter anfragen
   request_weather();
}

int main(void) { init(); app_event_loop(); return 0; }
