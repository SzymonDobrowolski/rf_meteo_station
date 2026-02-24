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

// Zawsze najpierw LVGL, potem port
#include "lvgl.h"
#include "esp_lvgl_port.h"

// Twoje moduły
#include "gpio_init.h"
#include "spi_init.h"
#include "uart_init.h"
#include "nrf.h"
#include "wifi_project.h"
#include "sntp.h"
#include "lcd.h"
#include "Config.h"

static const char *TAG = "MAIN";

// Obiekty systemowe
spi_device_handle_t nrf_handle = NULL;
SemaphoreHandle_t xSpiMutex = NULL;
QueueHandle_t xSensorQueue = NULL;

// Czcionka z polskimi znakami
LV_FONT_DECLARE(montserrat_pl_14);
// Ikony dla kafelków
#define MY_SYM_TEMP     "\xEF\x8B\x8B" // Termometr (0xF2CB)
#define MY_SYM_HUM      "\xEF\x81\x83" // Kropla (0xF043)
#define MY_SYM_PRESS    "\xEF\x98\xA4" // Ciśnienie (0xF624)

// Ikony dla paska statusu (Bateria)
#define MY_SYM_BAT_100  "\xEF\x89\x80" // Pełna (0xF240)
#define MY_SYM_BAT_50   "\xEF\x89\x82" // Połowa (0xF242)
#define MY_SYM_BAT_0    "\xEF\x89\x84" // Pusta (0xF244)

// Globalne wskaźniki na obiekty LVGL (aby móc je aktualizować z innych tasków)
lv_obj_t * lbl_title;
lv_obj_t * lbl_temp;
lv_obj_t * lbl_hum;
lv_obj_t * lbl_press;
lv_obj_t * lbl_time;
lv_obj_t * lbl_date;
lv_obj_t * icon_wifi;
lv_obj_t * icon_wifi_err;

bool is_wifi_connecting = false;

static void set_obj_opa(void * obj, int32_t v);
void start_wifi_blink(void);
void update_wifi_icon(int8_t rssi, bool connected); // Dopasuj nazwę (icon czy status?)

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

}

// --- TWORZENIE GŁÓWNEGO INTERFEJSU LVGL ---
void create_weather_ui(void) {
    lvgl_port_lock(-1); 

    lv_obj_t * scr = lv_scr_act();
    // Tło główne ekranu (zaadaptuje się do trybu nocnego)
    lv_obj_set_style_bg_color(scr, lv_obj_get_style_bg_color(scr, 0), 0);

    // 1. Kontener na kafelki (Flexbox) - rządek
    lv_obj_t * cont = lv_obj_create(scr);
    lv_obj_set_size(cont, 320, 180); 
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
    lv_obj_set_size(card_temp, 95, 150);
    lv_obj_add_style(card_temp, &style_card, 0);
    lv_obj_set_flex_flow(card_temp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card_temp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * icon_t = lv_label_create(card_temp);
    lv_obj_set_style_text_font(icon_t, &montserrat_pl_14, 0);
    lv_label_set_text(icon_t, MY_SYM_TEMP);
    
    lv_obj_t * title_temp = lv_label_create(card_temp);
    lv_label_set_text(title_temp, "TEMP");
    lv_obj_set_style_text_font(title_temp, &montserrat_pl_14, 0); // Mniejszy opis
    lv_obj_set_style_opa(title_temp, LV_OPA_70, 0); // Lekko przygaszony

    lbl_temp = lv_label_create(card_temp);
    lv_obj_set_style_text_font(lbl_temp, &montserrat_pl_14, 0); // Twoja czcionka
    lv_label_set_text(lbl_temp, "--.-");

    // --- KAFELEK 2: WILGOTNOŚĆ ---
    lv_obj_t * card_hum = lv_obj_create(cont);
    lv_obj_set_size(card_hum, 95, 150);
    lv_obj_add_style(card_hum, &style_card, 0);
    lv_obj_set_flex_flow(card_hum, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card_hum, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * icon_h = lv_label_create(card_hum);
    lv_obj_set_style_text_font(icon_h, &montserrat_pl_14, 0);
    lv_label_set_text(icon_h, MY_SYM_HUM);
    
    lv_obj_t * title_hum = lv_label_create(card_hum);
    lv_label_set_text(title_hum, "WILG");
    lv_obj_set_style_text_font(title_hum, &montserrat_pl_14, 0);
    lv_obj_set_style_opa(title_hum, LV_OPA_70, 0);

    lbl_hum = lv_label_create(card_hum);
    lv_obj_set_style_text_font(lbl_hum, &montserrat_pl_14, 0);
    lv_label_set_text(lbl_hum, "--%");

    // --- KAFELEK 3: CIŚNIENIE ---
    lv_obj_t * card_press = lv_obj_create(cont);
    lv_obj_set_size(card_press, 95, 150);
    lv_obj_add_style(card_press, &style_card, 0);
    lv_obj_set_flex_flow(card_press, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card_press, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * icon_p = lv_label_create(card_press);
    lv_obj_set_style_text_font(icon_p, &montserrat_pl_14, 0);
    lv_label_set_text(icon_p, MY_SYM_PRESS);
    
    lv_obj_t * title_press = lv_label_create(card_press);
    lv_label_set_text(title_press, "CIŚN");
    lv_obj_set_style_text_font(title_press, &montserrat_pl_14, 0);
    lv_obj_set_style_opa(title_press, LV_OPA_70, 0);

    lbl_press = lv_label_create(card_press);
    lv_obj_set_style_text_font(lbl_press, &montserrat_pl_14, 0);
    lv_label_set_text(lbl_press, "----");

    create_status_bar(scr);
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

                // 1. Zmiana motywu LVGL
                lv_obj_report_style_change(NULL);
                lvgl_port_lock(-1); // POPRAWKA 2
                lv_disp_t * disp = lv_disp_get_default();
                lv_theme_t * th = lv_theme_default_init(disp,
                    lv_palette_main(LV_PALETTE_BLUE), // Kolor akcentów 1
                    lv_palette_main(LV_PALETTE_RED),  // Kolor akcentów 2
                    is_night,                         // TRUE = tryb ciemny
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

    // 2. Rysowanie początkowego interfejsu
    create_weather_ui();
    
    // 3. Startowanie zadań FreeRTOS
    xTaskCreate(nrf_receiver_task, "NRF_TASK", 4096, NULL, 4, NULL);
    xTaskCreate(collect_time_task, "TIME_TASK", 8192, NULL, 4, NULL); 
    xTaskCreate(update_ui_task, "GUI_UPDATE_TASK", 4096, NULL, 5, NULL);
    xTaskCreate(night_mode_task, "NIGHT_MODE_TASK", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "System dziala!");
    
    // POPRAWKA 3: Zatrzymujemy główny task w nieskończonej pętli
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}