#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "nvs_flash.h"
#include "esp_wifi.h"

#include "lvgl.h"
#include "esp_lvgl_port.h"

#include "gpio_init.h"
#include "spi_init.h"
#include "uart_init.h"
#include "nrf.h"
#include "wifi_project.h"
#include "sntp.h"
#include "lcd.h"
#include "Config.h"
#include "touch.h"

static const char *TAG = "MAIN";

// Obiekty systemowe
spi_device_handle_t nrf_handle = NULL;
SemaphoreHandle_t xSpiMutex = NULL;
QueueHandle_t xSensorQueue = NULL;

LV_FONT_DECLARE(montserrat_pl_14);

#define MY_SYM_TEMP     "\xEF\x8B\x8B" // Termometr
#define MY_SYM_HUM      "\xEF\x81\x83" // Kropla
#define MY_SYM_PRESS    "\xEF\x98\xA4" // Ciśnienie

#define MY_SYM_BAT_100  "\xEF\x89\x80" // Pełna bateria
#define MY_SYM_BAT_50   "\xEF\x89\x82" // Połowa
#define MY_SYM_BAT_0    "\xEF\x89\x84" // Pusta

// Obiekty LVGL
static lv_obj_t * main_screen = NULL;
static lv_obj_t * settings_screen = NULL;
static lv_obj_t * wifi_screen = NULL;
static lv_obj_t * btn_wifi_list[10]; // Tablica przycisków sieci
static lv_obj_t * wifi_list_cont = NULL;
static lv_obj_t * btn_refresh_ptr = NULL;
static lv_obj_t * scan_spinner_ptr = NULL; // <--- DODAJ TO
static bool is_scanning = false; // <--- DODAJ TĘ FLAGĘ

lv_obj_t *lbl_temp, *lbl_hum, *lbl_press, *lbl_time, *lbl_date, *icon_wifi, *icon_wifi_err, *icon_battery;
bool is_wifi_connecting = false;

// Prototypy funkcji (aby uniknąć ostrzeżeń kompilatora)
void create_weather_ui(void);
void create_settings_ui(void);
void create_wifi_settings_ui(void);
void create_status_bar(lv_obj_t * scr);
void create_menu_button(lv_obj_t * scr);
static void btn_event_cb(lv_event_t * e);
static void wifi_settings(lv_event_t * e);
static void back_to_settings_cb(lv_event_t * e);
static void back_event_to_main(lv_event_t * e);
static void open_password_modal(lv_event_t * e);
void update_wifi_icon(int8_t rssi, bool connected);
void start_wifi_blink(void);
static void set_obj_opa(void * obj, int32_t v);
void refresh_wifi_list(lv_obj_t * list_cont);
static lv_style_t style_btn;

