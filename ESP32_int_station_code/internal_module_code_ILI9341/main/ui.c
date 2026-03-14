#include "ui.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "driver/ledc.h"
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lvgl_port.h"
// Dołącz swój nagłówek WiFi (żeby widzieć wifi_list, wifi_count itp.)
#include "wifi_project.h" 

static const char *TAG_UI = "UI_MODULE";

// --- MAKRA IKON ---
#define MY_SYM_TEMP     "\xEF\x8B\x8B"
#define MY_SYM_HUM      "\xEF\x81\x83"
#define MY_SYM_PRESS    "\xEF\x98\xA4"
#define MY_SYM_BAT_100  "\xEF\x89\x80"
#define MY_SYM_BAT_50   "\xEF\x89\x82"
#define MY_SYM_BAT_0    "\xEF\x89\x84"

#define SYM_W_SUN        "\xEF\x86\x85" 
#define SYM_W_CLOUD      "\xEF\x83\x82" 
#define SYM_W_CLOUD_SUN  "\xEF\x9B\x84" 
#define SYM_W_RAIN       "\xEF\x9D\x80" 
#define SYM_W_SNOW       "\xEF\x8B\x9C" 
#define SYM_W_STORM      "\xEF\x83\xA7" 

// --- DEFINICJE ZMIENNYCH GLOBALNYCH  ---
lv_obj_t * main_screen = NULL;
lv_obj_t * settings_screen = NULL;
lv_obj_t * graph_screen = NULL;
lv_obj_t * weather_info_screen = NULL;

lv_obj_t * lbl_temp = NULL;
lv_obj_t * lbl_hum = NULL;
lv_obj_t * lbl_press = NULL;
lv_obj_t * lbl_time = NULL;
lv_obj_t * lbl_date = NULL;

lv_obj_t * icon_wifi = NULL;
lv_obj_t * icon_wifi_err = NULL;
lv_obj_t * icon_battery = NULL;

lv_obj_t * chart_temp = NULL;
lv_chart_series_t * ser_temp = NULL;
lv_obj_t * chart_hum = NULL;
lv_chart_series_t * ser_hum = NULL;
lv_obj_t * chart_press = NULL;
lv_chart_series_t * ser_press = NULL;

lv_obj_t * forecast_day_labels[7];
lv_obj_t * forecast_icon_labels[7];
lv_obj_t * forecast_temp_labels[7];
lv_obj_t * lbl_weather_city = NULL;

// --- ZMIENNE PRYWATNE UI (Tylko w tym pliku) ---
static lv_style_t style_btn;

// Zmienne Menu
static lv_obj_t * reset_settings_screen = NULL;
static lv_obj_t * btn_reset_confirm = NULL;

// Zmienne WiFi
static lv_obj_t * wifi_screen = NULL;
static lv_obj_t * btn_refresh_ptr = NULL;
static lv_obj_t * scan_spinner_ptr = NULL;
static lv_obj_t * wifi_list_cont = NULL;
static lv_obj_t * password_modal = NULL;
static lv_obj_t * ta_password = NULL;
static lv_obj_t * kb = NULL;
static char selected_ssid[33] = {0};

// Zmienne Czasu
static lv_obj_t * date_time_screen = NULL;
static lv_obj_t * roller_day;
static lv_obj_t * roller_month;
static lv_obj_t * roller_year;
static lv_obj_t * roller_hour;
static lv_obj_t * roller_minute;

// Zmienne Jasności
static lv_obj_t * bright_screen = NULL;
static lv_obj_t * slider_day;
static lv_obj_t * slider_night;
static lv_obj_t * lbl_val_day;
static lv_obj_t * lbl_val_night;

// Zmienne Lokalizacji
static lv_obj_t * loc_modal = NULL;
static lv_obj_t * ta_loc = NULL;

// Struktura WiFi
typedef struct {
    char ssid[33];
    char pass[64];
} wifi_cred_t;

bool is_scanning = false;

// --- PREDEKLARACJE (Żeby funkcje widziały się nawzajem) ---
static void back_event_to_main(lv_event_t * e);
static void settings_event_cb(lv_event_t * e);
void create_wifi_settings_ui(void);
static void open_password_modal(lv_event_t * e);
static void set_obj_opa(void * obj, int32_t v);
void start_wifi_blink(void);

// ==========================================================
// 1. WSPÓLNE SYMBOLE I STYLE
// ==========================================================
void init_button_styles(void) {
    lv_style_init(&style_btn);
    lv_style_set_radius(&style_btn, 15);
    lv_style_set_bg_opa(&style_btn, LV_OPA_COVER);
    lv_style_set_bg_color(&style_btn, lv_palette_lighten(LV_PALETTE_GREY, 3));
    lv_style_set_border_width(&style_btn, 0);
}

static void set_obj_opa(void * obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)obj, v, 0);
}

static void back_event_to_main(lv_event_t * e) {
    lv_scr_load(main_screen);
}

static void settings_event_cb(lv_event_t * e) {
    lv_scr_load(settings_screen);
}

// ==========================================================
// 2. MODUŁ WIFI I PASEK STATUSU
// ==========================================================
void update_wifi_icon(int8_t rssi, bool connected) {
    lv_color_t theme_text_color = lv_obj_get_style_text_color(lv_scr_act(), 0);

    if (connected) {
        lv_anim_del(icon_wifi, set_obj_opa);
        lv_obj_set_style_opa(icon_wifi, LV_OPA_COVER, 0);
        lv_obj_add_flag(icon_wifi_err, LV_OBJ_FLAG_HIDDEN);
        
        if (rssi > -55) {
            lv_label_set_text(icon_wifi, LV_SYMBOL_WIFI); 
            lv_obj_set_style_text_color(icon_wifi, theme_text_color, 0);
        } else if (rssi > -75) {
            lv_label_set_text(icon_wifi, LV_SYMBOL_WIFI); 
            lv_obj_set_style_text_color(icon_wifi, theme_text_color, 0);
        } else {
            lv_label_set_text(icon_wifi, LV_SYMBOL_WIFI);
            lv_obj_set_style_text_color(icon_wifi, lv_palette_main(LV_PALETTE_GREY), 0);
        }
    } else if (is_wifi_connecting) {
        if(lv_anim_get(icon_wifi, set_obj_opa) == NULL) start_wifi_blink();
        lv_obj_add_flag(icon_wifi_err, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(icon_wifi, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_label_set_text(icon_wifi, LV_SYMBOL_WIFI);
    } else {
        lv_anim_del(icon_wifi, set_obj_opa);
        lv_obj_set_style_opa(icon_wifi, LV_OPA_COVER, 0);
        lv_obj_clear_flag(icon_wifi_err, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(icon_wifi, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_label_set_text(icon_wifi, LV_SYMBOL_WIFI);
    }
}

void start_wifi_blink(void) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, icon_wifi);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_0);
    lv_anim_set_time(&a, 500);
    lv_anim_set_playback_time(&a, 500);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, set_obj_opa); 
    lv_anim_start(&a);
}

