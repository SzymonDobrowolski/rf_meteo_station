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
#include <sys/time.h>
#include "esp_http_client.h"
#include "cJSON.h"

#include "nvs.h"

#include "lvgl.h"
#include "esp_lvgl_port.h"

#include "gpio_init.h"
#include "spi_init.h"
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
LV_FONT_DECLARE(weather_icons);
LV_IMG_DECLARE(put_logo);

#define MY_SYM_TEMP     "\xEF\x8B\x8B" // Termometr
#define MY_SYM_HUM      "\xEF\x81\x83" // Kropla
#define MY_SYM_PRESS    "\xEF\x98\xA4" // Ciśnienie

#define MY_SYM_BAT_100  "\xEF\x89\x80" // Pełna bateria
#define MY_SYM_BAT_50   "\xEF\x89\x82" // Połowa
#define MY_SYM_BAT_0    "\xEF\x89\x84" // Pusta

// --- IKONY POGODOWE (Dla wygenerowanej czcionki FontAwesome) ---
#define SYM_W_SUN        "\xEF\x86\x85" // Słońce (f185)
#define SYM_W_CLOUD      "\xEF\x83\x82" // Chmura (f0c2)
#define SYM_W_CLOUD_SUN  "\xEF\x9B\x84" // Słońce z chmurą (f6c4)
#define SYM_W_RAIN       "\xEF\x9D\x80" // Deszcz (f740)
#define SYM_W_SNOW       "\xEF\x8B\x9C" // Śnieg (f2dc)
#define SYM_W_STORM      "\xEF\x83\xA7" // Burza (f0e7)

// Obiekty LVGL
static lv_obj_t * main_screen = NULL;
static lv_obj_t * settings_screen = NULL;
static lv_obj_t * weather_info_screen = NULL;
static lv_obj_t * graph_screen = NULL;

char current_city[64] = "Warszawa"; // Domyślne miasto
bool force_weather_update = true;   // Flaga wymuszająca natychmiastowe pobranie po wpisaniu
static lv_obj_t * forecast_day_labels[7];
static lv_obj_t * forecast_icon_labels[7];
static lv_obj_t * forecast_temp_labels[7];
static lv_obj_t * lbl_weather_city = NULL; // 

static lv_obj_t * chart_temp;
static lv_chart_series_t * ser_temp;

static lv_obj_t * chart_hum;
static lv_chart_series_t * ser_hum;

static lv_obj_t * chart_press;
static lv_chart_series_t * ser_press;

static lv_obj_t * wifi_screen = NULL;
static lv_obj_t * btn_wifi_list[10]; // Tablica przycisków sieci
static lv_obj_t * wifi_list_cont = NULL;
static lv_obj_t * btn_refresh_ptr = NULL;
static lv_obj_t * scan_spinner_ptr = NULL; // 
static bool is_scanning = false; // 
static lv_obj_t * password_modal = NULL;
static lv_obj_t * ta_password = NULL;
static lv_obj_t * kb = NULL;
static char selected_ssid[33] = {0}; // Tutaj zapiszemy klikniętą sieć

static lv_obj_t * date_time_screen = NULL;
static lv_obj_t * roller_day;
static lv_obj_t * roller_month;
static lv_obj_t * roller_year;
static lv_obj_t * roller_hour;
static lv_obj_t * roller_minute;

uint32_t day_brightness = 100; // Domyślnie 100%
uint32_t night_brightness = 25; // Domyślnie 25%
static lv_obj_t * bright_screen = NULL;
static lv_obj_t * slider_day;
static lv_obj_t * slider_night;
static lv_obj_t * lbl_val_day;
static lv_obj_t * lbl_val_night;

static lv_obj_t * reset_settings_screen = NULL;
static lv_obj_t * btn_reset_confirm = NULL;

lv_obj_t *lbl_temp, *lbl_hum, *lbl_press, *lbl_time, *lbl_date, *icon_wifi, *icon_wifi_err, *icon_battery;
bool is_wifi_connecting = false;

// Prototypy funkcji (aby uniknąć ostrzeżeń kompilatora)
void create_weather_ui(void);
void create_settings_ui(void);
void create_weather_screen(void);
void weather_info_event_cb(lv_event_t * e);
void create_graph_screen(void);
void graph_event_cb(lv_event_t * e);
void create_date_time_ui(void);
void create_wifi_settings_ui(void);
void create_status_bar(lv_obj_t * scr);
void create_menu_button(lv_obj_t * scr);
void create_graph_button(lv_obj_t * scr);
void create_weather_info_button(lv_obj_t * scr);
static void settings_event_cb(lv_event_t * e);
static void wifi_settings(lv_event_t * e);
static void back_event_to_main(lv_event_t * e);
static void open_password_modal(lv_event_t * e);
static void close_modal_cb(lv_event_t * e);
static void connect_btn_cb(lv_event_t * e);
void update_wifi_icon(int8_t rssi, bool connected);
void start_wifi_blink(void);
static void set_obj_opa(void * obj, int32_t v);
void refresh_wifi_list(lv_obj_t * list_cont);
static lv_style_t style_btn;
static void date_time_settings_cb(lv_event_t * e);
static void bright_settings_cb(lv_event_t * e);
static void reset_settings_cb(lv_event_t * e);

// --- ZDARZENIE POTWIERDZENIA RESETU ---
static void confirm_reset_cb(lv_event_t * e) {
    ESP_LOGW(TAG, "Rozpoczynam twardy reset do ustawien fabrycznych...");
    
    // 1. Kasujemy całą pamięć NVS (usuwa WiFi i ustawienia jasności)
    nvs_flash_erase(); 
    
    // Zatrzymujemy na 1 sekundę, żeby silnik NVS zapisał pustą przestrzeń
    vTaskDelay(pdMS_TO_TICKS(1000)); 

    ESP_LOGW(TAG, "Zresetowano. Wykonuje restart ukladu...");
    
    // 2. Automatycznie restartujemy kontroler
    esp_restart(); 
}

// --- TWORZENIE EKRANU RESETU ---
void create_reset_ui(void) {

    if (reset_settings_screen != NULL) {
        lv_scr_load(reset_settings_screen);
        return;
    }
    
    reset_settings_screen = lv_obj_create(NULL);

    // Tytuł
    lv_obj_t * title = lv_label_create(reset_settings_screen);
    lv_label_set_text(title, "UWAGA!");
    lv_obj_set_style_text_font(title, &montserrat_pl_14, 0);
    lv_obj_set_style_text_color(title, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Długi tekst informacyjny
    lv_obj_t * info = lv_label_create(reset_settings_screen);
    lv_label_set_text(info, "Ta operacja usunie zapisane\nsieci Wi-Fi oraz przywróci\ndomyślne ustawienia jasności.\n\nUrządzenie zostanie\nautomatycznie uruchomione ponownie.");
    lv_obj_set_style_text_font(info, &montserrat_pl_14, 0);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, -20);

    // --- PRZYCISKI AKCJI ---
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
    lv_obj_set_style_text_color(lbl_reset, lv_color_white(), 0);
    lv_obj_center(lbl_reset);

    lv_scr_load(reset_settings_screen);

}