// --- INICJALIZACJA PODŚWIETLENIA (PWM) ---
void backlight_pwm_init(void) {
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_8_BIT, 
        .freq_hz          = 5000,             
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = BL_PIN,
        // POPRAWKA 1: Zmniejszono jasność startową do 100, żeby uniknąć restartów od spadku napięcia
        .duty           = 255,  
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

// Funkcja inicjalizująca styl (wywołaj ją raz w app_main lub przed użyciem)
void init_button_styles(void) {
    lv_style_init(&style_btn);
    lv_style_set_radius(&style_btn, 15);
    lv_style_set_bg_opa(&style_btn, LV_OPA_COVER);
    lv_style_set_bg_color(&style_btn, lv_palette_lighten(LV_PALETTE_GREY, 3));
    lv_style_set_border_width(&style_btn, 0);
    // Tekst ustawiamy wewnątrz przycisku, ale można też w stylu
}

// --- FUNKCJE DOTYCZĄCE WIFI --- //

void update_wifi_icon(int8_t rssi, bool connected) {
    // Kolor adaptacyjny: biały w nocy, czarny w dzień
    lv_color_t theme_text_color = lv_obj_get_style_text_color(lv_scr_act(), 0);

    if (connected) {
        lv_anim_del(icon_wifi, set_obj_opa);
        lv_obj_set_style_opa(icon_wifi, LV_OPA_COVER, 0);
        lv_obj_add_flag(icon_wifi_err, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(icon_wifi, theme_text_color, 0);
        
        // --- LOGIKA RSSI (Zależność kresek) ---
        if (rssi > -55) {
            // Najsilniejszy sygnał (3 kreski)
            lv_label_set_text(icon_wifi, LV_SYMBOL_WIFI); 
        } 
        else if (rssi > -75) {
            // Średni sygnał (2 kreski)
            // Używamy symbolu Volume Mid lub innego, jeśli LV_SYMBOL_WIFI jest tylko jeden
            // Jeśli Twoja czcionka nie ma innych wersji WiFi, można tu użyć:
            lv_label_set_text(icon_wifi, LV_SYMBOL_WIFI); 
            // Opcjonalnie: dla średniego sygnału lekko zmniejszamy jasność (np. szary)
            // lv_obj_set_style_text_color(icon_wifi, lv_palette_main(LV_PALETTE_GREY), 0);
        } 
        else {
            // Słaby sygnał (1 kreska / kropka)
            // Możesz tu wstawić symbol kropki "." lub LV_SYMBOL_AUDIO jeśli pasuje
            lv_label_set_text(icon_wifi, LV_SYMBOL_WIFI);
            // Dla słabego sygnału wymuszamy szary, żeby odróżnić od pełnego zasięgu
            lv_obj_set_style_text_color(icon_wifi, lv_palette_main(LV_PALETTE_GREY), 0);
        }
    } 
    else if (is_wifi_connecting) {
        if(lv_anim_get(icon_wifi, set_obj_opa) == NULL) start_wifi_blink();
        lv_obj_add_flag(icon_wifi_err, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(icon_wifi, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_label_set_text(icon_wifi, LV_SYMBOL_WIFI);
    } 
    else {
        lv_anim_del(icon_wifi, set_obj_opa);
        lv_obj_set_style_opa(icon_wifi, LV_OPA_COVER, 0);
        lv_obj_clear_flag(icon_wifi_err, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(icon_wifi, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_label_set_text(icon_wifi, LV_SYMBOL_WIFI);
    }
}

static void set_obj_opa(void * obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)obj, v, 0);
}

// ----- FUNKCJA ŁĄCZĄCA Z SIECIĄ WIFI (ZARZĄDZANIE ZDARZENIAMI) --- //

void start_wifi_blink(void) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, icon_wifi);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_0);
    lv_anim_set_time(&a, 500);
    lv_anim_set_playback_time(&a, 500);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, set_obj_opa); // Teraz kompilator już zna set_obj_opa
    lv_anim_start(&a);
}

// --- ODŚWIEŻANIE LISTY SIECI (np. po ponownym skanowaniu) --- //