static void wifi_refresh_event_cb(lv_event_t * e) {
    if (is_scanning) return; 
    is_scanning = true; 
    
    if (btn_refresh_ptr != NULL) lv_obj_add_flag(btn_refresh_ptr, LV_OBJ_FLAG_HIDDEN);
    if (scan_spinner_ptr != NULL) lv_obj_clear_flag(scan_spinner_ptr, LV_OBJ_FLAG_HIDDEN);
    if (wifi_list_cont != NULL && lv_obj_is_valid(wifi_list_cont)) {
        lv_obj_clean(wifi_list_cont);
    }
    xTaskCreate(wifi_scan_task, "WIFI_SCAN", 8192, NULL, 5, NULL);
}

void refresh_wifi_list(lv_obj_t * list_cont) {
    lv_obj_clean(list_cont); 

    if (wifi_count == 0) {
        lv_obj_t * lbl = lv_label_create(list_cont);
        lv_label_set_text(lbl, "Nie znaleziono sieci.");
        return;
    }

    wifi_ap_record_t current_ap;
    bool is_connected = (esp_wifi_sta_get_ap_info(&current_ap) == ESP_OK);

    for(int i = 0; i < wifi_count; i++) {
        bool is_duplicate = false;
        for(int j = 0; j < i; j++) {
            if(strncmp((char *)wifi_list[i].ssid, (char *)wifi_list[j].ssid, 32) == 0) {
                is_duplicate = true;
                break;
            }
        }
        if(is_duplicate) continue; 

        lv_obj_t * btn = lv_btn_create(list_cont);
        lv_obj_set_size(btn, 280, 40);
        lv_obj_add_style(btn, &style_btn, 0); 
        
        lv_obj_t * l = lv_label_create(btn);
        lv_obj_t * icon_l = lv_label_create(btn);
        lv_obj_set_style_text_font(icon_l, &montserrat_pl_14, 0);
        lv_obj_align(icon_l, LV_ALIGN_RIGHT_MID, -10, 0); 
        
        bool is_this_net_connected = (is_connected && strncmp((char *)wifi_list[i].ssid, (char *)current_ap.ssid, 32) == 0);
        
        if (is_this_net_connected) {
            lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_BLUE), 0);
            lv_label_set_text_fmt(l, "%.32s", (char *)wifi_list[i].ssid); 
            lv_obj_set_style_text_color(l, lv_color_white(), 0); 
            lv_obj_set_style_text_color(icon_l, lv_color_white(), 0);
            lv_obj_set_style_text_font(l, &montserrat_pl_14, 0);
            lv_label_set_text(icon_l, LV_SYMBOL_WIFI); 
        } else {
            lv_label_set_text_fmt(l, "%.32s", (char *)wifi_list[i].ssid);
            lv_obj_set_style_text_font(l, &montserrat_pl_14, 0);
            lv_obj_set_style_text_color(l, lv_color_hex(0x000000), 0);
            lv_label_set_text(icon_l, LV_SYMBOL_WIFI); 
            int8_t rssi = wifi_list[i].rssi;
            
            if (rssi > -55) lv_obj_set_style_text_color(icon_l, lv_color_hex(0x000000), 0);
            else if (rssi > -75) lv_obj_set_style_text_color(icon_l, lv_palette_main(LV_PALETTE_GREY), 0);
            else lv_obj_set_style_text_color(icon_l, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
        }
        lv_obj_add_event_cb(btn, open_password_modal, LV_EVENT_CLICKED, (void *)wifi_list[i].ssid);
    }
    lv_obj_update_layout(list_cont);
}

static void close_modal_cb(lv_event_t * e) {
    if (password_modal != NULL) {
        lv_obj_del_async(password_modal); 
        password_modal = NULL; ta_password = NULL; kb = NULL;
    }
}

static void connect_btn_cb(lv_event_t * e) {
    if (ta_password == NULL) return; 
    const char * pwd = lv_textarea_get_text(ta_password);
    
    wifi_cred_t *creds = malloc(sizeof(wifi_cred_t));
    snprintf(creds->ssid, sizeof(creds->ssid), "%.32s", selected_ssid);
    snprintf(creds->pass, sizeof(creds->pass), "%.63s", pwd);

    close_modal_cb(NULL); // Zamknij modal

    if (btn_refresh_ptr != NULL) lv_obj_add_flag(btn_refresh_ptr, LV_OBJ_FLAG_HIDDEN);
    if (scan_spinner_ptr != NULL) lv_obj_clear_flag(scan_spinner_ptr, LV_OBJ_FLAG_HIDDEN);

    xTaskCreate(wifi_connect_task, "WIFI_CONN", 8192, creds, 5, NULL);
}