// Zdarzenie po kliknięciu "Reset do ustawień fabrycznych" w menu
static void reset_settings_cb(lv_event_t * e) {
    create_reset_ui();
}

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
    lv_obj_clean(list_cont); // czyszczenie

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
            lv_obj_set_style_text_font(l, &montserrat_pl_14, 0);
            lv_label_set_text(icon_l, LV_SYMBOL_WIFI); 
        } else {
            // ZABEZPIECZENIE: %.32s wymusza ograniczenie znaków
            lv_label_set_text_fmt(l, "%.32s", (char *)wifi_list[i].ssid);
            lv_obj_set_style_text_font(l, &montserrat_pl_14, 0);
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
        
        lv_obj_add_event_cb(btn, open_password_modal, LV_EVENT_CLICKED, (void *)wifi_list[i].ssid);
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
    
    if (btn_refresh_ptr != NULL) lv_obj_add_flag(btn_refresh_ptr, LV_OBJ_FLAG_HIDDEN);
    if (scan_spinner_ptr != NULL) lv_obj_clear_flag(scan_spinner_ptr, LV_OBJ_FLAG_HIDDEN);

    // TUTA JEST BEZPIECZNE CZYSZCZENIE (Wątek GUI usuwa stare sieci z ekranu)
    if (wifi_list_cont != NULL && lv_obj_is_valid(wifi_list_cont)) {
        lv_obj_clean(wifi_list_cont);
    }

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

// --- ŁADOWANIE EKRANU USTAWIEŃ --- //
static void settings_event_cb(lv_event_t * e) {
    lv_scr_load(settings_screen);
}

static void back_event_to_main(lv_event_t * e) {
    lv_scr_load(main_screen);
}

// --- FUNKCJE DO PAMIĘCI NVS ---
void save_wifi_credentials(const char * ssid, const char * pass) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_str(my_handle, "ssid", ssid);
        nvs_set_str(my_handle, "pass", pass);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Zapisano nowe dane WiFi do NVS: %s", ssid);
    }
}

bool load_wifi_credentials(char * ssid, char * pass, size_t max_len) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
        size_t len = max_len;
        esp_err_t err_ssid = nvs_get_str(my_handle, "ssid", ssid, &len);
        len = max_len;
        esp_err_t err_pass = nvs_get_str(my_handle, "pass", pass, &len);
        nvs_close(my_handle);
        return (err_ssid == ESP_OK && err_pass == ESP_OK);
    }
    return false;
}

static void close_modal_cb(lv_event_t * e) {
    if (password_modal != NULL) {
        lv_obj_del_async(password_modal); // MUSI BYĆ ASYNC!
        password_modal = NULL;
        ta_password = NULL;
        kb = NULL;
    }
}

// Struktura do przekazania danych z modalu do taska
typedef struct {
    char ssid[33];
    char pass[64];
} wifi_cred_t;

void wifi_connect_task(void *pvParameters) {
    wifi_cred_t *creds = (wifi_cred_t *)pvParameters;
    
    ESP_LOGI(TAG, "Proba polaczenia z siecia: %s", creds->ssid);
    
    is_wifi_connecting = true; 
    bool success = wifi_connect_station(creds->ssid, creds->pass); 
    is_wifi_connecting = false; 

    // --- POPRAWKA: DAJEMY SYSTEMOWI CZAS NA "ODDECH" ---
    // Zatrzymujemy ten konkretny task na 1.5 sekundy. 
    // W tym czasie spinner dalej uroczo kręci się na ekranie,
    // a sterowniki Wi-Fi zdążą zapisać flagi i adres IP.
    vTaskDelay(pdMS_TO_TICKS(1500));

    lvgl_port_lock(-1);
    if (success) {
        ESP_LOGI(TAG, "Polaczono pomyslnie!");
        save_wifi_credentials(creds->ssid, creds->pass);
    } else {
        ESP_LOGE(TAG, "Blad polaczenia z WiFi");
    }

    // Teraz odświeżamy listę - sterownik Wi-Fi na pewno zwróci już poprawne dane
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

static void connect_btn_cb(lv_event_t * e) {
    // TARCZA ANTY-CRASHOWA: Zabezpieczenie przed podwójnym klikiem
    if (ta_password == NULL) return; 
    
    const char * pwd = lv_textarea_get_text(ta_password);
    
    wifi_cred_t *creds = malloc(sizeof(wifi_cred_t));
    snprintf(creds->ssid, sizeof(creds->ssid), "%.32s", selected_ssid);
    snprintf(creds->pass, sizeof(creds->pass), "%.63s", pwd);

    if (password_modal != NULL) {
        lv_obj_del_async(password_modal);
        password_modal = NULL;
        ta_password = NULL;
        kb = NULL;
    }

    if (btn_refresh_ptr != NULL) lv_obj_add_flag(btn_refresh_ptr, LV_OBJ_FLAG_HIDDEN);
    if (scan_spinner_ptr != NULL) lv_obj_clear_flag(scan_spinner_ptr, LV_OBJ_FLAG_HIDDEN);

    xTaskCreate(wifi_connect_task, "WIFI_CONN", 8192, creds, 5, NULL);
}

// Zdarzenie po kliknięciu przycisku sieci (na razie tylko loguje)
static void open_password_modal(lv_event_t * e) {
    // Pobieramy nazwę sieci przekazaną z przycisku

    if (password_modal != NULL) return;

    char * ssid = (char *)lv_event_get_user_data(e);
    snprintf(selected_ssid, sizeof(selected_ssid), "%.32s", ssid);

    // 1. Tło modala (Ciemna, półprzezroczysta nakładka na cały ekran)
    password_modal = lv_obj_create(lv_scr_act());
    lv_obj_set_size(password_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(password_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(password_modal, LV_OPA_50, 0); 
    lv_obj_set_style_border_width(password_modal, 0, 0);
    lv_obj_set_style_pad_all(password_modal, 0, 0);
    lv_obj_add_flag(password_modal, LV_OBJ_FLAG_CLICKABLE); // Blokuje klikanie w obiekty pod spodem!

    // 2. Biały panel na górze (na pole tekstowe i przyciski)
    lv_obj_t * panel = lv_obj_create(password_modal);
    lv_obj_set_size(panel, 300, 110);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    // 3. Tytuł z nazwą sieci
    lv_obj_t * lbl_title = lv_label_create(panel);
    lv_label_set_text_fmt(lbl_title, "Hasło: %s", selected_ssid);
    lv_obj_set_style_text_font(lbl_title, &montserrat_pl_14, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, -5);

    // 4. Pole tekstowe na hasło
    ta_password = lv_textarea_create(panel);
    lv_obj_set_size(ta_password, 260, 40);
    lv_obj_align(ta_password, LV_ALIGN_TOP_MID, 0, 20);
    lv_textarea_set_password_mode(ta_password, true); // Zmienia tekst w gwiazdki
    lv_textarea_set_one_line(ta_password, true);
    lv_textarea_set_max_length(ta_password, 63); // Standardowa max długość hasła WiFi

    // 5. Przyciski akcji (Anuluj i Połącz)
    lv_obj_t * btn_cancel = lv_btn_create(panel);
    lv_obj_set_size(btn_cancel, 100, 30);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 10, 5);
    lv_obj_t * lbl_canc = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_canc, "Anuluj");
    lv_obj_set_style_text_font(lbl_canc, &montserrat_pl_14, 0);
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

    // 6. Klawiatura (Na dole ekranu)
    kb = lv_keyboard_create(password_modal);
    lv_obj_set_size(kb, 320, 120); // Zajmuje równe pół ekranu 320x240
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, ta_password); // Łączy klawiaturę z polem tekstowym

}

// --- ZDARZENIE PO KLIKNIĘCIU PRZYCISKU USTAWIEŃ WIFI --- //

static void wifi_settings(lv_event_t * e) {

    if (is_wifi_connecting) {
        ESP_LOGW(TAG, "Blokada: Trwa łączenie z WiFi. Skanowanie niedozwolone.");
        return; 
    }

    create_wifi_settings_ui();

    if (is_scanning) {
        if (btn_refresh_ptr != NULL) lv_obj_add_flag(btn_refresh_ptr, LV_OBJ_FLAG_HIDDEN);
        if (scan_spinner_ptr != NULL) lv_obj_clear_flag(scan_spinner_ptr, LV_OBJ_FLAG_HIDDEN);
        return; 
    }
    
    is_scanning = true;

    if (btn_refresh_ptr != NULL) lv_obj_add_flag(btn_refresh_ptr, LV_OBJ_FLAG_HIDDEN);
    if (scan_spinner_ptr != NULL) lv_obj_clear_flag(scan_spinner_ptr, LV_OBJ_FLAG_HIDDEN);

    // TUTA JEST BEZPIECZNE CZYSZCZENIE (Wątek GUI usuwa stare sieci z ekranu)
    if (wifi_list_cont != NULL && lv_obj_is_valid(wifi_list_cont)) {
        lv_obj_clean(wifi_list_cont);
    }

    xTaskCreate(wifi_scan_task, "WIFI_SCAN", 8192, NULL, 5, NULL);
}