void refresh_wifi_list(lv_obj_t * list_cont) {
    // 1. CZYSZCZENIE LISTY (Tylko tutaj jest to w 100% bezpieczne)

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
            // ZABEZPIECZENIE: strncmp czyta tylko max 32 bajty
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
        
        // ZABEZPIECZENIE: strncmp zamiast strcmp
        bool is_this_net_connected = (is_connected && strncmp((char *)wifi_list[i].ssid, (char *)current_ap.ssid, 32) == 0);
        
        if (is_this_net_connected) {
            lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_BLUE), 0);
            
            // ZABEZPIECZENIE: %.32s wymusza ograniczenie znaków
            lv_label_set_text_fmt(l, "%.32s", (char *)wifi_list[i].ssid); 
            
            lv_obj_set_style_text_color(l, lv_color_white(), 0); 
            lv_obj_set_style_text_color(icon_l, lv_color_white(), 0);
            lv_label_set_text(icon_l, LV_SYMBOL_WIFI); 
        } else {
            // ZABEZPIECZENIE: %.32s wymusza ograniczenie znaków
            lv_label_set_text_fmt(l, "%.32s", (char *)wifi_list[i].ssid);
            lv_obj_set_style_text_color(l, lv_color_hex(0x000000), 0);
            
            lv_label_set_text(icon_l, LV_SYMBOL_WIFI); 
            int8_t rssi = wifi_list[i].rssi;
            
            if (rssi > -55) {
                lv_obj_set_style_text_color(icon_l, lv_color_hex(0x000000), 0);
            } else if (rssi > -75) {
                lv_obj_set_style_text_color(icon_l, lv_palette_main(LV_PALETTE_GREY), 0);
            } else {
                lv_obj_set_style_text_color(icon_l, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
            }
        }
        
        lv_obj_add_event_cb(btn, open_password_modal, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_update_layout(list_cont);
}



// --- TASK DO SKANOWANIA SIECI (WYWOŁYWANY PO KLIKNIĘCIU "USTAWIENIA WIFI") --- //

void wifi_scan_task(void *pvParameters) {
    wifi_scan_networks(); // Blokujące skanowanie

    lvgl_port_lock(-1);
    
    // ZABEZPIECZENIE: Sprawdzamy czy lista w ogóle istnieje w pamięci
    if (wifi_list_cont != NULL && lv_obj_is_valid(wifi_list_cont)) {
        refresh_wifi_list(wifi_list_cont);
    }
    
    // UKRYJ spinner, POKAŻ przycisk
    if (scan_spinner_ptr != NULL && lv_obj_is_valid(scan_spinner_ptr)) {
        lv_obj_add_flag(scan_spinner_ptr, LV_OBJ_FLAG_HIDDEN);
    }
    if (btn_refresh_ptr != NULL && lv_obj_is_valid(btn_refresh_ptr)) {
        lv_obj_clear_flag(btn_refresh_ptr, LV_OBJ_FLAG_HIDDEN); 
    }
    
    lvgl_port_unlock();

    is_scanning = false; // Zdejmij blokadę
    vTaskDelete(NULL);
}

// --- ZDARZENIE PO KLIKNIĘCIU PRZYCISKU USTAWIEŃ WIFI --- //

static void wifi_refresh_event_cb(lv_event_t * e) {
    if (is_scanning) return; 
    is_scanning = true; 
    
    lvgl_port_lock(-1);
    if (btn_refresh_ptr != NULL) lv_obj_add_flag(btn_refresh_ptr, LV_OBJ_FLAG_HIDDEN);
    if (scan_spinner_ptr != NULL) lv_obj_clear_flag(scan_spinner_ptr, LV_OBJ_FLAG_HIDDEN);

    // TUTA JEST BEZPIECZNE CZYSZCZENIE (Wątek GUI usuwa stare sieci z ekranu)
    if (wifi_list_cont != NULL && lv_obj_is_valid(wifi_list_cont)) {
        lv_obj_clean(wifi_list_cont);
    }
    lvgl_port_unlock();

    xTaskCreate(wifi_scan_task, "WIFI_SCAN", 8192, NULL, 5, NULL);
}

// --- TWORZENIE PRZYCISKU MENU (FUNKCJA POMOCNICZA) --- //

static lv_obj_t * create_menu_btn(lv_obj_t * parent, const char * text, lv_style_t * style) {
    lv_obj_t * btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 280, 35); 
    lv_obj_add_style(btn, style, 0); // Dodajemy nasz jasnoszary styl
    
    lv_obj_t * l = lv_label_create(btn);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &montserrat_pl_14, 0);
    
    // Wymuszenie czarnego koloru tekstu
    lv_obj_set_style_text_color(l, lv_color_hex(0x000000), LV_STATE_DEFAULT);
    
    lv_obj_center(l);
    return btn;
}

// --- OBSŁUGA ZDARZEŃ (np. kliknięcie przycisku) --- //

static void btn_event_cb(lv_event_t * e) {
    lvgl_port_lock(-1);
    lv_scr_load(settings_screen);
    lvgl_port_unlock();
}

static void back_event_to_main(lv_event_t * e) {
    lvgl_port_lock(-1);
    lv_scr_load(main_screen);
    lvgl_port_unlock();
}

// Zdarzenie po kliknięciu przycisku sieci (na razie tylko loguje)
static void open_password_modal(lv_event_t * e) {
    ESP_LOGI(TAG, "Otwieranie okna hasła...");
}

// Zdarzenie powrotu do ekranu ustawień
static void back_to_settings_cb(lv_event_t * e) {
    lvgl_port_lock(-1);
    lv_scr_load(settings_screen);
    lvgl_port_unlock();
}