static void open_password_modal(lv_event_t * e) {
    if (password_modal != NULL) return;
    char * ssid = (char *)lv_event_get_user_data(e);
    snprintf(selected_ssid, sizeof(selected_ssid), "%.32s", ssid);

    password_modal = lv_obj_create(lv_scr_act());
    lv_obj_set_size(password_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(password_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(password_modal, LV_OPA_50, 0); 
    lv_obj_set_style_border_width(password_modal, 0, 0);
    lv_obj_set_style_pad_all(password_modal, 0, 0);
    lv_obj_add_flag(password_modal, LV_OBJ_FLAG_CLICKABLE); 

    lv_obj_t * panel = lv_obj_create(password_modal);
    lv_obj_set_size(panel, 300, 110);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * lbl_title = lv_label_create(panel);
    lv_label_set_text_fmt(lbl_title, "Hasło: %s", selected_ssid);
    lv_obj_set_style_text_font(lbl_title, &montserrat_pl_14, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, -5);

    ta_password = lv_textarea_create(panel);
    lv_obj_set_size(ta_password, 260, 40);
    lv_obj_align(ta_password, LV_ALIGN_TOP_MID, 0, 20);
    lv_textarea_set_password_mode(ta_password, true); 
    lv_textarea_set_one_line(ta_password, true);
    lv_textarea_set_max_length(ta_password, 63); 

    lv_obj_t * btn_cancel = lv_btn_create(panel);
    lv_obj_set_size(btn_cancel, 100, 30);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 10, 5);
    lv_obj_t * lbl_canc = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_canc, "Anuluj");
    lv_obj_center(lbl_canc);
    lv_obj_add_event_cb(btn_cancel, close_modal_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_connect = lv_btn_create(panel);
    lv_obj_set_size(btn_connect, 100, 30);
    lv_obj_align(btn_connect, LV_ALIGN_BOTTOM_RIGHT, -10, 5);
    lv_obj_set_style_bg_color(btn_connect, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_t * lbl_conn = lv_label_create(btn_connect);
    lv_label_set_text(lbl_conn, "Połącz");
    lv_obj_set_style_text_font(lbl_conn, &montserrat_pl_14, 0);
    lv_obj_center(lbl_conn);
    lv_obj_add_event_cb(btn_connect, connect_btn_cb, LV_EVENT_CLICKED, NULL);

    kb = lv_keyboard_create(password_modal);
    lv_obj_set_size(kb, 320, 120); 
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, ta_password); 
}

void create_wifi_settings_ui(void) {
    if (wifi_screen != NULL) { lv_scr_load(wifi_screen); return; }
    
    wifi_screen = lv_obj_create(NULL);
    lv_obj_t * lbl = lv_label_create(wifi_screen);
    lv_label_set_text(lbl, "WIFI: WYBIERZ SIEĆ");
    lv_obj_set_style_text_font(lbl, &montserrat_pl_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 10);

    btn_refresh_ptr = lv_btn_create(wifi_screen); 
    lv_obj_set_size(btn_refresh_ptr, 35, 35);
    lv_obj_align(btn_refresh_ptr, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_add_event_cb(btn_refresh_ptr, wifi_refresh_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_refresh = lv_label_create(btn_refresh_ptr);
    lv_label_set_text(lbl_refresh, LV_SYMBOL_REFRESH);
    lv_obj_center(lbl_refresh);

    scan_spinner_ptr = lv_spinner_create(wifi_screen, 1000, 60);
    lv_obj_set_size(scan_spinner_ptr, 35, 35);
    lv_obj_align(scan_spinner_ptr, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_add_flag(scan_spinner_ptr, LV_OBJ_FLAG_HIDDEN); 

    wifi_list_cont = lv_obj_create(wifi_screen);
    lv_obj_set_size(wifi_list_cont, 300, 150);
    lv_obj_align(wifi_list_cont, LV_ALIGN_TOP_MID, 0, 50); 
    lv_obj_set_flex_flow(wifi_list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wifi_list_cont, 10, 0);
    lv_obj_set_scroll_dir(wifi_list_cont, LV_DIR_VER);

    lv_obj_t * btn_back = lv_btn_create(wifi_screen);
    lv_obj_set_size(btn_back, 80, 30);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_event_cb(btn_back, settings_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "WRÓĆ");
    lv_obj_set_style_text_font(lbl_back, &montserrat_pl_14, 0);
    lv_obj_center(lbl_back);

    lv_scr_load(wifi_screen);
}

static void wifi_settings(lv_event_t * e) {
    if (is_wifi_connecting) { ESP_LOGW(TAG_UI, "Blokada skanowania"); return; }
    create_wifi_settings_ui();
    if (is_scanning) return; 
    
    is_scanning = true;
    if (btn_refresh_ptr != NULL) lv_obj_add_flag(btn_refresh_ptr, LV_OBJ_FLAG_HIDDEN);
    if (scan_spinner_ptr != NULL) lv_obj_clear_flag(scan_spinner_ptr, LV_OBJ_FLAG_HIDDEN);
    if (wifi_list_cont != NULL && lv_obj_is_valid(wifi_list_cont)) lv_obj_clean(wifi_list_cont);

    xTaskCreate(wifi_scan_task, "WIFI_SCAN", 8192, NULL, 5, NULL);
}


// ==========================================================
// 3. MENU USTAWIEŃ I RESET
// ==========================================================
static lv_obj_t * create_menu_btn(lv_obj_t * parent, const char * text, lv_style_t * style) {
    lv_obj_t * btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 280, 35); 
    lv_obj_add_style(btn, style, 0); 
    lv_obj_t * l = lv_label_create(btn);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &montserrat_pl_14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x000000), LV_STATE_DEFAULT);
    lv_obj_center(l);
    return btn;
}

static void confirm_reset_cb(lv_event_t * e) {
    ESP_LOGW(TAG_UI, "Rozpoczynam twardy reset do ustawien fabrycznych...");
    nvs_flash_erase(); 
    vTaskDelay(pdMS_TO_TICKS(1000)); 
    esp_restart(); 
}

void create_reset_ui(void) {
    if (reset_settings_screen != NULL) { lv_scr_load(reset_settings_screen); return; }
    
    reset_settings_screen = lv_obj_create(NULL);
    lv_obj_t * title = lv_label_create(reset_settings_screen);
    lv_label_set_text(title, "UWAGA!");
    lv_obj_set_style_text_font(title, &montserrat_pl_14, 0);
    lv_obj_set_style_text_color(title, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t * info = lv_label_create(reset_settings_screen);
    lv_label_set_text(info, "Ta operacja usunie zapisane\nsieci Wi-Fi oraz przywróci\ndomyślne ustawienia jasności.\n\nAutomatyczny restart.");
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_text_font(info, &montserrat_pl_14, 0);

    lv_obj_t * btn_back = lv_btn_create(reset_settings_screen);
    lv_obj_set_size(btn_back, 110, 40);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 20, -15);
    lv_obj_add_event_cb(btn_back, settings_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "Anuluj");
    lv_obj_center(lbl_back);

    btn_reset_confirm = lv_btn_create(reset_settings_screen);
    lv_obj_set_size(btn_reset_confirm, 110, 40);
    lv_obj_align(btn_reset_confirm, LV_ALIGN_BOTTOM_RIGHT, -20, -15);
    lv_obj_set_style_bg_color(btn_reset_confirm, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_add_event_cb(btn_reset_confirm, confirm_reset_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_reset = lv_label_create(btn_reset_confirm);
    lv_label_set_text(lbl_reset, "RESETUJ");
    lv_obj_center(lbl_reset);

    lv_scr_load(reset_settings_screen);
}

static void reset_settings_cb(lv_event_t * e) { create_reset_ui(); }


// ==========================================================
// 4. DATA I CZAS (Z BĘBNAMI)
// ==========================================================
static void generate_roller_opts(char * buf, int start, int end) {
    buf[0] = '\0';
    for (int i = start; i <= end; i++) {
        char tmp[8];
        if (i == end) sprintf(tmp, "%02d", i);
        else sprintf(tmp, "%02d\n", i);
        strcat(buf, tmp);
    }
}

static void save_time_cb(lv_event_t * e) {
    struct tm timeinfo = {0};
    timeinfo.tm_mday = lv_roller_get_selected(roller_day) + 1;
    timeinfo.tm_mon  = lv_roller_get_selected(roller_month); 
    timeinfo.tm_year = lv_roller_get_selected(roller_year) + (2024 - 1900); 
    timeinfo.tm_hour = lv_roller_get_selected(roller_hour);
    timeinfo.tm_min  = lv_roller_get_selected(roller_minute);
    timeinfo.tm_sec  = 0;

    time_t t = mktime(&timeinfo);
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    lv_scr_load(settings_screen);
}

void create_date_time_ui(void) {
    if (date_time_screen != NULL) { lv_scr_load(date_time_screen); return; }
    
    date_time_screen = lv_obj_create(NULL);
    lv_obj_t * title = lv_label_create(date_time_screen);
    lv_label_set_text(title, "USTAW CZAS I DATE");
    lv_obj_set_style_text_font(title, &montserrat_pl_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    char opts_31[100], opts_12[40], opts_years[100], opts_24[80], opts_60[200];
    generate_roller_opts(opts_31, 1, 31);
    generate_roller_opts(opts_12, 1, 12);
    generate_roller_opts(opts_years, 2024, 2040); 
    generate_roller_opts(opts_24, 0, 23);
    generate_roller_opts(opts_60, 0, 59);

    lv_obj_t * date_cont = lv_obj_create(date_time_screen);
    lv_obj_set_size(date_cont, 280, 75);
    lv_obj_clear_flag(date_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(date_cont, LV_ALIGN_TOP_MID, 0, 35);
    lv_obj_set_style_bg_opa(date_cont, 0, 0);
    lv_obj_set_style_border_opa(date_cont, 0, 0);
    lv_obj_set_flex_flow(date_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(date_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    roller_day = lv_roller_create(date_cont);
    lv_roller_set_options(roller_day, opts_31, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_day, 2); lv_obj_set_width(roller_day, 60);

    roller_month = lv_roller_create(date_cont);
    lv_roller_set_options(roller_month, opts_12, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_month, 2); lv_obj_set_width(roller_month, 60);

    roller_year = lv_roller_create(date_cont);
    lv_roller_set_options(roller_year, opts_years, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_year, 2); lv_obj_set_width(roller_year, 80);

    lv_obj_t * time_cont = lv_obj_create(date_time_screen);
    lv_obj_set_size(time_cont, 200, 75);
    lv_obj_clear_flag(time_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(time_cont, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_set_style_bg_opa(time_cont, 0, 0);
    lv_obj_set_style_border_opa(time_cont, 0, 0);
    lv_obj_set_flex_flow(time_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(time_cont, 20, 0);

    roller_hour = lv_roller_create(time_cont);
    lv_roller_set_options(roller_hour, opts_24, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_hour, 2); lv_obj_set_width(roller_hour, 60);

    lv_obj_t * colon = lv_label_create(time_cont);
    lv_label_set_text(colon, ":");

    roller_minute = lv_roller_create(time_cont);
    lv_roller_set_options(roller_minute, opts_60, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_minute, 2); lv_obj_set_width(roller_minute, 60);

    lv_obj_t * btn_back = lv_btn_create(date_time_screen);
    lv_obj_set_size(btn_back, 100, 35);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 20, -10);
    lv_obj_add_event_cb(btn_back, settings_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "Anuluj"); lv_obj_center(lbl_back);

    lv_obj_t * btn_save = lv_btn_create(date_time_screen);
    lv_obj_set_size(btn_save, 100, 35);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_RIGHT, -20, -10);
    lv_obj_set_style_bg_color(btn_save, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_add_event_cb(btn_save, save_time_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, "Zapisz"); lv_obj_center(lbl_save);

    time_t now; struct tm timeinfo;
    time(&now); localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year > (2020 - 1900)) {
        lv_roller_set_selected(roller_day, timeinfo.tm_mday - 1, LV_ANIM_OFF);
        lv_roller_set_selected(roller_month, timeinfo.tm_mon, LV_ANIM_OFF);
        lv_roller_set_selected(roller_year, timeinfo.tm_year - (2024 - 1900), LV_ANIM_OFF);
        lv_roller_set_selected(roller_hour, timeinfo.tm_hour, LV_ANIM_OFF);
        lv_roller_set_selected(roller_minute, timeinfo.tm_min, LV_ANIM_OFF);
    }
    lv_scr_load(date_time_screen);
}

static void date_time_settings_cb(lv_event_t * e) { create_date_time_ui(); }


// ==========================================================
// 5. JASNOŚĆ
// ==========================================================
static void slider_event_cb(lv_event_t * e) {
    lv_obj_t * slider = lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    if(slider == slider_day) lv_label_set_text_fmt(lbl_val_day, "%d%%", val);
    else if(slider == slider_night) lv_label_set_text_fmt(lbl_val_night, "%d%%", val);
}

static void save_bright_cb(lv_event_t * e) {
    day_brightness = lv_slider_get_value(slider_day);
    night_brightness = lv_slider_get_value(slider_night);
    save_brightness_settings(day_brightness, night_brightness);
    
    time_t now; struct tm timeinfo;
    time(&now); localtime_r(&now, &timeinfo);
    bool is_night = false;
    if (timeinfo.tm_year > (2020 - 1900)) is_night = (timeinfo.tm_hour >= 22 || timeinfo.tm_hour < 8);
    
    uint32_t duty = is_night ? ((night_brightness * 255) / 100) : ((day_brightness * 255) / 100);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

    lv_scr_load(settings_screen);
}

void create_brightness_ui(void) {
    if (bright_screen != NULL) { lv_scr_load(bright_screen); return; }
    
    bright_screen = lv_obj_create(NULL);
    lv_obj_t * title = lv_label_create(bright_screen);
    lv_label_set_text(title, "USTAWIENIA JASNOŚCI");
    lv_obj_set_style_text_font(title, &montserrat_pl_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t * lbl_day = lv_label_create(bright_screen);
    lv_label_set_text(lbl_day, "Dzień (08:00 - 22:00)");
    lv_obj_align(lbl_day, LV_ALIGN_TOP_LEFT, 20, 45);
    lv_obj_set_style_text_font(lbl_day, &montserrat_pl_14, 0);

    slider_day = lv_slider_create(bright_screen);
    lv_obj_set_size(slider_day, 220, 15);
    lv_obj_align(slider_day, LV_ALIGN_TOP_LEFT, 20, 65);
    lv_slider_set_range(slider_day, 10, 100); 
    lv_slider_set_value(slider_day, day_brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_day, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lbl_val_day = lv_label_create(bright_screen);
    lv_label_set_text_fmt(lbl_val_day, "%d%%", (int)day_brightness);
    lv_obj_align(lbl_val_day, LV_ALIGN_TOP_LEFT, 255, 63);

    lv_obj_t * lbl_night = lv_label_create(bright_screen);
    lv_label_set_text(lbl_night, "Noc (22:00 - 08:00)");
    lv_obj_align(lbl_night, LV_ALIGN_TOP_LEFT, 20, 105);
    lv_obj_set_style_text_font(lbl_night, &montserrat_pl_14, 0);

    slider_night = lv_slider_create(bright_screen);
    lv_obj_set_size(slider_night, 220, 15);
    lv_obj_align(slider_night, LV_ALIGN_TOP_LEFT, 20, 125);
    lv_slider_set_range(slider_night, 10, 100); 
    lv_slider_set_value(slider_night, night_brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_night, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lbl_val_night = lv_label_create(bright_screen);
    lv_label_set_text_fmt(lbl_val_night, "%d%%", (int)night_brightness);
    lv_obj_align(lbl_val_night, LV_ALIGN_TOP_LEFT, 255, 123);

    lv_obj_t * btn_back = lv_btn_create(bright_screen);
    lv_obj_set_size(btn_back, 100, 35);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 20, -10);
    lv_obj_add_event_cb(btn_back, settings_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "Anuluj"); lv_obj_center(lbl_back);

    lv_obj_t * btn_save = lv_btn_create(bright_screen);
    lv_obj_set_size(btn_save, 100, 35);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_RIGHT, -20, -10);
    lv_obj_set_style_bg_color(btn_save, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_add_event_cb(btn_save, save_bright_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, "Zapisz"); lv_obj_center(lbl_save);

    lv_scr_load(bright_screen);
}

static void bright_settings_cb(lv_event_t * e) { create_brightness_ui(); }


// ==========================================================
// 6. LOKALIZACJA POGODY
// ==========================================================
static void close_loc_modal_cb(lv_event_t * e) {
    if (loc_modal != NULL) {
        lv_obj_del_async(loc_modal); 
        loc_modal = NULL; ta_loc = NULL;
    }
}

static void save_loc_btn_cb(lv_event_t * e) {
    if (ta_loc == NULL) return; 
    const char * city = lv_textarea_get_text(ta_loc);
    snprintf(current_city, sizeof(current_city), "%s", city); 
    save_location_settings(current_city); 
    force_weather_update = true; 
    
    if (lbl_weather_city != NULL) {
        lv_label_set_text_fmt(lbl_weather_city, "PROGNOZA POGODY DLA: %s", current_city);
    }
    close_loc_modal_cb(NULL);
}

static void location_settings_cb(lv_event_t * e) {
    if (loc_modal != NULL) return;
    loc_modal = lv_obj_create(lv_scr_act());
    lv_obj_set_size(loc_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(loc_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(loc_modal, LV_OPA_50, 0); 
    lv_obj_set_style_border_width(loc_modal, 0, 0);
    lv_obj_set_style_pad_all(loc_modal, 0, 0);
    lv_obj_add_flag(loc_modal, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * panel = lv_obj_create(loc_modal);
    lv_obj_set_size(panel, 300, 110);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 5);

    lv_obj_t * lbl_title = lv_label_create(panel);
    lv_label_set_text(lbl_title, "Wpisz miejscowość:");
    lv_obj_set_style_text_font(lbl_title, &montserrat_pl_14, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, -5);

    ta_loc = lv_textarea_create(panel);
    lv_obj_set_size(ta_loc, 260, 40);
    lv_obj_align(ta_loc, LV_ALIGN_TOP_MID, 0, 20);
    lv_textarea_set_one_line(ta_loc, true);
    lv_textarea_set_text(ta_loc, current_city); 

    lv_obj_t * btn_cancel = lv_btn_create(panel);
    lv_obj_set_size(btn_cancel, 100, 30);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 10, 5);
    lv_obj_t * lbl_canc = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_canc, "Anuluj"); lv_obj_center(lbl_canc);
    lv_obj_add_event_cb(btn_cancel, close_loc_modal_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_save = lv_btn_create(panel);
    lv_obj_set_size(btn_save, 100, 30);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_RIGHT, -10, 5);
    lv_obj_set_style_bg_color(btn_save, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_t * lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, "Zapisz"); lv_obj_center(lbl_save);
    lv_obj_add_event_cb(btn_save, save_loc_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * kb_city = lv_keyboard_create(loc_modal);
    lv_obj_set_size(kb_city, 320, 120);
    lv_obj_align(kb_city, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb_city, ta_loc);
}

// ==========================================================
// 7. GŁÓWNE EKRANY (WYKRESY, POGODA, USTAWIENIA)
// ==========================================================
void create_settings_ui(void) {
    if (settings_screen != NULL) return; 
    settings_screen = lv_obj_create(NULL);

    lv_obj_t * title = lv_label_create(settings_screen);
    lv_label_set_text(title, "USTAWIENIA");
    lv_obj_set_style_text_font(title, &montserrat_pl_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    lv_obj_t * menu_cont = lv_obj_create(settings_screen);
    lv_obj_set_size(menu_cont, 300, 160);
    lv_obj_align(menu_cont, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_flex_flow(menu_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(menu_cont, 10, 0); 
    lv_obj_add_flag(menu_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(menu_cont, LV_DIR_VER);

    lv_obj_t * btn_wifi = create_menu_btn(menu_cont, "Ustawienia WiFi", &style_btn);
    lv_obj_add_event_cb(btn_wifi, wifi_settings, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_date = create_menu_btn(menu_cont, "Data i Czas", &style_btn);
    lv_obj_add_event_cb(btn_date, date_time_settings_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_bright = create_menu_btn(menu_cont, "Ustawienia Jasności", &style_btn);
    lv_obj_add_event_cb(btn_bright, bright_settings_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t * btn_reset = create_menu_btn(menu_cont, "Reset do Ustawień Fabrycznych", &style_btn);
    lv_obj_add_event_cb(btn_reset, reset_settings_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_loc = create_menu_btn(menu_cont, "Lokalizacja Prognozy Pogody", &style_btn);
    lv_obj_add_event_cb(btn_loc, location_settings_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_back = lv_btn_create(settings_screen);
    lv_obj_set_size(btn_back, 80, 30);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 10, 5);
    lv_obj_add_event_cb(btn_back, back_event_to_main, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "WRÓĆ"); lv_obj_center(lbl_back);
    lv_obj_set_style_text_font(lbl_back, &montserrat_pl_14, 0);
}

static void chart_x_axis_cb(lv_event_t * e) {
    lv_obj_draw_part_dsc_t * dsc = lv_event_get_draw_part_dsc(e);
    if(dsc->part == LV_PART_TICKS && dsc->id == LV_CHART_AXIS_PRIMARY_X) {
        int hour = dsc->value * 6; 
        if (hour >= 24) hour = 0; 
        lv_snprintf(dsc->text, dsc->text_length, "%02d:00", hour);
    }
}

void create_graph_screen(void) {
    graph_screen = lv_obj_create(NULL);
    lv_obj_t * lbl = lv_label_create(graph_screen);
    lv_label_set_text(lbl, "WYKRESY (OSTATNIA DOBA)");
    lv_obj_set_style_text_font(lbl, &montserrat_pl_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 5);

    lv_obj_t * btn_back = lv_btn_create(graph_screen);
    lv_obj_set_size(btn_back, 80, 30);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -5);
    lv_obj_add_event_cb(btn_back, back_event_to_main, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "WRÓĆ"); lv_obj_center(lbl_back);
    lv_obj_set_style_text_font(lbl_back, &montserrat_pl_14, 0);

    lv_obj_t * chart_cont = lv_obj_create(graph_screen);
    lv_obj_set_size(chart_cont, 320, 170); 
    lv_obj_align(chart_cont, LV_ALIGN_TOP_MID, 0, 30); 
    lv_obj_set_flex_flow(chart_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(chart_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(chart_cont, 5, 0); 
    lv_obj_set_style_bg_opa(chart_cont, 0, 0);
    lv_obj_set_style_border_opa(chart_cont, 0, 0);
    lv_obj_set_scroll_dir(chart_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(chart_cont, LV_SCROLLBAR_MODE_OFF);

    // --- WYKRES 1: TEMP ---
    lv_obj_t * lbl_t = lv_label_create(chart_cont);
    lv_label_set_text(lbl_t, "Temperatura [°C]");
    lv_obj_set_style_text_font(lbl_t, &montserrat_pl_14, 0);
    lv_obj_set_style_pad_top(lbl_t, 30, 0);
    
    chart_temp = lv_chart_create(chart_cont);
    lv_obj_set_size(chart_temp, 240, 140);
    lv_obj_set_style_pad_left(chart_temp, 10, 0);   
    lv_obj_set_style_pad_bottom(chart_temp, 25, 0); 
    lv_obj_set_style_pad_right(chart_temp, 15, 0);  
    lv_obj_set_style_pad_top(chart_temp, 5, 0);    
    lv_chart_set_type(chart_temp, LV_CHART_TYPE_LINE);
    lv_obj_set_style_size(chart_temp, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(chart_temp, 3, LV_PART_ITEMS);
    lv_chart_set_range(chart_temp, LV_CHART_AXIS_PRIMARY_Y, -20, 50); 
    lv_chart_set_axis_tick(chart_temp, LV_CHART_AXIS_PRIMARY_Y, 5, 2, 8, 2, true, 45); 
    lv_chart_set_axis_tick(chart_temp, LV_CHART_AXIS_PRIMARY_X, 5, 2, 5, 1, true, 20);
    lv_chart_set_div_line_count(chart_temp, 8, 5);
    lv_chart_set_point_count(chart_temp, 96); 
    lv_obj_add_event_cb(chart_temp, chart_x_axis_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    ser_temp = lv_chart_add_series(chart_temp, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    
    // --- WYKRES 2: WILG ---
    lv_obj_t * lbl_h = lv_label_create(chart_cont);
    lv_label_set_text(lbl_h, "Wilgotność [%]");
    lv_obj_set_style_text_font(lbl_h, &montserrat_pl_14, 0);
    lv_obj_set_style_pad_top(lbl_h, 30, 0);
    
    chart_hum = lv_chart_create(chart_cont);
    lv_obj_set_size(chart_hum, 240, 140);
    lv_obj_set_style_pad_left(chart_hum, 10, 0);   
    lv_obj_set_style_pad_bottom(chart_hum, 25, 0); 
    lv_obj_set_style_pad_right(chart_hum, 15, 0);  
    lv_obj_set_style_pad_top(chart_hum, 5, 0);  
    lv_chart_set_type(chart_hum, LV_CHART_TYPE_LINE);
    lv_obj_set_style_size(chart_hum, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(chart_hum, 3, LV_PART_ITEMS);
    lv_chart_set_range(chart_hum, LV_CHART_AXIS_PRIMARY_Y, 0, 100); 
    lv_chart_set_axis_tick(chart_hum, LV_CHART_AXIS_PRIMARY_Y, 5, 2, 6, 2, true, 45);
    lv_chart_set_axis_tick(chart_hum, LV_CHART_AXIS_PRIMARY_X, 5, 2, 5, 1, true, 20);
    lv_chart_set_div_line_count(chart_hum, 6, 5);
    lv_chart_set_point_count(chart_hum, 96);
    lv_obj_add_event_cb(chart_hum, chart_x_axis_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    ser_hum = lv_chart_add_series(chart_hum, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);

    // --- WYKRES 3: CISN ---
    lv_obj_t * lbl_p = lv_label_create(chart_cont);
    lv_label_set_text(lbl_p, "Ciśnienie [hPa]");
    lv_obj_set_style_text_font(lbl_p, &montserrat_pl_14, 0);
    lv_obj_set_style_pad_top(lbl_p, 30, 0);
    
    chart_press = lv_chart_create(chart_cont);
    lv_obj_set_size(chart_press, 240, 140);
    lv_obj_set_style_pad_left(chart_press, 10, 0);   
    lv_obj_set_style_pad_bottom(chart_press, 25, 0); 
    lv_obj_set_style_pad_right(chart_press, 15, 0);  
    lv_obj_set_style_pad_top(chart_press, 5, 0);  
    lv_chart_set_type(chart_press, LV_CHART_TYPE_LINE);
    lv_obj_set_style_size(chart_press, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(chart_press, 3, LV_PART_ITEMS);
    lv_chart_set_range(chart_press, LV_CHART_AXIS_PRIMARY_Y, 950, 1050); 
    lv_chart_set_axis_tick(chart_press, LV_CHART_AXIS_PRIMARY_Y, 5, 2, 6, 2, true, 45);
    lv_chart_set_axis_tick(chart_press, LV_CHART_AXIS_PRIMARY_X, 5, 2, 5, 1, true, 20);
    lv_chart_set_div_line_count(chart_press, 6, 5);
    lv_chart_set_point_count(chart_press, 96); 
    lv_obj_add_event_cb(chart_press, chart_x_axis_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    ser_press = lv_chart_add_series(chart_press, lv_palette_main(LV_PALETTE_GREEN), LV_CHART_AXIS_PRIMARY_Y);
}

static void graph_event_cb(lv_event_t * e) { lv_scr_load(graph_screen); }

static void create_graph_button(lv_obj_t * scr) {
    static lv_style_t style_btn;
    lv_style_init(&style_btn);
    lv_style_set_radius(&style_btn, 25);
    lv_style_set_bg_color(&style_btn, lv_palette_main(LV_PALETTE_GREY));
    lv_style_set_bg_opa(&style_btn, LV_OPA_10);
    lv_style_set_border_width(&style_btn, 1);
    lv_style_set_border_color(&style_btn, lv_palette_main(LV_PALETTE_GREY));

    lv_obj_t * btn = lv_obj_create(scr);
    lv_obj_set_size(btn, 110, 50); 
    lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 135, -10);
    lv_obj_add_style(btn, &style_btn, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_40, LV_STATE_PRESSED);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE); 

    lv_obj_t * label = lv_label_create(btn);
    lv_label_set_text(label, "WYKRESY\nPOMIARÓW"); 
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0); 
    lv_obj_set_style_text_font(label, &montserrat_pl_14, 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, graph_event_cb, LV_EVENT_CLICKED, NULL);
}

void create_weather_info_screen(void) {
    weather_info_screen = lv_obj_create(NULL);
    lbl_weather_city = lv_label_create(weather_info_screen);
    lv_label_set_text_fmt(lbl_weather_city, "PROGNOZA POGODY DLA: %s", current_city); 
    lv_obj_set_style_text_font(lbl_weather_city, &montserrat_pl_14, 0);
    lv_obj_align(lbl_weather_city, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t * scroll_cont = lv_obj_create(weather_info_screen);
    lv_obj_set_size(scroll_cont, 320, 160); 
    lv_obj_align(scroll_cont, LV_ALIGN_TOP_MID, 0, 35);
    lv_obj_set_flex_flow(scroll_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_scrollbar_mode(scroll_cont, LV_SCROLLBAR_MODE_OFF); 
    lv_obj_set_scroll_dir(scroll_cont, LV_DIR_HOR); 
    lv_obj_add_flag(scroll_cont, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_bg_opa(scroll_cont, 0, 0);
    lv_obj_set_style_border_opa(scroll_cont, 0, 0);
    lv_obj_set_style_pad_all(scroll_cont, 10, 0);
    lv_obj_set_style_pad_column(scroll_cont, 15, 0); 

    const char * dummy_days[7] = {"DZIŚ", "JUTRO", "ŚRO", "CZW", "PIĄ", "SOB", "NIE"};

    for(int i = 0; i < 7; i++) {
        lv_obj_t * card = lv_obj_create(scroll_cont);
        lv_obj_set_size(card, 100, 140); 
        lv_obj_set_style_radius(card, 20, 0);
        lv_obj_set_style_bg_color(card, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_10, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_SNAPPABLE); 

        forecast_day_labels[i] = lv_label_create(card);
        lv_label_set_text(forecast_day_labels[i], dummy_days[i]);
        lv_obj_set_style_text_font(forecast_day_labels[i], &montserrat_pl_14, 0);

        forecast_icon_labels[i] = lv_label_create(card);
        lv_label_set_text(forecast_icon_labels[i], SYM_W_SUN); 
        lv_obj_set_style_text_font(forecast_icon_labels[i], &weather_icons, 0); 

        forecast_temp_labels[i] = lv_label_create(card);
        lv_label_set_text_fmt(forecast_temp_labels[i], "--°C\n--°C"); 
        lv_obj_set_style_text_align(forecast_temp_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(forecast_temp_labels[i], &montserrat_pl_14, 0);
    }
    
    lv_obj_t * btn_back = lv_btn_create(weather_info_screen);
    lv_obj_set_size(btn_back, 80, 30);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_event_cb(btn_back, back_event_to_main, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "WRÓĆ"); lv_obj_center(lbl_back);
    lv_obj_set_style_text_font(lbl_back, &montserrat_pl_14, 0);
}

static void weather_info_event_cb(lv_event_t * e) { lv_scr_load(weather_info_screen); }

static void create_weather_info_button(lv_obj_t * scr) {
    static lv_style_t style_btn;
    lv_style_init(&style_btn);
    lv_style_set_radius(&style_btn, 25);
    lv_style_set_bg_color(&style_btn, lv_palette_main(LV_PALETTE_GREY));
    lv_style_set_bg_opa(&style_btn, LV_OPA_10);
    lv_style_set_border_width(&style_btn, 1);
    lv_style_set_border_color(&style_btn, lv_palette_main(LV_PALETTE_GREY));

    lv_obj_t * btn = lv_obj_create(scr);
    lv_obj_set_size(btn, 110, 50); 
    lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_style(btn, &style_btn, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_40, LV_STATE_PRESSED);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE); 

    lv_obj_t * label = lv_label_create(btn);
    lv_label_set_text(label, "PROGNOZA\nPOGODY"); 
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0); 
    lv_obj_set_style_text_font(label, &montserrat_pl_14, 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, weather_info_event_cb, LV_EVENT_CLICKED, NULL);
}

static void create_status_bar(lv_obj_t * scr) {
    lv_obj_t * status_bar = lv_obj_create(scr);
    lv_obj_set_size(status_bar, 320, 50); 
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(status_bar, 0, 0);
    lv_obj_set_style_border_opa(status_bar, 0, 0);
    lv_obj_set_style_pad_all(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * clock_cont = lv_obj_create(status_bar);
    lv_obj_set_size(clock_cont, 110, 40); 
    lv_obj_align(clock_cont, LV_ALIGN_TOP_LEFT, 5, 5);
    lv_obj_set_style_radius(clock_cont, 10, 0);
    lv_obj_set_style_bg_opa(clock_cont, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(clock_cont, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_pad_all(clock_cont, 2, 0); 
    lv_obj_set_style_border_width(clock_cont, 1, 0);
    lv_obj_set_style_border_color(clock_cont, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_flex_flow(clock_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(clock_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(clock_cont, LV_OBJ_FLAG_SCROLLABLE);

    lbl_date = lv_label_create(clock_cont);
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_12, 0); 
    lv_label_set_text(lbl_date, "--.--.----");

    lbl_time = lv_label_create(clock_cont);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_16, 0); 
    lv_label_set_text(lbl_time, "--:--:--");

    lv_obj_move_foreground(status_bar);

    icon_wifi = lv_label_create(status_bar);
    lv_obj_align(icon_wifi, LV_ALIGN_TOP_RIGHT, -5, 5); 
    lv_label_set_text(icon_wifi, LV_SYMBOL_WIFI);     
    lv_obj_set_style_text_font(icon_wifi, &lv_font_montserrat_14, 0); 
    lv_obj_set_style_text_color(icon_wifi, lv_palette_main(LV_PALETTE_GREY), 0); 

    icon_wifi_err = lv_label_create(status_bar);
    lv_label_set_text(icon_wifi_err, LV_SYMBOL_CLOSE); 
    lv_obj_set_style_text_font(icon_wifi_err, &lv_font_montserrat_12, 0); 
    lv_obj_set_style_text_color(icon_wifi_err, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_align_to(icon_wifi_err, icon_wifi, LV_ALIGN_TOP_RIGHT,-5, 5);
    lv_obj_add_flag(icon_wifi_err, LV_OBJ_FLAG_HIDDEN);

    icon_battery = lv_label_create(status_bar);
    lv_obj_align(icon_battery, LV_ALIGN_TOP_RIGHT, -30, 5); 
    lv_label_set_text(icon_battery, MY_SYM_BAT_100); 
    lv_obj_set_style_text_font(icon_battery, &montserrat_pl_14, 0);
    lv_obj_set_style_text_color(icon_battery, lv_palette_main(LV_PALETTE_GREY), 0); 
}

static void create_menu_button(lv_obj_t * scr) {
    lv_obj_t * btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 50, 50);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_set_style_radius(btn, 25, 0); 
    lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);

    lv_obj_t * label = lv_label_create(btn);
    lv_label_set_text(label, LV_SYMBOL_SETTINGS); 
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0); 
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, settings_event_cb, LV_EVENT_CLICKED, NULL);
}

void create_weather_ui(void) {
    main_screen = lv_scr_act();
    lv_obj_t * scr = main_screen;

    lv_obj_t * logo = lv_img_create(scr);
    lv_img_set_src(logo, &put_logo);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 2);

    lv_obj_t * cont = lv_obj_create(scr);
    lv_obj_set_size(cont, 320, 120); 
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 55); 
    lv_obj_set_style_bg_opa(cont, 0, 0); 
    lv_obj_set_style_border_opa(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW); 
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cont, 8, 0); 
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    static lv_style_t style_card;
    lv_style_init(&style_card);
    lv_style_set_radius(&style_card, 30); 
    lv_style_set_bg_opa(&style_card, LV_OPA_10); 
    lv_style_set_bg_color(&style_card, lv_palette_main(LV_PALETTE_GREY));
    lv_style_set_border_width(&style_card, 1);
    lv_style_set_border_color(&style_card, lv_palette_main(LV_PALETTE_GREY));
    lv_style_set_pad_all(&style_card, 5);

    lv_obj_t * card_temp = lv_obj_create(cont);
    lv_obj_set_size(card_temp, 95, 100);
    lv_obj_add_style(card_temp, &style_card, 0);
    lv_obj_set_flex_flow(card_temp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card_temp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * icon_t = lv_label_create(card_temp);
    lv_obj_set_style_text_font(icon_t, &montserrat_pl_14, 0);
    lv_label_set_text(icon_t, MY_SYM_TEMP);
    
    lv_obj_t * title_temp = lv_label_create(card_temp);
    lv_label_set_text(title_temp, "TEMP");
    lv_obj_set_style_text_font(title_temp, &montserrat_pl_14, 0); 

    lbl_temp = lv_label_create(card_temp);
    lv_obj_set_style_text_font(lbl_temp, &montserrat_pl_14, 0); 
    lv_label_set_text(lbl_temp, "--.-");

    lv_obj_t * card_hum = lv_obj_create(cont);
    lv_obj_set_size(card_hum, 95, 100);
    lv_obj_add_style(card_hum, &style_card, 0);
    lv_obj_set_flex_flow(card_hum, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card_hum, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * icon_h = lv_label_create(card_hum);
    lv_obj_set_style_text_font(icon_h, &montserrat_pl_14, 0);
    lv_label_set_text(icon_h, MY_SYM_HUM);
    
    lv_obj_t * title_hum = lv_label_create(card_hum);
    lv_label_set_text(title_hum, "WILG");
    lv_obj_set_style_text_font(title_hum, &montserrat_pl_14, 0);

    lbl_hum = lv_label_create(card_hum);
    lv_obj_set_style_text_font(lbl_hum, &montserrat_pl_14, 0);
    lv_label_set_text(lbl_hum, "--%");

    lv_obj_t * card_press = lv_obj_create(cont);
    lv_obj_set_size(card_press, 95, 100);
    lv_obj_add_style(card_press, &style_card, 0);
    lv_obj_set_flex_flow(card_press, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card_press, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * icon_p = lv_label_create(card_press);
    lv_obj_set_style_text_font(icon_p, &montserrat_pl_14, 0);
    lv_label_set_text(icon_p, MY_SYM_PRESS);
    
    lv_obj_t * title_press = lv_label_create(card_press);
    lv_label_set_text(title_press, "CIŚN");
    lv_obj_set_style_text_font(title_press, &montserrat_pl_14, 0);

    lbl_press = lv_label_create(card_press);
    lv_obj_set_style_text_font(lbl_press, &montserrat_pl_14, 0);
    lv_label_set_text(lbl_press, "----");

    create_status_bar(scr); 
    create_menu_button(scr); 
    create_graph_button(scr); 
    create_weather_info_button(scr); 
}

// ==========================================================
// 8. TASKI DO OBSŁUGI EKRANU WIFI
// ==========================================================
void wifi_scan_task(void *pvParameters) {
    wifi_scan_networks(); // Główna funkcja z Twojego wifi_project.c
    
    lvgl_port_lock(-1);
    if (wifi_list_cont != NULL && lv_obj_is_valid(wifi_list_cont)) {
        refresh_wifi_list(wifi_list_cont);
    }
    
    // Ukrywamy kółko ładowania i pokazujemy przycisk odświeżania
    if (scan_spinner_ptr != NULL && lv_obj_is_valid(scan_spinner_ptr)) {
        lv_obj_add_flag(scan_spinner_ptr, LV_OBJ_FLAG_HIDDEN);
    }
    if (btn_refresh_ptr != NULL && lv_obj_is_valid(btn_refresh_ptr)) {
        lv_obj_clear_flag(btn_refresh_ptr, LV_OBJ_FLAG_HIDDEN); 
    }
    lvgl_port_unlock();

    is_scanning = false; // Zdejmujemy blokadę!
    vTaskDelete(NULL);
}

void wifi_connect_task(void *pvParameters) {
    wifi_cred_t *creds = (wifi_cred_t *)pvParameters;
    ESP_LOGI(TAG_UI, "Proba polaczenia z siecia: %s", creds->ssid);
    
    is_wifi_connecting = true; 
    bool success = wifi_connect_station(creds->ssid, creds->pass); 
    is_wifi_connecting = false; 

    // Czekamy chwilę, żeby sterownik WiFi ustabilizował połączenie
    vTaskDelay(pdMS_TO_TICKS(1500));

    lvgl_port_lock(-1);
    if (success) {
        ESP_LOGI(TAG_UI, "Polaczono pomyslnie!");
        save_wifi_credentials(creds->ssid, creds->pass); // Wywołuje funkcję z main.c
    } else {
        ESP_LOGE(TAG_UI, "Blad polaczenia z WiFi");
    }

    // Odświeżamy listę i gasimy spinner
    if (wifi_list_cont != NULL && lv_obj_is_valid(wifi_list_cont)) {
        refresh_wifi_list(wifi_list_cont);
    }
    if (scan_spinner_ptr != NULL && lv_obj_is_valid(scan_spinner_ptr)) {
        lv_obj_add_flag(scan_spinner_ptr, LV_OBJ_FLAG_HIDDEN);
    }
    if (btn_refresh_ptr != NULL && lv_obj_is_valid(btn_refresh_ptr)) {
        lv_obj_clear_flag(btn_refresh_ptr, LV_OBJ_FLAG_HIDDEN); 
    }
    lvgl_port_unlock();

    free(creds);
    vTaskDelete(NULL);
}