// 3. Gdy połączysz się z WiFi:
void set_wifi_connected(int index) {
    lv_obj_set_style_bg_color(btn_wifi_list[index], lv_palette_main(LV_PALETTE_BLUE), 0);
    // ... dodaj label "Połączono" ...
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
    lv_obj_add_event_cb(btn_back, settings_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "WRÓĆ");
    lv_obj_set_style_text_font(lbl_back, &montserrat_pl_14, 0);
    lv_obj_center(lbl_back);

    lv_scr_load(wifi_screen);
}

// Pomocnicza funkcja do generowania listy liczb dla bębnów (np. "01\n02\n03...")
static void generate_roller_opts(char * buf, int start, int end) {
    buf[0] = '\0';
    for (int i = start; i <= end; i++) {
        char tmp[8];
        if (i == end) sprintf(tmp, "%02d", i);
        else sprintf(tmp, "%02d\n", i);
        strcat(buf, tmp);
    }
}

// Zdarzenie: Zapisz czas i wróć
static void save_time_cb(lv_event_t * e) {
    // Zbieranie danych z bębnów
    struct tm timeinfo = {0};
    timeinfo.tm_mday = lv_roller_get_selected(roller_day) + 1;
    timeinfo.tm_mon  = lv_roller_get_selected(roller_month); 
    timeinfo.tm_year = lv_roller_get_selected(roller_year) + (2024 - 1900); 
    timeinfo.tm_hour = lv_roller_get_selected(roller_hour);
    timeinfo.tm_min  = lv_roller_get_selected(roller_minute);
    timeinfo.tm_sec  = 0;

    // Przeliczenie na format systemowy
    time_t t = mktime(&timeinfo);
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    
    // Ustawienie głównego zegara ESP32 (wbudowanego RTC)
    settimeofday(&tv, NULL);
    
    ESP_LOGI(TAG, "Czas zaktualizowany recznie!");

    lv_scr_load(settings_screen);

}

// --- TWORZENIE EKRANU USTAWIEŃ CZASU ---
void create_date_time_ui(void) {
    if (date_time_screen != NULL) {
        lv_scr_load(date_time_screen);
        return;
    }
    
    date_time_screen = lv_obj_create(NULL);
    
    // Tytuł
    lv_obj_t * title = lv_label_create(date_time_screen);
    lv_label_set_text(title, "USTAW CZAS I DATE");
    lv_obj_set_style_text_font(title, &montserrat_pl_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Bufory na teksty do bębnów
    char opts_31[100], opts_12[40], opts_years[100];
    char opts_24[80], opts_60[200];
    
    generate_roller_opts(opts_31, 1, 31);
    generate_roller_opts(opts_12, 1, 12);
    generate_roller_opts(opts_years, 2024, 2040); // Zakres lat
    generate_roller_opts(opts_24, 0, 23);
    generate_roller_opts(opts_60, 0, 59);

    // --- RZĄD 1: DATA ---
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
    lv_roller_set_visible_row_count(roller_day, 2);
    lv_obj_set_width(roller_day, 60);

    roller_month = lv_roller_create(date_cont);
    lv_roller_set_options(roller_month, opts_12, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_month, 2);
    lv_obj_set_width(roller_month, 60);

    roller_year = lv_roller_create(date_cont);
    lv_roller_set_options(roller_year, opts_years, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_year, 2);
    lv_obj_set_width(roller_year, 80);

    // --- RZĄD 2: CZAS ---
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
    lv_roller_set_visible_row_count(roller_hour, 2);
    lv_obj_set_width(roller_hour, 60);

    lv_obj_t * colon = lv_label_create(time_cont);
    lv_label_set_text(colon, ":");
    lv_obj_set_style_text_font(colon, &montserrat_pl_14, 0);

    roller_minute = lv_roller_create(time_cont);
    lv_roller_set_options(roller_minute, opts_60, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_minute, 2);
    lv_obj_set_width(roller_minute, 60);

    // --- PRZYCISKI AKCJI ---
    lv_obj_t * btn_back = lv_btn_create(date_time_screen);
    lv_obj_set_size(btn_back, 100, 35);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 20, -10);
    lv_obj_add_event_cb(btn_back, settings_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "Anuluj");
    lv_obj_center(lbl_back);

    lv_obj_t * btn_save = lv_btn_create(date_time_screen);
    lv_obj_set_size(btn_save, 100, 35);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_RIGHT, -20, -10);
    lv_obj_set_style_bg_color(btn_save, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_add_event_cb(btn_save, save_time_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, "Zapisz");
    lv_obj_set_style_text_color(lbl_save, lv_color_white(), 0);
    lv_obj_center(lbl_save);

    // Odczyt aktualnego czasu z systemu i ustawienie bębnów w odpowiedniej pozycji
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

// Zdarzenie odpalane po kliknięciu "Data i Czas" w menu głównym
static void date_time_settings_cb(lv_event_t * e) {
    create_date_time_ui();
}

// --- FUNKCJE NVS DLA JASNOŚCI ---
void save_brightness_settings(uint32_t day, uint32_t night) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_u32(my_handle, "day_br", day);
        nvs_set_u32(my_handle, "night_br", night);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
}

void load_brightness_settings(void) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
        nvs_get_u32(my_handle, "day_br", &day_brightness);
        nvs_get_u32(my_handle, "night_br", &night_brightness);
        nvs_close(my_handle);
    }
}

// --- ZDARZENIA SUWAKÓW ---
static void slider_event_cb(lv_event_t * e) {
    lv_obj_t * slider = lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    
    // Odświeżanie tekstu na żywo
    if(slider == slider_day) {
        lv_label_set_text_fmt(lbl_val_day, "%d%%", val);
    } else if(slider == slider_night) {
        lv_label_set_text_fmt(lbl_val_night, "%d%%", val);
    }
}

// Zdarzenie zapisu jasności
static void save_bright_cb(lv_event_t * e) {
    
    day_brightness = lv_slider_get_value(slider_day);
    night_brightness = lv_slider_get_value(slider_night);
    
    save_brightness_settings(day_brightness, night_brightness);
    
    // Natychmiastowe zastosowanie nowej jasności!
    time_t now; struct tm timeinfo;
    time(&now); localtime_r(&now, &timeinfo);
    bool is_night = false;
    if (timeinfo.tm_year > (2020 - 1900)) {
        is_night = (timeinfo.tm_hour >= 22 || timeinfo.tm_hour < 8);
    }
    uint32_t duty = is_night ? ((night_brightness * 255) / 100) : ((day_brightness * 255) / 100);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

    lv_scr_load(settings_screen);
}

