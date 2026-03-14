#pragma once

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

// --- ZMIENNE GLOBALNE UI (Eksportowane dla main.c) ---
extern lv_obj_t * main_screen;
extern lv_obj_t * settings_screen;
extern lv_obj_t * graph_screen;
extern lv_obj_t * weather_info_screen;

extern lv_obj_t * lbl_temp;
extern lv_obj_t * lbl_hum;
extern lv_obj_t * lbl_press;
extern lv_obj_t * lbl_time;
extern lv_obj_t * lbl_date;

extern lv_obj_t * icon_wifi;
extern lv_obj_t * icon_wifi_err;
extern lv_obj_t * icon_battery;

extern lv_obj_t * chart_temp;
extern lv_chart_series_t * ser_temp;
extern lv_obj_t * chart_hum;
extern lv_chart_series_t * ser_hum;
extern lv_obj_t * chart_press;
extern lv_chart_series_t * ser_press;

extern lv_obj_t * forecast_day_labels[7];
extern lv_obj_t * forecast_icon_labels[7];
extern lv_obj_t * forecast_temp_labels[7];
extern lv_obj_t * lbl_weather_city;

// --- ZMIENNE I OBIEKTY Z MAIN.C (Potrzebne w UI) ---
extern bool is_wifi_connecting;
extern uint32_t day_brightness;
extern uint32_t night_brightness;
extern char current_city[64];
extern bool force_weather_update;
LV_FONT_DECLARE(weather_icons);
LV_IMG_DECLARE(put_logo); 
LV_FONT_DECLARE(montserrat_pl_14); // Zakładam, że masz ją zadeklarowaną gdzieś globalnie

// --- FUNKCJE SYSTEMOWE Z MAIN.C (Wywoływane przez przyciski UI) ---
void save_brightness_settings(uint32_t day, uint32_t night);
void save_location_settings(const char * city);
void save_wifi_credentials(const char * ssid, const char * pass);
void wifi_scan_task(void *pvParameters);
void wifi_connect_task(void *pvParameters);

// --- GŁÓWNE FUNKCJE UI ---
void init_button_styles(void);
void create_weather_ui(void);
void create_settings_ui(void);
void create_graph_screen(void);
void create_weather_info_screen(void);
void update_wifi_icon(int8_t rssi, bool connected);