// --- ZDARZENIE PO KLIKNIĘCIU PRZYCISKU USTAWIEŃ WIFI --- //

static void wifi_settings(lv_event_t * e) {
    lvgl_port_lock(-1);
    create_wifi_settings_ui();
    lv_scr_load(wifi_screen);
    lvgl_port_unlock();

    if (is_scanning) {
        lvgl_port_lock(-1);
        if (btn_refresh_ptr != NULL) lv_obj_add_flag(btn_refresh_ptr, LV_OBJ_FLAG_HIDDEN);
        if (scan_spinner_ptr != NULL) lv_obj_clear_flag(scan_spinner_ptr, LV_OBJ_FLAG_HIDDEN);
        lvgl_port_unlock();
        return; 
    }
    
    is_scanning = true;

    lvgl_port_lock(-1);
    if (btn_refresh_ptr != NULL) lv_obj_add_flag(btn_refresh_ptr, LV_OBJ_FLAG_HIDDEN);
    if (scan_spinner_ptr != NULL) lv_obj_clear_flag(scan_spinner_ptr, LV_OBJ_FLAG_HIDDEN);

    // TUTA JEST BEZPIECZNE CZYSZCZENIE (Wątek GUI usuwa stare sieci z ekranu)
    if (wifi_list_cont != NULL && lv_obj_is_valid(wifi_list_cont)) {
        lv_obj_clean(wifi_list_cont);
    }
    lvgl_port_unlock();

    xTaskCreate(wifi_scan_task, "WIFI_SCAN", 8192, NULL, 5, NULL);
}

// 3. Gdy połączysz się z WiFi:
void set_wifi_connected(int index) {
    lvgl_port_lock(-1);
    lv_obj_set_style_bg_color(btn_wifi_list[index], lv_palette_main(LV_PALETTE_BLUE), 0);
    // ... dodaj label "Połączono" ...
    lvgl_port_unlock();
}


// --- TWORZENIE EKRANU WYBORU SIECI WIFI --- //