// --- TWORZENIE EKRANU JASNOŚCI ---
void create_brightness_ui(void) {
    if (bright_screen != NULL) { 
        lv_scr_load(bright_screen);
        return;
    }
    
    bright_screen = lv_obj_create(NULL);
    
    lv_obj_t * title = lv_label_create(bright_screen);
    lv_label_set_text(title, "USTAWIENIA JASNOSCI");
    lv_obj_set_style_text_font(title, &montserrat_pl_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // --- SEKCJA DZIEŃ ---
    lv_obj_t * lbl_day = lv_label_create(bright_screen);
    lv_label_set_text(lbl_day, "Dzien (08:00 - 22:00)");
    lv_obj_align(lbl_day, LV_ALIGN_TOP_LEFT, 20, 45);

    slider_day = lv_slider_create(bright_screen);
    lv_obj_set_size(slider_day, 220, 15);
    lv_obj_align(slider_day, LV_ALIGN_TOP_LEFT, 20, 65);
    lv_slider_set_range(slider_day, 10, 100); // Od 10% do 100%
    lv_slider_set_value(slider_day, day_brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_day, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lbl_val_day = lv_label_create(bright_screen);
    lv_label_set_text_fmt(lbl_val_day, "%d%%", (int)day_brightness);
    lv_obj_align(lbl_val_day, LV_ALIGN_TOP_LEFT, 255, 63);

    // --- SEKCJA NOC ---
    lv_obj_t * lbl_night = lv_label_create(bright_screen);
    lv_label_set_text(lbl_night, "Noc (22:00 - 08:00)");
    lv_obj_align(lbl_night, LV_ALIGN_TOP_LEFT, 20, 105);

    slider_night = lv_slider_create(bright_screen);
    lv_obj_set_size(slider_night, 220, 15);
    lv_obj_align(slider_night, LV_ALIGN_TOP_LEFT, 20, 125);
    lv_slider_set_range(slider_night, 10, 100); // Od 10% do 100%
    lv_slider_set_value(slider_night, night_brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_night, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lbl_val_night = lv_label_create(bright_screen);
    lv_label_set_text_fmt(lbl_val_night, "%d%%", (int)night_brightness);
    lv_obj_align(lbl_val_night, LV_ALIGN_TOP_LEFT, 255, 123);

    // --- PRZYCISKI AKCJI ---
    lv_obj_t * btn_back = lv_btn_create(bright_screen);
    lv_obj_set_size(btn_back, 100, 35);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 20, -10);
    lv_obj_add_event_cb(btn_back, settings_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "Anuluj");
    lv_obj_center(lbl_back);

    lv_obj_t * btn_save = lv_btn_create(bright_screen);
    lv_obj_set_size(btn_save, 100, 35);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_RIGHT, -20, -10);
    lv_obj_set_style_bg_color(btn_save, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_add_event_cb(btn_save, save_bright_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, "Zapisz");
    lv_obj_set_style_text_color(lbl_save, lv_color_white(), 0);
    lv_obj_center(lbl_save);

    lv_scr_load(bright_screen);

}

static void bright_settings_cb(lv_event_t * e) {
    create_brightness_ui();
}

void save_location_settings(const char * city) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_str(my_handle, "city", city);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Zapisano nowe miasto do NVS: %s", city);
    }
}

void load_location_settings(void) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
        size_t len = sizeof(current_city);
        nvs_get_str(my_handle, "city", current_city, &len);
        nvs_close(my_handle);
    }
}

// --- UI: KLAWIATURA LOKALIZACJI ---
static lv_obj_t * loc_modal = NULL;
static lv_obj_t * ta_loc = NULL;

static void close_loc_modal_cb(lv_event_t * e) {
    if (loc_modal != NULL) {
        lv_obj_del_async(loc_modal); // MUSI BYĆ ASYNC!
        loc_modal = NULL;
        ta_loc = NULL;
    }
}

static void save_loc_btn_cb(lv_event_t * e) {
    // TARCZA ANTY-CRASHOWA: Jeśli pole już zniknęło, nie rób nic!
    if (ta_loc == NULL) return; 

    const char * city = lv_textarea_get_text(ta_loc);
    snprintf(current_city, sizeof(current_city), "%s", city); 
    save_location_settings(current_city); 
    force_weather_update = true; 
    
    if (lbl_weather_city != NULL) {
        lv_label_set_text_fmt(lbl_weather_city, "PROGNOZA POGODY DLA: %s", current_city);
    }

    if (loc_modal != NULL) {
        lv_obj_del_async(loc_modal); 
        loc_modal = NULL;
        ta_loc = NULL;
    }
}

static void location_settings_cb(lv_event_t * e) {

    if (loc_modal != NULL) return;
    // Tło modala
    loc_modal = lv_obj_create(lv_scr_act());
    lv_obj_set_size(loc_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(loc_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(loc_modal, LV_OPA_50, 0); 
    lv_obj_set_style_border_width(loc_modal, 0, 0);
    lv_obj_set_style_pad_all(loc_modal, 0, 0);
    lv_obj_add_flag(loc_modal, LV_OBJ_FLAG_CLICKABLE);

    // Panel
    lv_obj_t * panel = lv_obj_create(loc_modal);
    lv_obj_set_size(panel, 300, 110);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 5);

    lv_obj_t * lbl_title = lv_label_create(panel);
    lv_label_set_text(lbl_title, "Wpisz miejscowość:");
    lv_obj_set_style_text_font(lbl_title, &montserrat_pl_14, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, -5);

    // Pole tekstowe
    ta_loc = lv_textarea_create(panel);
    lv_obj_set_size(ta_loc, 260, 40);
    lv_obj_align(ta_loc, LV_ALIGN_TOP_MID, 0, 20);
    lv_textarea_set_one_line(ta_loc, true);
    lv_textarea_set_text(ta_loc, current_city); // Od razu wpisane aktualne miasto

    // Przyciski
    lv_obj_t * btn_cancel = lv_btn_create(panel);
    lv_obj_set_size(btn_cancel, 100, 30);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 10, 5);
    lv_obj_t * lbl_canc = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_canc, "Anuluj");
    lv_obj_center(lbl_canc);
    lv_obj_add_event_cb(btn_cancel, close_loc_modal_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_save = lv_btn_create(panel);
    lv_obj_set_size(btn_save, 100, 30);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_RIGHT, -10, 5);
    lv_obj_set_style_bg_color(btn_save, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_t * lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, "Zapisz");
    lv_obj_set_style_text_color(lbl_save, lv_color_white(), 0);
    lv_obj_center(lbl_save);
    lv_obj_add_event_cb(btn_save, save_loc_btn_cb, LV_EVENT_CLICKED, NULL);

    // Klawiatura
    lv_obj_t * kb_city = lv_keyboard_create(loc_modal);
    lv_obj_set_size(kb_city, 320, 120);
    lv_obj_align(kb_city, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb_city, ta_loc);

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
    lv_obj_add_event_cb(btn_date, date_time_settings_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_bright = create_menu_btn(menu_cont, "Ustawienia Jasności", &style_btn);
    lv_obj_add_event_cb(btn_bright, bright_settings_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t * btn_reset = create_menu_btn(menu_cont, "Reset do Ustawień Fabrycznych", &style_btn);
    lv_obj_add_event_cb(btn_reset, reset_settings_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_loc = create_menu_btn(menu_cont, "Lokalizacja Prognozy Pogody", &style_btn);
    lv_obj_add_event_cb(btn_loc, location_settings_cb, LV_EVENT_CLICKED, NULL);

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

// --- OKNO WYKRESLANIA WYKRESU --- //



// --- TWORZENIE PASKA STATUSU (Czas + WiFi) ---

void create_status_bar(lv_obj_t * scr) {
    
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
    lv_obj_set_style_border_width(clock_cont, 1, 0);
    lv_obj_set_style_border_color(clock_cont, lv_palette_main(LV_PALETTE_GREY), 0);
    
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
    lv_obj_add_event_cb(btn, settings_event_cb, LV_EVENT_CLICKED, NULL);
}

// --- FUNKCJA FORMATUJĄCA OŚ X NA GODZINY ---
static void chart_x_axis_cb(lv_event_t * e) {
    lv_obj_draw_part_dsc_t * dsc = lv_event_get_draw_part_dsc(e);
    
    if(dsc->part == LV_PART_TICKS && dsc->id == LV_CHART_AXIS_PRIMARY_X) {
        // Mamy 7 głównych kresek (od 0 do 6). 
        // Mnożymy x4, żeby otrzymać: 00:00, 04:00, 08:00, 12:00...
        int hour = dsc->value * 6; 
        if (hour >= 24) hour = 0; // Północ to 00:00
        lv_snprintf(dsc->text, dsc->text_length, "%02d:00", hour);
    }
}

void create_graph_screen(void)
{
    graph_screen = lv_obj_create(NULL);
    
    // Tytuł
    lv_obj_t * lbl = lv_label_create(graph_screen);
    lv_label_set_text(lbl, "WYKRESY (OSTATNIA DOBA)");
    lv_obj_set_style_text_font(lbl, &montserrat_pl_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 5);

    // Przycisk POWRÓT na starym miejscu (Na dole)
    lv_obj_t * btn_back = lv_btn_create(graph_screen);
    lv_obj_set_size(btn_back, 80, 30);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -5);
    lv_obj_add_event_cb(btn_back, back_event_to_main, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "WRÓĆ");
    lv_obj_set_style_text_font(lbl_back, &montserrat_pl_14, 0);
    lv_obj_center(lbl_back);

    // Kontener pionowy na wykresy
    lv_obj_t * chart_cont = lv_obj_create(graph_screen);
    lv_obj_set_size(chart_cont, 320, 170); 
    lv_obj_align(chart_cont, LV_ALIGN_TOP_MID, 0, 30); // Zaczyna się pod tytułem
    lv_obj_set_flex_flow(chart_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(chart_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(chart_cont, 5, 0); 
    lv_obj_set_style_bg_opa(chart_cont, 0, 0);
    lv_obj_set_style_border_opa(chart_cont, 0, 0);
    
    // BARDZO WAŻNE: Blokujemy przewijanie kontenera w poziomie i ucinamy pasek!
    lv_obj_set_scroll_dir(chart_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(chart_cont, LV_SCROLLBAR_MODE_OFF);

    // --- WYKRES 1: TEMPERATURA ---
    lv_obj_t * lbl_t = lv_label_create(chart_cont);
    lv_label_set_text(lbl_t, "Temperatura [°C]");
    lv_obj_set_style_text_font(lbl_t, &montserrat_pl_14, 0);
    lv_obj_set_style_pad_top(lbl_t, 30, 0);
    
    chart_temp = lv_chart_create(chart_cont);
    lv_obj_set_size(chart_temp, 240, 140); // Szerokość powiększona do 300!
    lv_obj_set_style_pad_left(chart_temp, 10, 0);   // POTĘŻNE miejsce na oś Y (65 pikseli)
    lv_obj_set_style_pad_bottom(chart_temp, 25, 0); // Miejsce na godziny (oś X)
    lv_obj_set_style_pad_right(chart_temp, 15, 0);  
    lv_obj_set_style_pad_top(chart_temp, 5, 0);    

    lv_chart_set_type(chart_temp, LV_CHART_TYPE_LINE);
    lv_obj_set_style_size(chart_temp, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(chart_temp, 3, LV_PART_ITEMS);
    lv_obj_set_style_line_rounded(chart_temp, true, LV_PART_ITEMS);
    lv_chart_set_range(chart_temp, LV_CHART_AXIS_PRIMARY_Y, -20, 50); 
    // Ustawiamy jasnoszary kolor siatki (żeby zeszła na drugi plan)
    lv_obj_set_style_line_color(chart_temp, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
    
    // Robimy z siatki linię przerywaną (2 piksele kreski, 2 piksele przerwy)
    lv_obj_set_style_line_dash_width(chart_temp, 2, LV_PART_MAIN);
    lv_obj_set_style_line_dash_gap(chart_temp, 2, LV_PART_MAIN);
    // Oś Y: rysujemy etykiety na szerokości 65 pikseli (ostatni argument)
    lv_chart_set_axis_tick(chart_temp, LV_CHART_AXIS_PRIMARY_Y, 5, 2, 8, 2, true, 45); 
    // Oś X: dokładnie 5 kresek głównych!
    lv_chart_set_axis_tick(chart_temp, LV_CHART_AXIS_PRIMARY_X, 5, 2, 5, 1, true, 20);
    lv_chart_set_point_count(chart_temp, 96); 

    lv_obj_add_event_cb(chart_temp, chart_x_axis_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    ser_temp = lv_chart_add_series(chart_temp, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(chart_temp, ser_temp, LV_CHART_POINT_NONE);

    // --- WYKRES 2: WILGOTNOŚĆ ---
    lv_obj_t * lbl_h = lv_label_create(chart_cont);
    lv_label_set_text(lbl_h, "Wilgotność [%]");
    lv_obj_set_style_text_font(lbl_h, &montserrat_pl_14, 0);
    lv_obj_set_style_pad_top(lbl_h, 30, 0);
    
    chart_hum = lv_chart_create(chart_cont);
    lv_obj_set_size(chart_hum, 240, 140);
    lv_obj_set_style_pad_left(chart_hum, 10, 0);   // Zwiększone na 65
    lv_obj_set_style_pad_bottom(chart_hum, 25, 0); 
    lv_obj_set_style_pad_right(chart_hum, 15, 0);  
    lv_obj_set_style_pad_top(chart_hum, 5, 0);  
    // Pogrubiamy linię z 1-2 pikseli na 3 piksele (wyraźniejsza krzywa)
    lv_obj_set_style_line_width(chart_hum, 3, LV_PART_ITEMS);
    
    // Zaokrąglamy łączenia między punktami (efekt "płynnej" fali)
    lv_obj_set_style_line_rounded(chart_hum, true, LV_PART_ITEMS);
    // Ustawiamy jasnoszary kolor siatki (żeby zeszła na drugi plan)
    lv_obj_set_style_line_color(chart_hum, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
    
    // Robimy z siatki linię przerywaną (2 piksele kreski, 2 piksele przerwy)
    lv_obj_set_style_line_dash_width(chart_hum, 2, LV_PART_MAIN);
    lv_obj_set_style_line_dash_gap(chart_hum, 2, LV_PART_MAIN);
    
    lv_chart_set_type(chart_hum, LV_CHART_TYPE_LINE);
    lv_obj_set_style_size(chart_hum, 0, LV_PART_INDICATOR);
    lv_chart_set_range(chart_hum, LV_CHART_AXIS_PRIMARY_Y, 0, 100); 
    // Oś Y szeroka na 65
    lv_chart_set_axis_tick(chart_hum, LV_CHART_AXIS_PRIMARY_Y, 5, 2, 6, 2, true, 45);
    // Oś X na dokładnie 5 kresek (było 7!)
    lv_chart_set_axis_tick(chart_hum, LV_CHART_AXIS_PRIMARY_X, 5, 2, 5, 1, true, 20);
    lv_chart_set_point_count(chart_hum, 96);
    
    lv_obj_add_event_cb(chart_hum, chart_x_axis_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    ser_hum = lv_chart_add_series(chart_hum, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(chart_hum, ser_hum, LV_CHART_POINT_NONE);

    // --- WYKRES 3: CIŚNIENIE ---
    lv_obj_t * lbl_p = lv_label_create(chart_cont);
    lv_label_set_text(lbl_p, "Ciśnienie [hPa]");
    lv_obj_set_style_text_font(lbl_p, &montserrat_pl_14, 0);
    lv_obj_set_style_pad_top(lbl_p, 30, 0);
    
    chart_press = lv_chart_create(chart_cont);
    lv_obj_set_size(chart_press, 240, 140);
    lv_obj_set_style_pad_left(chart_press, 10, 0);   // Zwiększone na 65
    lv_obj_set_style_pad_bottom(chart_press, 25, 0); 
    lv_obj_set_style_pad_right(chart_press, 15, 0);  
    lv_obj_set_style_pad_top(chart_press, 5, 0);  
    // Pogrubiamy linię z 1-2 pikseli na 3 piksele (wyraźniejsza krzywa)
    lv_obj_set_style_line_width(chart_press, 3, LV_PART_ITEMS);
    
    // Zaokrąglamy łączenia między punktami (efekt "płynnej" fali)
    lv_obj_set_style_line_rounded(chart_press, true, LV_PART_ITEMS);
    // Ustawiamy jasnoszary kolor siatki (żeby zeszła na drugi plan)
    lv_obj_set_style_line_color(chart_press, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
    
    // Robimy z siatki linię przerywaną (2 piksele kreski, 2 piksele przerwy)
    lv_obj_set_style_line_dash_width(chart_press, 2, LV_PART_MAIN);
    lv_obj_set_style_line_dash_gap(chart_press, 2, LV_PART_MAIN);
    
    lv_chart_set_type(chart_press, LV_CHART_TYPE_LINE);
    lv_obj_set_style_size(chart_press, 0, LV_PART_INDICATOR);
    lv_chart_set_range(chart_press, LV_CHART_AXIS_PRIMARY_Y, 950, 1050); 
    // Oś Y szeroka na 65
    lv_chart_set_axis_tick(chart_press, LV_CHART_AXIS_PRIMARY_Y, 5, 2, 5, 2, true, 45);
    // Oś X na dokładnie 5 kresek (było 7!)
    lv_chart_set_axis_tick(chart_press, LV_CHART_AXIS_PRIMARY_X, 5, 2, 5, 1, true, 20);
    lv_chart_set_point_count(chart_press, 96); 
    
    lv_obj_add_event_cb(chart_press, chart_x_axis_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    ser_press = lv_chart_add_series(chart_press, lv_palette_main(LV_PALETTE_GREEN), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(chart_press, ser_press, LV_CHART_POINT_NONE);
}

void graph_event_cb(lv_event_t * e) {
    lv_scr_load(graph_screen);
}

void create_graph_button(lv_obj_t * scr)
{
    static lv_style_t style_btn;
    lv_style_init(&style_btn);
    lv_style_set_radius(&style_btn, 25);
    lv_style_set_bg_color(&style_btn, lv_palette_main(LV_PALETTE_GREY));
    lv_style_set_bg_opa(&style_btn, LV_OPA_10);
    // Dodajemy obramowanie, żeby przycisk idealnie pasował stylem do fasolek pogodowych
    lv_style_set_border_width(&style_btn, 1);
    lv_style_set_border_color(&style_btn, lv_palette_main(LV_PALETTE_GREY));

    // TRIK: Tworzymy zwykły "pusty" obiekt zamiast gotowego przycisku
    lv_obj_t * btn = lv_obj_create(scr);
    lv_obj_set_size(btn, 110, 50); // Dałem 110 szerokości, żeby tekst lepiej leżał
    lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 135, -10);
    lv_obj_add_style(btn, &style_btn, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_40, LV_STATE_PRESSED);
    
    // Blokujemy scrollowanie i włączamy klikalność!
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE); 

    lv_obj_t * label = lv_label_create(btn);
    // Łamiemy linię, żeby napis nie wyszedł poza przycisk
    lv_label_set_text(label, "WYKRESY\nPOMIARÓW"); 
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0); // Wyśrodkowanie
    lv_obj_set_style_text_font(label, &montserrat_pl_14, 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, graph_event_cb, LV_EVENT_CLICKED, NULL);

}

void create_weather_info_screen(void)
{
    weather_info_screen = lv_obj_create(NULL);
    
    // Tytuł
    lbl_weather_city = lv_label_create(weather_info_screen);
    lv_label_set_text_fmt(lbl_weather_city, "PROGNOZA POGODY DLA: %s", current_city); // Wkleja zmienną current_city
    lv_obj_set_style_text_font(lbl_weather_city, &montserrat_pl_14, 0);
    lv_obj_align(lbl_weather_city, LV_ALIGN_TOP_MID, 0, 10);

    // --- KONTENER KARUZELI ---
    lv_obj_t * scroll_cont = lv_obj_create(weather_info_screen);
    lv_obj_set_size(scroll_cont, 320, 160); // Zajmuje środek ekranu
    lv_obj_align(scroll_cont, LV_ALIGN_TOP_MID, 0, 35);
    
    // Ustawiamy jako poziomy rządek (Flex Row)
    lv_obj_set_flex_flow(scroll_cont, LV_FLEX_FLOW_ROW);
    // Włączamy "przyciąganie" (Snap) przewijania, żeby karty fajnie wskakiwały na środek
    lv_obj_set_scrollbar_mode(scroll_cont, LV_SCROLLBAR_MODE_OFF); // Ukrywamy brzydki pasek przewijania
    lv_obj_set_scroll_dir(scroll_cont, LV_DIR_HOR); // Ustawiamy przewijanie tylko w poziomie
    lv_obj_add_flag(scroll_cont, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    
    lv_obj_set_style_bg_opa(scroll_cont, 0, 0);
    lv_obj_set_style_border_opa(scroll_cont, 0, 0);
    lv_obj_set_style_pad_all(scroll_cont, 10, 0);
    lv_obj_set_style_pad_column(scroll_cont, 15, 0); // Przerwy między kartami

    const char * dummy_days[7] = {"DZIŚ", "JUTRO", "ŚRO", "CZW", "PIĄ", "SOB", "NIE"};

    // Tworzymy 7 pionowych kart w pętli
    for(int i = 0; i < 7; i++) {
        lv_obj_t * card = lv_obj_create(scroll_cont);
        lv_obj_set_size(card, 100, 140); // Wąska i wysoka karta
        lv_obj_set_style_radius(card, 20, 0);
        lv_obj_set_style_bg_color(card, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_10, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, lv_palette_main(LV_PALETTE_GREY), 0);
        
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        
        // Zabezpieczenie dotyku
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_SNAPPABLE); // Mówi kontenerowi "Przyciągaj do mnie!"

        // 1. Nazwa dnia
        forecast_day_labels[i] = lv_label_create(card);
        lv_label_set_text(forecast_day_labels[i], dummy_days[i]);
        lv_obj_set_style_text_font(forecast_day_labels[i], &montserrat_pl_14, 0);

        // 2. Ikona Pogody
        forecast_icon_labels[i] = lv_label_create(card);
        lv_label_set_text(forecast_icon_labels[i], SYM_W_SUN); // Tymczasowe słoneczko
        lv_obj_set_style_text_font(forecast_icon_labels[i], &weather_icons, 0); // Większa czcionka dla ikony

        // 3. Temperatury (Max i Min)
        forecast_temp_labels[i] = lv_label_create(card);
        // Tymczasowe, malejące temperatury dla bajeru wizualnego
        lv_label_set_text_fmt(forecast_temp_labels[i], "2%d°C\n 1%d°C", 5-i, 2+i); 
        lv_obj_set_style_text_align(forecast_temp_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(forecast_temp_labels[i], &montserrat_pl_14, 0);
    }
    
    // --- PRZYCISK POWRÓT ---
    lv_obj_t * btn_back = lv_btn_create(weather_info_screen);
    lv_obj_set_size(btn_back, 80, 30);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_event_cb(btn_back, back_event_to_main, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "WRÓĆ");
    lv_obj_set_style_text_font(lbl_back, &montserrat_pl_14, 0);
    lv_obj_center(lbl_back);
}

void weather_info_event_cb(lv_event_t * e) {
    lv_scr_load(weather_info_screen);
}

void create_weather_info_button(lv_obj_t * scr)
{
    static lv_style_t style_btn;
    lv_style_init(&style_btn);
    lv_style_set_radius(&style_btn, 25);
    lv_style_set_bg_color(&style_btn, lv_palette_main(LV_PALETTE_GREY));
    lv_style_set_bg_opa(&style_btn, LV_OPA_10);
    // Dodajemy obramowanie, żeby przycisk idealnie pasował stylem do fasolek pogodowych
    lv_style_set_border_width(&style_btn, 1);
    lv_style_set_border_color(&style_btn, lv_palette_main(LV_PALETTE_GREY));

    // TRIK: Tworzymy zwykły "pusty" obiekt zamiast gotowego przycisku
    lv_obj_t * btn = lv_obj_create(scr);
    lv_obj_set_size(btn, 110, 50); // Dałem 110 szerokości, żeby tekst lepiej leżał
    lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_style(btn, &style_btn, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_40, LV_STATE_PRESSED);
    
    // Blokujemy scrollowanie i włączamy klikalność!
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE); 

    lv_obj_t * label = lv_label_create(btn);
    // Łamiemy linię, żeby napis nie wyszedł poza przycisk
    lv_label_set_text(label, "PROGNOZA\nPOGODY"); 
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0); // Wyśrodkowanie
    lv_obj_set_style_text_font(label, &montserrat_pl_14, 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, weather_info_event_cb, LV_EVENT_CLICKED, NULL);
}



// --- TWORZENIE GŁÓWNEGO INTERFEJSU LVGL ---
void create_weather_ui(void) {

    main_screen = lv_scr_act();
    lv_obj_t * scr = lv_scr_act();

    // --- LOGO POLITECHNIKI ---
    lv_obj_t * logo = lv_img_create(scr);
    lv_img_set_src(logo, &put_logo);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 2);

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
    create_graph_button(scr); // Stworzenie przycisku wykresu
    create_weather_info_button(scr); // Stworzenie przycisku prognozy pogody
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
// ZEGAR //
void collect_time_task(void *pvParameters) {
    ESP_LOGI("TIME", "Zadanie czasu uruchomione");
    
    char saved_ssid[33] = {0};
    char saved_pass[64] = {0};

    while(1) {
        // Próbujemy wczytać z pamięci NVS. Jeśli się nie uda, czekamy.
        if (load_wifi_credentials(saved_ssid, saved_pass, sizeof(saved_pass))) {
            
            is_wifi_connecting = true; // Zacznie migać ikonka na status barze po włączeniu zasilania
            bool connected = wifi_connect_station(saved_ssid, saved_pass);
            is_wifi_connecting = false;
            
            if (connected) { 
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
        } else {
            ESP_LOGW("TIME", "Brak zapisanych danych WiFi w pamięci. Skonfiguruj przez ekran dotykowy.");
            vTaskDelay(pdMS_TO_TICKS(10000)); // Czekaj 10s na konfigurację użytkownika
        }
    }
}


uint8_t get_battery_level(uint16_t v) {
    // Odcięcie i 100%
    if (v >= 3000) return 100;
    if (v <= 2000) return 0;

    // 1. Faza początkowa: Szybki spadek napięcia po wyjęciu z blistra
    // Od 3.0V do 2.8V (3000 - 2800mV) ucieka pierwsze 15% pojemności
    if (v > 2800) {
        return 85 + 15 * (v - 2800) / 200; 
    }
    // 2. Faza robocza (Płaskowyż): Baterie długo trzymają to napięcie
    // Od 2.8V do 2.4V (2800 - 2400mV) oddają większość energii (ok. 55% pojemności)
    else if (v > 2400) {
        return 30 + 55 * (v - 2400) / 400;
    }
    // 3. Faza końcowa: Wyraźny spadek
    // Od 2.4V do 2.2V (2400 - 2200mV) to przedostatnie 20% życia
    else if (v > 2200) {
        return 10 + 20 * (v - 2200) / 200;
    }
    // 4. Faza "Agonii": Napięcie leci w dół
    // Od 2.2V do 2.0V (2200 - 2000mV) zostaje ostatnie 10% na "dokończenie spraw"
    else {
        return 10 * (v - 2000) / 200;
    }
}

// --- MAPOWANIE KODÓW POGODY (WMO) NA TWOJE IKONY ---
const char* get_weather_icon(int code) {
    if (code == 0 || code == 1) return SYM_W_SUN; // Czyste niebo
    if (code == 2) return SYM_W_CLOUD_SUN; // Częściowe zachmurzenie
    if (code == 3 || code == 45 || code == 48) return SYM_W_CLOUD; // Pochmurno / Mgła
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return SYM_W_RAIN; // Różne rodzaje deszczu
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return SYM_W_SNOW; // Śnieg
    if (code >= 95) return SYM_W_STORM; // Burze
    return SYM_W_SUN; // Domyślnie słońce w razie błędu
}


// Pomocnicza funkcja do zamiany spacji na znaki w linku (np. "Nowy Jork" -> "Nowy%20Jork")
void encode_url_spaces(const char *src, char *dest) {
    while (*src) {
        if (*src == ' ') { *dest++ = '%'; *dest++ = '2'; *dest++ = '0'; }
        else { *dest++ = *src; }
        src++;
    }
    *dest = '\0';
}

void fetch_weather_task(void *pvParameters) {
    const char* pl_days[] = {"NIE", "PON", "WTO", "ŚRO", "CZW", "PIĄ", "SOB"};

    while(1) {
        time_t now; struct tm timeinfo;
        time(&now); localtime_r(&now, &timeinfo);
        bool is_time_synced = (timeinfo.tm_year > (2020 - 1900));

        wifi_ap_record_t ap_info;
        bool is_wifi_connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);

        if (is_wifi_connected && is_time_synced && force_weather_update) {
            ESP_LOGI(TAG, "Szukanie koordynatow dla miasta: %s", current_city);
            force_weather_update = false; // Reset flagi
            
            char encoded_city[128];
            encode_url_spaces(current_city, encoded_city);

            // 1. ZAPYTANIE DO GEOKODOWANIA (Szukanie miasta)
            char geo_url[256];
            snprintf(geo_url, sizeof(geo_url), "http://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=pl&format=json", encoded_city);

            esp_http_client_config_t config = { .url = geo_url, .method = HTTP_METHOD_GET, .timeout_ms = 5000 };
            esp_http_client_handle_t client = esp_http_client_init(&config);
            esp_err_t err = esp_http_client_open(client, 0);

            float lat = 200.0, lon = 200.0; // Wartości domyślne (błędne)

            if (err == ESP_OK) {
                esp_http_client_fetch_headers(client);
                char *buffer = malloc(2048);
                if (buffer) {
                    int total_read = 0;
                    while (total_read < 2047) {
                        int read_len = esp_http_client_read(client, buffer + total_read, 2047 - total_read);
                        if (read_len <= 0) break;
                        total_read += read_len;
                        vTaskDelay(pdMS_TO_TICKS(1));
                    }
                    buffer[total_read] = 0;
                    
                    cJSON *root = cJSON_Parse(buffer);
                    if (root) {
                        cJSON *results = cJSON_GetObjectItem(root, "results");
                        if (cJSON_IsArray(results) && cJSON_GetArraySize(results) > 0) {
                            cJSON *first_result = cJSON_GetArrayItem(results, 0);
                            lat = cJSON_GetObjectItem(first_result, "latitude")->valuedouble;
                            lon = cJSON_GetObjectItem(first_result, "longitude")->valuedouble;
                            ESP_LOGI(TAG, "Znaleziono: Lat: %.2f, Lon: %.2f", lat, lon);
                        }
                        cJSON_Delete(root);
                    }
                    free(buffer);
                }
            }
            esp_http_client_cleanup(client);

            // 2. JEŚLI ZNALEZIONO MIASTO -> POBIERZ POGODĘ
            if (lat != 200.0) {
                char weather_url[256];
                snprintf(weather_url, sizeof(weather_url), "http://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=Europe%%2FWarsaw", lat, lon);
                
                esp_http_client_config_t w_config = { .url = weather_url, .method = HTTP_METHOD_GET, .timeout_ms = 5000 };
                esp_http_client_handle_t w_client = esp_http_client_init(&w_config);
                err = esp_http_client_open(w_client, 0);

                if (err == ESP_OK) {
                    esp_http_client_fetch_headers(w_client);
                    char *w_buffer = malloc(2048);
                    if (w_buffer) {
                        int total_read = 0;
                        while (total_read < 2047) {
                            int read_len = esp_http_client_read(w_client, w_buffer + total_read, 2047 - total_read);
                            if (read_len <= 0) break;
                            total_read += read_len;
                        }
                        w_buffer[total_read] = 0;

                        cJSON *root = cJSON_Parse(w_buffer);
                        if (root) {
                            cJSON *daily = cJSON_GetObjectItem(root, "daily");
                            if (daily) {
                                cJSON *w_codes = cJSON_GetObjectItem(daily, "weather_code");
                                cJSON *t_max = cJSON_GetObjectItem(daily, "temperature_2m_max");
                                cJSON *t_min = cJSON_GetObjectItem(daily, "temperature_2m_min");

                                if (w_codes && t_max && t_min) {
                                    int today_wday = timeinfo.tm_wday; 
                                    lvgl_port_lock(-1);
                                    for(int i = 0; i < 7; i++) {
                                        cJSON *code_item = cJSON_GetArrayItem(w_codes, i);
                                        cJSON *max_item = cJSON_GetArrayItem(t_max, i);
                                        cJSON *min_item = cJSON_GetArrayItem(t_min, i);

                                        if (code_item && max_item && min_item) {
                                            lv_label_set_text(forecast_icon_labels[i], get_weather_icon(code_item->valueint));
                                            lv_label_set_text_fmt(forecast_temp_labels[i], "%d°C\n%d°C", (int)max_item->valuedouble, (int)min_item->valuedouble);
                                            
                                            if (i == 0) lv_label_set_text(forecast_day_labels[i], "DZIŚ");
                                            else if (i == 1) lv_label_set_text(forecast_day_labels[i], "JUTRO");
                                            else lv_label_set_text(forecast_day_labels[i], pl_days[(today_wday + i) % 7]);
                                        }
                                    }
                                    lvgl_port_unlock();
                                }
                            }
                            cJSON_Delete(root);
                        }
                        free(w_buffer);
                    }
                }
                esp_http_client_cleanup(w_client);
            } else {
                ESP_LOGE(TAG, "Nie znaleziono podanego miasta!");
            }
        }

        // 3. Sprytny "Sen" - śpimy w 2-sekundowych interwałach. 
        // Dzięki temu, jak wpiszesz nowe miasto, system wybudzi się max po 2 sekundach i pobierze dane!
        for(int s = 0; s < 7200; s += 2) { 
            if (force_weather_update) break; // Ktoś zmienił miasto, przerywamy sen!
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
}

// --- TASK AKTUALIZUJĄCY EKRAN (GUI) ---
void update_ui_task(void *pvParameters) {
    SensorData receivedData = {0};
    char buf[64];
    time_t now; 
    struct tm timeinfo;
    int start_delay_counter = 5; 
    int chart_update_counter = 590; 
    int last_day = -1; 
    bool first_chart_point = true; // <--- NOWA FLAGA DLA TRIKU WIZUALNEGO!
    
    while(1) {
        // 1. Pobieranie danych z kolejki (poza lockiem)
        xQueueReceive(xSensorQueue, &receivedData, 0);
        uint8_t battery_level = get_battery_level(receivedData.battery_mv);
        
        time(&now);
        localtime_r(&now, &timeinfo);
        
        wifi_ap_record_t ap_info;
        esp_err_t res = esp_wifi_sta_get_ap_info(&ap_info);

        // --- 2. GŁÓWNA SEKCJA RYSOWANIA (LOCK) ---
        lvgl_port_lock(-1); 

        // Aktualizacja kafelków tekstowych
        sprintf(buf, "%.2f °C", receivedData.temp_hundredths / 100.0f);
        lv_label_set_text(lbl_temp, buf); 
        sprintf(buf, "%.2f %%", receivedData.hum_x1024 / 1024.0f);
        lv_label_set_text(lbl_hum, buf);
        sprintf(buf, "%.0f hPa", receivedData.pressure_pa / 100.0f);
        lv_label_set_text(lbl_press, buf);

        // Zarządzanie nowym dniem na wykresach
        if (timeinfo.tm_year > (2020 - 1900)) {
            if (last_day == -1) last_day = timeinfo.tm_mday;
            if (last_day != timeinfo.tm_mday) {
                // Wybiła północ (zmienił się dzień)! Czyścimy wykresy:
                lv_chart_set_all_value(chart_temp, ser_temp, LV_CHART_POINT_NONE);
                lv_chart_set_all_value(chart_hum, ser_hum, LV_CHART_POINT_NONE);
                lv_chart_set_all_value(chart_press, ser_press, LV_CHART_POINT_NONE);
                
                last_day = timeinfo.tm_mday;
                first_chart_point = true; // Znowu potrzebujemy "podwójnej" kropki na starcie dnia
            }
        }
 // --- SEKCJA WYKRESÓW (Odchudzona: 15-minutowa!) ---
        chart_update_counter++;
        
        // 1800 * 500ms = 900 sekund = 15 minut
        if (chart_update_counter >= 1800) {  
            
            if (ser_temp != NULL && receivedData.pressure_pa > 0 && timeinfo.tm_year > (2020 - 1900)) {
                chart_update_counter = 0; 
                
                int32_t t = receivedData.temp_hundredths / 100;
                int32_t h = receivedData.hum_x1024 / 1024;
                int32_t p = receivedData.pressure_pa / 100;

                // TARCZA OCHRONNA (Clamping)
                if (t > 80) t = 80; else if (t < -40) t = -40;
                if (h > 100) h = 100; else if (h < 0) h = 0;
                if (p > 1200) p = 1200; else if (p < 800) p = 800;

                // OBLICZENIE POZYCJI X DLA 96 PUNKTÓW
                uint16_t point_index = (timeinfo.tm_hour * 4) + (timeinfo.tm_min / 15);

                if (point_index < 96) {
                    lv_chart_set_value_by_id(chart_temp, ser_temp, point_index, t);
                    lv_chart_set_value_by_id(chart_hum, ser_hum, point_index, h);
                    lv_chart_set_value_by_id(chart_press, ser_press, point_index, p);
                    
                    // --- TRIK WIZUALNY ---
                    if (first_chart_point && point_index > 0) {
                        lv_chart_set_value_by_id(chart_temp, ser_temp, point_index - 1, t);
                        lv_chart_set_value_by_id(chart_hum, ser_hum, point_index - 1, h);
                        lv_chart_set_value_by_id(chart_press, ser_press, point_index - 1, p);
                        first_chart_point = false; 
                    }
                }
            } else if (receivedData.pressure_pa == 0) {
                // Czujnik jeszcze nic nie wysłał? Spróbuj za sekundę.
                chart_update_counter = 1798; 
            }
        }
        

        // Aktualizacja baterii
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

        // Aktualizacja zegara
        if (timeinfo.tm_year > (2020 - 1900)) {
            sprintf(buf, "%02d.%02d.%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
            lv_label_set_text(lbl_date, buf);
            sprintf(buf, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            lv_label_set_text(lbl_time, buf);
        }

        // --- BEZPIECZNA AKTUALIZACJA IKONY WIFI (Wewnątrz LOCK) ---
        if (start_delay_counter > 0) {
            start_delay_counter--;
        } else {
            if (is_wifi_connecting) {
                update_wifi_icon(0, false); 
            } else {
                update_wifi_icon(ap_info.rssi, (res == ESP_OK));
            }
        }

        lvgl_port_unlock(); 
        // --- KONIEC LOCKA ---

        vTaskDelay(pdMS_TO_TICKS(500)); 
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
                uint32_t duty = is_night ? ((night_brightness * 255) / 100) : ((day_brightness * 255) / 100);
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

    load_brightness_settings();

    load_location_settings();

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
    lvgl_port_lock(-1);
    init_button_styles(); // Inicjalizacja stylów przycisków
    create_weather_ui();
    create_settings_ui();
    create_graph_screen();
    create_weather_info_screen();
    lv_scr_load(main_screen); // Ustawienie ekranu głównego jako aktywnego
    lvgl_port_unlock();
    
    // 3. Startowanie zadań FreeRTOS
    xTaskCreate(nrf_receiver_task, "NRF_TASK", 4096, NULL, 4, NULL);
    xTaskCreate(collect_time_task, "TIME_TASK", 8192, NULL, 4, NULL); 
    xTaskCreate(update_ui_task, "GUI_UPDATE_TASK", 8192, NULL, 5, NULL);
    xTaskCreate(night_mode_task, "NIGHT_MODE_TASK", 4096, NULL, 5, NULL);
    xTaskCreate(fetch_weather_task, "FETCH_WEATHER", 8192, NULL, 4, NULL);

    ESP_LOGI(TAG, "System dziala!");
    
    // POPRAWKA 3: Zatrzymujemy główny task w nieskończonej pętli
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}