void create_wifi_settings_ui(void) {
    if (wifi_screen != NULL) {
        lv_scr_load(wifi_screen);
        return;
    }
    
    wifi_screen = lv_obj_create(NULL);
    
    // Tytuł
    lv_obj_t * lbl = lv_label_create(wifi_screen);
    lv_label_set_text(lbl, "WIFI: WYBIERZ SIEĆ");
    lv_obj_set_style_text_font(lbl, &montserrat_pl_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 10);

    // Przycisk odświeżania (JEDYNY, w prawym górnym rogu)
    // ... (Tytuł ekranu) ...

    // Przycisk odświeżania
    btn_refresh_ptr = lv_btn_create(wifi_screen); 
    lv_obj_set_size(btn_refresh_ptr, 35, 35);
    lv_obj_align(btn_refresh_ptr, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_add_flag(btn_refresh_ptr, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_style_pad_all(btn_refresh_ptr, 0, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn_refresh_ptr, wifi_refresh_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_refresh = lv_label_create(btn_refresh_ptr);
    lv_label_set_text(lbl_refresh, LV_SYMBOL_REFRESH);
    lv_obj_center(lbl_refresh);

    // --- NOWE: STWÓRZ SPINNERA RAZ I GO UKRYJ ---
    scan_spinner_ptr = lv_spinner_create(wifi_screen, 1000, 60);
    lv_obj_set_size(scan_spinner_ptr, 35, 35);
    lv_obj_align(scan_spinner_ptr, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_add_flag(scan_spinner_ptr, LV_OBJ_FLAG_HIDDEN); // Domyślnie ukryty

// ... (reszta kodu, czyli wifi_list_cont) ...

    // Kontener listy
    wifi_list_cont = lv_obj_create(wifi_screen);
    lv_obj_set_size(wifi_list_cont, 300, 150);
    lv_obj_align(wifi_list_cont, LV_ALIGN_TOP_MID, 0, 50); // Trochę niżej, żeby nie nachodziło na przycisk
    lv_obj_set_flex_flow(wifi_list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wifi_list_cont, 10, 0);
    lv_obj_add_flag(wifi_list_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(wifi_list_cont, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_dir(wifi_list_cont, LV_DIR_VER);

    // Przycisk POWRÓT
    lv_obj_t * btn_back = lv_btn_create(wifi_screen);
    lv_obj_set_size(btn_back, 80, 30);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_event_cb(btn_back, back_to_settings_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "WRÓĆ");
    lv_obj_set_style_text_font(lbl_back, &montserrat_pl_14, 0);
    lv_obj_center(lbl_back);
    
    lv_scr_load(wifi_screen);
}

// --- TWORZENIE EKRANU USTAWIEŃ --- //

void create_settings_ui(void) {
    if (settings_screen != NULL) return; // Zapobiega tworzeniu kopii
        
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
    lv_obj_clear_flag(menu_cont, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_dir(menu_cont, LV_DIR_VER);

    // Definicja stylu dla przycisków w menu
    static lv_style_t style_btn;
    lv_style_init(&style_btn);
    lv_style_set_radius(&style_btn, 15);
    lv_style_set_bg_opa(&style_btn, LV_OPA_COVER);
    lv_style_set_bg_color(&style_btn, lv_palette_lighten(LV_PALETTE_GREY, 3)); // Twój jasnoszary
    lv_style_set_border_width(&style_btn, 0);

    // Tworzenie przycisków (wszystkie używają jednego stylu)
    lv_obj_t * btn_wifi = create_menu_btn(menu_cont, "Ustawienia WiFi", &style_btn);
    lv_obj_add_event_cb(btn_wifi, wifi_settings, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_date = create_menu_btn(menu_cont, "Data i Czas", &style_btn);
    // lv_obj_add_event_cb(btn_date, date_time_settings, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_bright = create_menu_btn(menu_cont, "Ustawienia Jasności", &style_btn);
    
    lv_obj_t * btn_reset = create_menu_btn(menu_cont, "Reset do Ustawień Fabrycznych", &style_btn);

    // Przycisk powrotu
    lv_obj_t * btn_back = lv_btn_create(settings_screen);
    lv_obj_set_size(btn_back, 80, 30);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 10, 5);
    lv_obj_add_event_cb(btn_back, back_event_to_main, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "WRÓĆ");
    lv_obj_set_style_text_font(lbl_back, &montserrat_pl_14, 0);
    lv_obj_center(lbl_back);
}

// --- TWORZENIE PASKA STATUSU (Czas + WiFi) ---

void create_status_bar(lv_obj_t * scr) {
    lvgl_port_lock(-1);
    
    // 1. Główny niewidzialny pasek na samej górze
    lv_obj_t * status_bar = lv_obj_create(scr);
    lv_obj_set_size(status_bar, 320, 50); 
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(status_bar, 0, 0);
    lv_obj_set_style_border_opa(status_bar, 0, 0);
    lv_obj_set_style_pad_all(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

/*
    //2. Tytuł
    lbl_title = lv_label_create(status_bar);
    lv_label_set_text(lbl_title, "STACJA POGODOWA");
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, -60, 10);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_16, 0);
*/


    // 3. Pigułka z układem pionowym (Flex Column)
    lv_obj_t * clock_cont = lv_obj_create(status_bar);
    lv_obj_set_size(clock_cont, 110, 40); // Trochę wyższa pigułka
    lv_obj_align(clock_cont, LV_ALIGN_TOP_LEFT, 5, 5);
    lv_obj_set_style_radius(clock_cont, 10, 0);
    lv_obj_set_style_bg_opa(clock_cont, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(clock_cont, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_pad_all(clock_cont, 2, 0); // Mały odstęp od krawędzi pigułki
    
    // Ustawienie układu pionowego
    lv_obj_set_flex_flow(clock_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(clock_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(clock_cont, LV_OBJ_FLAG_SCROLLABLE);

    // 4. Etykieta DATY (na górze)
    lbl_date = lv_label_create(clock_cont);
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_12, 0); // Mniejsza czcionka dla daty
    lv_label_set_text(lbl_date, "01.01.2026");

    // 5. Etykieta CZASU (pod spodem)
    lbl_time = lv_label_create(clock_cont);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_16, 0); // Większa czcionka dla czasu
    lv_label_set_text(lbl_time, "00:00:00");

    lv_obj_move_foreground(status_bar);
    lvgl_port_unlock();

    // 6. Ikona WiFi
    // Obiekt ikony WiFi wewnątrz status_bar
    icon_wifi = lv_label_create(status_bar);
    lv_obj_align(icon_wifi, LV_ALIGN_TOP_RIGHT, -5, 5); // Po prawej stronie paska
    lv_label_set_text(icon_wifi, LV_SYMBOL_WIFI);     // Wbudowany symbol WiFi
    lv_obj_set_style_text_font(icon_wifi, &lv_font_montserrat_14, 0); // Symbole są w standardowych fontach
    lv_obj_set_style_text_color(icon_wifi, lv_palette_main(LV_PALETTE_GREY), 0); // Domyślnie szary (niepołączony)

    // 7. Stworzenie krzyżyka błędu
    icon_wifi_err = lv_label_create(status_bar);
    lv_label_set_text(icon_wifi_err, LV_SYMBOL_CLOSE); // Symbol "X"
    lv_obj_set_style_text_font(icon_wifi_err, &lv_font_montserrat_12, 0); // Trochę mniejszy niż WiFi
    lv_obj_set_style_text_color(icon_wifi_err, lv_palette_main(LV_PALETTE_RED), 0);

    // Ustawienie krzyżyka w prawym dolnym rogu ikony WiFi
    lv_obj_align_to(icon_wifi_err, icon_wifi, LV_ALIGN_TOP_RIGHT,-5, 5);

    // Domyślnie ukrywamy krzyżyk
    lv_obj_add_flag(icon_wifi_err, LV_OBJ_FLAG_HIDDEN);

    // 8. Ikona baterii 
    icon_battery = lv_label_create(status_bar);
    lv_obj_align(icon_battery, LV_ALIGN_TOP_RIGHT, -30, 5); // Po prawej stronie paska, przed WiFi
    lv_label_set_text(icon_battery, MY_SYM_BAT_100); // Domyślnie pełna bateria
    lv_obj_set_style_text_font(icon_battery, &montserrat_pl_14, 0);
    lv_obj_set_style_text_color(icon_battery, lv_palette_main(LV_PALETTE_GREY), 0); // Zielony kolor dla pełnej baterii
}

// --- STWORZENIE KAFELKA PRZYCISKU MENU ---

void create_menu_button(lv_obj_t * scr) {
    lv_obj_t * btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 50, 50);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_set_style_radius(btn, 25, 0); // Okrągły przycisk
    lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);

    lv_obj_t * label = lv_label_create(btn);
    lv_label_set_text(label, LV_SYMBOL_SETTINGS); // Symbol koła zębatego
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0); // Większa czcionka dla symbolu
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
}

// --- TWORZENIE GŁÓWNEGO INTERFEJSU LVGL ---
void create_weather_ui(void) {
    lvgl_port_lock(-1); 

    main_screen = lv_scr_act();
    lv_obj_t * scr = lv_scr_act();

    // 1. Kontener na kafelki (Flexbox) - rządek
    lv_obj_t * cont = lv_obj_create(scr);
    lv_obj_set_size(cont, 320, 120); 
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 55); // Tuż pod status barem
    lv_obj_set_style_bg_opa(cont, 0, 0); 
    lv_obj_set_style_border_opa(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW); 
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cont, 8, 0); // Odstęp między fasolkami
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    // Wspólny styl dla "Fasolki"
    static lv_style_t style_card;
    lv_style_init(&style_card);
    lv_style_set_radius(&style_card, 30); // Zaokrąglenie "fasolka"
    lv_style_set_bg_opa(&style_card, LV_OPA_10); // Delikatne tło
    lv_style_set_bg_color(&style_card, lv_palette_main(LV_PALETTE_GREY));
    lv_style_set_border_width(&style_card, 1);
    lv_style_set_border_color(&style_card, lv_palette_main(LV_PALETTE_GREY));
    lv_style_set_pad_all(&style_card, 5);

    // --- KAFELEK 1: TEMPERATURA ---
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
    lv_obj_set_style_text_font(title_temp, &montserrat_pl_14, 0); // Mniejszy opis

    lbl_temp = lv_label_create(card_temp);
    lv_obj_set_style_text_font(lbl_temp, &montserrat_pl_14, 0); // Twoja czcionka
    lv_label_set_text(lbl_temp, "--.-");

    // --- KAFELEK 2: WILGOTNOŚĆ ---
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

    // --- KAFELEK 3: CIŚNIENIE ---
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

    create_status_bar(scr); // Stworzenie paska statusu (czas + WiFi)
    create_menu_button(scr); // Stworzenie przycisku menu (koło zębate)
    lvgl_port_unlock();
}

// --- TASKI ODBIERAJĄCE DANE ---
void nrf_receiver_task(void *pvParameters) {
    SensorData localData = {0};
    while(1) {
        if (xSpiMutex && xSemaphoreTake(xSpiMutex, pdMS_TO_TICKS(100))) {
            if (nrf_receive_data(nrf_handle, &localData)) {
                xQueueOverwrite(xSensorQueue, &localData);
            }
            xSemaphoreGive(xSpiMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
// ZEGAR //
void collect_time_task(void *pvParameters) {
    ESP_LOGI("TIME", "Zadanie czasu uruchomione");
    while(1) {
        if (wifi_connect_station(SSID, PASSWORD)) { 
            ESP_LOGI("TIME", "WiFi OK, synchronizacja czasu...");
            sntp_init_module(); 
            setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); 
            tzset();
            wait_for_time_sync(); 
            vTaskDelay(pdMS_TO_TICKS(3600 * 1000)); // Kolejna synchronizacja za h
        } else { 
            ESP_LOGE("TIME", "WiFi zawiodlo, kolejna proba za 30s");
            vTaskDelay(pdMS_TO_TICKS(30000)); 
        }
    }
}


// --- TASK AKTUALIZUJĄCY EKRAN (GUI) ---
void update_ui_task(void *pvParameters) {
    SensorData receivedData = {0};
    char buf[64];
    time_t now; 
    struct tm timeinfo;
    int start_delay_counter = 5; // Pominięcie pierwszych 5 cykli (ok. 2.5s)

    while(1) {
        lvgl_port_lock(-1); // POPRAWKA 2

        // Aktualizacja Danych z NRF
        if (xQueueReceive(xSensorQueue, &receivedData, 0) == pdTRUE) {
            sprintf(buf, "%.2f °C", receivedData.temp_hundredths / 100.0f);
            lv_label_set_text(lbl_temp, buf); 
            
            sprintf(buf, "%.2f %%", receivedData.hum_x1024 / 1024.0f);
            lv_label_set_text(lbl_hum, buf);
            
            sprintf(buf, "%.0f hPa", receivedData.pressure_pa / 100.0f);
            lv_label_set_text(lbl_press, buf);
        }

        // Aktualizacja Czasu
        time(&now);
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year > (2020 - 1900)) {
            // Sam czas do prawego rogu
            sprintf(buf, "%02d.%02d.%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
            lv_label_set_text(lbl_date, buf);
            sprintf(buf, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            lv_label_set_text(lbl_time, buf);
        }

        lvgl_port_unlock();
        vTaskDelay(pdMS_TO_TICKS(500)); // Odświeżaj 2 razy na sekundę

        //aktualizacja ikony wifi

        if (start_delay_counter > 0) {
            start_delay_counter--;
            // W tym czasie ikona będzie szara (taka, jak w create_status_bar)
        } else {
            wifi_ap_record_t ap_info;
            esp_err_t res = esp_wifi_sta_get_ap_info(&ap_info);

            lvgl_port_lock(-1);
            if (is_wifi_connecting) {
                update_wifi_icon(0, false); // Blink
            } 
            else if (res == ESP_OK) {
                update_wifi_icon(ap_info.rssi, true);
            } 
            else {
                update_wifi_icon(0, false); // Krzyżyk (tylko jeśli połączenie się poddało)
            }
            lvgl_port_unlock();
            vTaskDelay(pdMS_TO_TICKS(500)); // Aktualizuj ikonę WiFi co sekundę
        }
            // Aktualizacja ikony baterii (przykładowa logika, dostosuj do swojego systemu pomiaru baterii)
            /*uint8_t battery_level = get_battery_level(); // Funkcja do odczytu poziomu baterii (0-100)
            lvgl_port_lock(-1);
            if (battery_level > 75) {
                lv_label_set_text(icon_battery, MY_SYM_BAT_100);
                lv_obj_set_style_text_color(icon_battery, lv_palette_main(LV_PALETTE_GREEN), 0);
            } else if (battery_level > 25) {
                lv_label_set_text(icon_battery, MY_SYM_BAT_50);
                lv_obj_set_style_text_color(icon_battery, lv_palette_main(LV_PALETTE_YELLOW), 0);
            } else {
                lv_label_set_text(icon_battery, MY_SYM_BAT_0);
                lv_obj_set_style_text_color(icon_battery, lv_palette_main(LV_PALETTE_RED), 0);
            }
            lvgl_port_unlock();
            vTaskDelay(pdMS_TO_TICKS(60000)); // Aktualizuj ikonę baterii co minutę
            */  
    }
    
}

// --- TASK TRYBU NOCNEGO ---
void night_mode_task(void *pvParameters) {
    bool last_is_night = false;
    bool first_run = true;

    while(1) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);

        if (timeinfo.tm_year > (2020 - 1900)) { // Sprawdź, czy mamy prawdziwy czas
            
            // Tryb nocny: od 22:00 do 7:59
            bool is_night = (timeinfo.tm_hour >= 22 || timeinfo.tm_hour < 8);
            //bool lv_dark_mode = !is_night; // Możesz tu dodać dodatkowe warunki, np. na podstawie czujnika światła

            if (is_night != last_is_night || first_run) {
                last_is_night = is_night;
                first_run = false;

                ESP_LOGI(TAG, "Przelaczanie na tryb: %s", is_night ? "NOCNY" : "DZIENNY");

                // --- POPRAWKA: LOCK MUSI BYĆ PRZED ZMIANĄ STYLU! ---
                lvgl_port_lock(-1); 
                
                lv_obj_report_style_change(NULL); // Teraz jest to w 100% bezpieczne
                
                lv_disp_t * disp = lv_disp_get_default();
                lv_theme_t * th = lv_theme_default_init(disp,
                    lv_palette_main(LV_PALETTE_BLUE),
                    lv_palette_main(LV_PALETTE_RED), 
                    is_night,                        
                    LV_FONT_DEFAULT);
                lv_disp_set_theme(disp, th);
                
                lvgl_port_unlock();

                // 2. Ściemnianie/Rozjaśnianie podświetlenia
                uint32_t duty = is_night ? 20 : 255; 
                ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
                ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000)); // Sprawdzaj czas co 5 sekund
    }
}

// --- MAIN ---
void app_main(void) {
    ESP_LOGI(TAG, "Start stacji pogodowej z LVGL...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 1. Inicjalizacja sprzętu
    xSpiMutex = xSemaphoreCreateMutex();
    xSensorQueue = xQueueCreate(1, sizeof(SensorData));
    
    gpio_init();
    spi_init();
    nrf_init(&nrf_handle);
    backlight_pwm_init();
    lcd_init();
    lcd_touch_init(VSPI_HOST);

    // 2. Rysowanie początkowego interfejsu
    init_button_styles(); // Inicjalizacja stylów przycisków
    create_weather_ui();
    lvgl_port_lock(-1);
    create_settings_ui();
    create_wifi_settings_ui();
    lv_scr_load(main_screen); // Ustawienie ekranu głównego jako aktywnego
    lvgl_port_unlock();
    
    // 3. Startowanie zadań FreeRTOS
    xTaskCreate(nrf_receiver_task, "NRF_TASK", 4096, NULL, 4, NULL);
    xTaskCreate(collect_time_task, "TIME_TASK", 8192, NULL, 4, NULL); 
    xTaskCreate(update_ui_task, "GUI_UPDATE_TASK", 8192, NULL, 5, NULL);
    xTaskCreate(night_mode_task, "NIGHT_MODE_TASK", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "System dziala!");
    
    // POPRAWKA 3: Zatrzymujemy główny task w nieskończonej pętli
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}